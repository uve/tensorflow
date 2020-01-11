//===- RewriterGen.cpp - MLIR pattern rewriter generator ------------------===//
//
// Copyright 2019 The MLIR Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// =============================================================================
//
// RewriterGen uses pattern rewrite definitions to generate rewriter matchers.
//
//===----------------------------------------------------------------------===//

#include "mlir/Support/STLExtras.h"
#include "mlir/TableGen/Attribute.h"
#include "mlir/TableGen/Format.h"
#include "mlir/TableGen/GenInfo.h"
#include "mlir/TableGen/Operator.h"
#include "mlir/TableGen/Pattern.h"
#include "mlir/TableGen/Predicate.h"
#include "mlir/TableGen/Type.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;
using namespace mlir;
using namespace mlir::tblgen;

namespace llvm {
template <> struct format_provider<mlir::tblgen::Pattern::IdentifierLine> {
  static void format(const mlir::tblgen::Pattern::IdentifierLine &v,
                     raw_ostream &os, StringRef style) {
    os << v.first << ":" << v.second;
  }
};
} // end namespace llvm

// Returns the bound symbol for the given op argument or op named `symbol`.
//
// Arguments and ops bound in the source pattern are grouped into a
// transient `PatternState` struct. This struct can be accessed in both
// `match()` and `rewrite()` via the local variable named as `s`.
static Twine getBoundSymbol(const StringRef &symbol) {
  return Twine("s.") + symbol;
}

// Gets the dynamic value pack's name by removing the index suffix from
// `symbol`. Returns `symbol` itself if it does not contain an index.
//
// We can use `name__<index>` to access the `<index>`-th value in the dynamic
// value pack bound to `name`. `name` is typically the results of an
// multi-result op.
static StringRef getValuePackName(StringRef symbol, unsigned *index = nullptr) {
  StringRef name, indexStr;
  unsigned idx = 0;
  std::tie(name, indexStr) = symbol.rsplit("__");
  if (indexStr.consumeInteger(10, idx)) {
    // The second part is not an index.
    return symbol;
  }
  if (index)
    *index = idx;
  return name;
}

// Formats all values from a dynamic value pack `symbol` according to the given
// `fmt` string. The `fmt` string should use `{0}` as a placeholder for `symbol`
// and `{1}` as a placeholder for the value index, which will be offsetted by
// `offset`. The `symbol` value pack has a total of `count` values.
//
// This extracts one value from the pack if `symbol` contains an index,
// otherwise it extracts all values sequentially and returns them as a
// comma-separated list.
static std::string formatValuePack(const char *fmt, StringRef symbol,
                                   unsigned count, unsigned offset) {
  auto getNthValue = [fmt, offset](StringRef results,
                                   unsigned index) -> std::string {
    return formatv(fmt, results, index + offset);
  };

  unsigned index = 0;
  StringRef name = getValuePackName(symbol, &index);
  if (name != symbol) {
    // The symbol contains an index.
    return getNthValue(name, index);
  }

  // The symbol does not contain an index. Treat the symbol as a whole.
  SmallVector<std::string, 4> values;
  values.reserve(count);
  for (unsigned i = 0; i < count; ++i)
    values.emplace_back(getNthValue(symbol, i));
  return llvm::join(values, ", ");
}

//===----------------------------------------------------------------------===//
// PatternSymbolResolver
//===----------------------------------------------------------------------===//

namespace {
// A class for resolving symbols bound in patterns.
//
// Symbols can be bound to op arguments and ops in the source pattern and ops
// in result patterns. For example, in
//
// ```
// def : Pattern<(SrcOp:$op1 $arg0, %arg1),
//               [(ResOp1:$op2), (ResOp2 $op2 (ResOp3))]>;
// ```
//
// `$argN` is bound to the `SrcOp`'s N-th argument. `$op1` is bound to `SrcOp`.
// `$op2` is bound to `ResOp1`.
//
// If a symbol binds to a multi-result op and it does not have the `__N`
// suffix, the symbol is expanded to the whole value pack generated by the
// multi-result op. If the symbol has a `__N` suffix, then it will expand to
// only the N-th result.
//
// This class keeps track of such symbols and translates them into their bound
// values.
//
// Note that we also generate local variables for unnamed DAG nodes, like
// `(ResOp3)` in the above. Since we don't bind a symbol to the op, the
// generated local variable will be implicitly named. Those implicit names are
// not tracked in this class.
class PatternSymbolResolver {
public:
  PatternSymbolResolver(const StringMap<Argument> &srcArgs,
                        const StringMap<const Operator *> &srcOperations);

  // Marks the given `symbol` as bound to a value pack with `numValues` and
  // returns true on success. Returns false if the `symbol` is already bound.
  bool add(StringRef symbol, int numValues);

  // Queries the substitution for the given `symbol`. Returns empty string if
  // symbol not found. If the symbol represents a value pack, returns all the
  // values separated via comma.
  std::string query(StringRef symbol) const;

  // Returns how many static values the given `symbol` correspond to. Returns a
  // negative value if the given symbol is not bound.
  //
  // Normally a symbol would correspond to just one value; for symbols bound to
  // multi-result ops, it can be more than one.
  int getValueCount(StringRef symbol) const;

private:
  // Symbols bound to arguments in source pattern.
  const StringMap<Argument> &sourceArguments;
  // Symbols bound to ops (for their results) in source pattern.
  const StringMap<const Operator *> &sourceOps;
  // Symbols bound to ops (for their results) in result patterns.
  // Key: symbol; value: number of values inside the pack
  StringMap<int> resultOps;
};
} // end anonymous namespace

PatternSymbolResolver::PatternSymbolResolver(
    const StringMap<Argument> &srcArgs,
    const StringMap<const Operator *> &srcOperations)
    : sourceArguments(srcArgs), sourceOps(srcOperations) {}

bool PatternSymbolResolver::add(StringRef symbol, int numValues) {
  StringRef name = getValuePackName(symbol);
  return resultOps.try_emplace(name, numValues).second;
}

std::string PatternSymbolResolver::query(StringRef symbol) const {
  StringRef name = getValuePackName(symbol);
  // Handle symbols bound to generated ops
  auto resOpIt = resultOps.find(name);
  if (resOpIt != resultOps.end())
    return formatValuePack("{0}.getOperation()->getResult({1})", symbol,
                           resOpIt->second, /*offset=*/0);

  // Handle symbols bound to matched op arguments
  auto srcArgIt = sourceArguments.find(symbol);
  if (srcArgIt != sourceArguments.end())
    return getBoundSymbol(symbol).str();

  // Handle symbols bound to matched op results
  auto srcOpIt = sourceOps.find(name);
  if (srcOpIt != sourceOps.end())
    return formatValuePack("{0}->getResult({1})", getBoundSymbol(symbol).str(),
                           srcOpIt->second->getNumResults(), /*offset=*/0);
  return {};
}

int PatternSymbolResolver::getValueCount(StringRef symbol) const {
  StringRef name = getValuePackName(symbol);
  // Handle symbols bound to generated ops
  auto resOpIt = resultOps.find(name);
  if (resOpIt != resultOps.end())
    return name == symbol ? resOpIt->second : 1;

  // Handle symbols bound to matched op arguments
  if (sourceArguments.count(symbol))
    return 1;

  // Handle symbols bound to matched op results
  auto srcOpIt = sourceOps.find(name);
  if (srcOpIt != sourceOps.end())
    return name == symbol ? srcOpIt->second->getNumResults() : 1;
  return -1;
}

//===----------------------------------------------------------------------===//
// PatternEmitter
//===----------------------------------------------------------------------===//

namespace {
class PatternEmitter {
public:
  static void emit(StringRef rewriteName, Record *p, RecordOperatorMap *mapper,
                   raw_ostream &os);

private:
  PatternEmitter(Record *pat, RecordOperatorMap *mapper, raw_ostream &os);

  // Emits the mlir::RewritePattern struct named `rewriteName`.
  void emit(StringRef rewriteName);

  // Emits the match() method.
  void emitMatchMethod(DagNode tree);

  // Collects all of the operations within the given dag tree.
  void collectOps(DagNode tree, llvm::SmallPtrSetImpl<const Operator *> &ops);

  // Emits the rewrite() method.
  void emitRewriteMethod();

  // Emits C++ statements for matching the op constrained by the given DAG
  // `tree`.
  void emitOpMatch(DagNode tree, int depth);

  // Emits C++ statements for matching the `index`-th argument of the given DAG
  // `tree` as an operand.
  void emitOperandMatch(DagNode tree, int index, int depth, int indent);

  // Emits C++ statements for matching the `index`-th argument of the given DAG
  // `tree` as an attribute.
  void emitAttributeMatch(DagNode tree, int index, int depth, int indent);

  // Returns a unique name for an value of the given `op`.
  std::string getUniqueValueName(const Operator *op);

  // Entry point for handling a rewrite pattern rooted at `resultTree` and
  // dispatches to concrete handlers. The given tree is the `resultIndex`-th
  // argument of the enclosing DAG.
  std::string handleRewritePattern(DagNode resultTree, int resultIndex,
                                   int depth);

  // Emits the C++ statement to replace the matched DAG with a value built via
  // calling native C++ code.
  std::string emitReplaceWithNativeCodeCall(DagNode resultTree);

  // Returns the C++ expression referencing the old value serving as the
  // replacement.
  std::string handleReplaceWithValue(DagNode tree);

  // Emits the C++ statement to build a new op out of the given DAG `tree` and
  // returns the variable name that this op is assigned to. If the root op in
  // DAG `tree` has a specified name, the created op will be assigned to a
  // variable of the given name. Otherwise, a unique name will be used as the
  // result value name.
  std::string emitOpCreate(DagNode tree, int resultIndex, int depth);

  // Returns the C++ expression to construct a constant attribute of the given
  // `value` for the given attribute kind `attr`.
  std::string handleConstantAttr(Attribute attr, StringRef value);

  // Returns the C++ expression to build an argument from the given DAG `leaf`.
  // `patArgName` is used to bound the argument to the source pattern.
  std::string handleOpArgument(DagLeaf leaf, StringRef patArgName);

  // Marks the symbol attached to DagNode `node` as bound. Aborts if the symbol
  // is already bound.
  void addSymbol(StringRef symbol, int numValues);

  // Gets the substitution for `symbol`. Aborts if `symbol` is not bound.
  std::string resolveSymbol(StringRef symbol);

  // Returns how many static values the given DAG `node` correspond to.
  int getNodeValueCount(DagNode node);

private:
  // Pattern instantiation location followed by the location of multiclass
  // prototypes used. This is intended to be used as a whole to
  // PrintFatalError() on errors.
  ArrayRef<llvm::SMLoc> loc;
  // Op's TableGen Record to wrapper object
  RecordOperatorMap *opMap;
  // Handy wrapper for pattern being emitted
  Pattern pattern;
  PatternSymbolResolver symbolResolver;
  // The next unused ID for newly created values
  unsigned nextValueId;
  raw_ostream &os;

  // Format contexts containing placeholder substitutations for match().
  FmtContext matchCtx;
  // Format contexts containing placeholder substitutations for rewrite().
  FmtContext rewriteCtx;

  // Number of op processed.
  int opCounter = 0;
};
} // end anonymous namespace

PatternEmitter::PatternEmitter(Record *pat, RecordOperatorMap *mapper,
                               raw_ostream &os)
    : loc(pat->getLoc()), opMap(mapper), pattern(pat, mapper),
      symbolResolver(pattern.getSourcePatternBoundArgs(),
                     pattern.getSourcePatternBoundOps()),
      nextValueId(0), os(os) {
  matchCtx.withBuilder("mlir::Builder(ctx)");
  rewriteCtx.withBuilder("rewriter");
}

std::string PatternEmitter::handleConstantAttr(Attribute attr,
                                               StringRef value) {
  if (!attr.isConstBuildable())
    PrintFatalError(loc, "Attribute " + attr.getAttrDefName() +
                             " does not have the 'constBuilderCall' field");

  // TODO(jpienaar): Verify the constants here
  return tgfmt(attr.getConstBuilderTemplate(),
               &rewriteCtx.withBuilder("rewriter"), value);
}

// Helper function to match patterns.
void PatternEmitter::emitOpMatch(DagNode tree, int depth) {
  Operator &op = tree.getDialectOp(opMap);
  if (op.isVariadic()) {
    PrintFatalError(loc, formatv("matching op '{0}' with variadic "
                                 "operands/results is unsupported right now",
                                 op.getOperationName()));
  }

  int indent = 4 + 2 * depth;
  // Skip the operand matching at depth 0 as the pattern rewriter already does.
  if (depth != 0) {
    // Skip if there is no defining operation (e.g., arguments to function).
    os.indent(indent) << formatv("if (!op{0}) return matchFailure();\n", depth);
    os.indent(indent) << formatv(
        "if (!isa<{1}>(op{0})) return matchFailure();\n", depth,
        op.getQualCppClassName());
  }
  if (tree.getNumArgs() != op.getNumArgs()) {
    PrintFatalError(loc, formatv("op '{0}' argument number mismatch: {1} in "
                                 "pattern vs. {2} in definition",
                                 op.getOperationName(), tree.getNumArgs(),
                                 op.getNumArgs()));
  }

  // If the operand's name is set, set to that variable.
  auto name = tree.getSymbol();
  if (!name.empty())
    os.indent(indent) << formatv("{0} = op{1};\n", getBoundSymbol(name), depth);

  for (int i = 0, e = tree.getNumArgs(); i != e; ++i) {
    auto opArg = op.getArg(i);

    // Handle nested DAG construct first
    if (DagNode argTree = tree.getArgAsNestedDag(i)) {
      os.indent(indent) << "{\n";
      os.indent(indent + 2)
          << formatv("auto op{0} = op{1}->getOperand({2})->getDefiningOp();\n",
                     depth + 1, depth, i);
      emitOpMatch(argTree, depth + 1);
      os.indent(indent + 2)
          << formatv("s.autogeneratedRewritePatternOps[{0}] = op{1};\n",
                     ++opCounter, depth + 1);
      os.indent(indent) << "}\n";
      continue;
    }

    // Next handle DAG leaf: operand or attribute
    if (opArg.is<NamedTypeConstraint *>()) {
      emitOperandMatch(tree, i, depth, indent);
    } else if (opArg.is<NamedAttribute *>()) {
      emitAttributeMatch(tree, i, depth, indent);
    } else {
      PrintFatalError(loc, "unhandled case when matching op");
    }
  }
}

void PatternEmitter::emitOperandMatch(DagNode tree, int index, int depth,
                                      int indent) {
  Operator &op = tree.getDialectOp(opMap);
  auto *operand = op.getArg(index).get<NamedTypeConstraint *>();
  auto matcher = tree.getArgAsLeaf(index);

  // If a constraint is specified, we need to generate C++ statements to
  // check the constraint.
  if (!matcher.isUnspecified()) {
    if (!matcher.isOperandMatcher()) {
      PrintFatalError(
          loc, formatv("the {1}-th argument of op '{0}' should be an operand",
                       op.getOperationName(), index + 1));
    }

    // Only need to verify if the matcher's type is different from the one
    // of op definition.
    if (operand->constraint != matcher.getAsConstraint()) {
      auto self = formatv("op{0}->getOperand({1})->getType()", depth, index);
      os.indent(indent) << "if (!("
                        << tgfmt(matcher.getConditionTemplate(),
                                 &matchCtx.withSelf(self))
                        << ")) return matchFailure();\n";
    }
  }

  // Capture the value
  auto name = tree.getArgName(index);
  if (!name.empty()) {
    os.indent(indent) << getBoundSymbol(name) << " = op" << depth
                      << "->getOperand(" << index << ");\n";
  }
}

void PatternEmitter::emitAttributeMatch(DagNode tree, int index, int depth,
                                        int indent) {
  Operator &op = tree.getDialectOp(opMap);
  auto *namedAttr = op.getArg(index).get<NamedAttribute *>();
  const auto &attr = namedAttr->attr;

  os.indent(indent) << "{\n";
  indent += 2;
  os.indent(indent) << formatv(
      "auto attr = op{0}->getAttrOfType<{1}>(\"{2}\");\n", depth,
      attr.getStorageType(), namedAttr->name);

  // TODO(antiagainst): This should use getter method to avoid duplication.
  if (attr.hasDefaultValueInitializer()) {
    os.indent(indent) << "if (!attr) attr = "
                      << tgfmt(attr.getConstBuilderTemplate(), &matchCtx,
                               attr.getDefaultValueInitializer())
                      << ";\n";
  } else if (attr.isOptional()) {
    // For a missing attribut that is optional according to definition, we
    // should just capature a mlir::Attribute() to signal the missing state.
    // That is precisely what getAttr() returns on missing attributes.
  } else {
    os.indent(indent) << "if (!attr) return matchFailure();\n";
  }

  auto matcher = tree.getArgAsLeaf(index);
  if (!matcher.isUnspecified()) {
    if (!matcher.isAttrMatcher()) {
      PrintFatalError(
          loc, formatv("the {1}-th argument of op '{0}' should be an attribute",
                       op.getOperationName(), index + 1));
    }

    // If a constraint is specified, we need to generate C++ statements to
    // check the constraint.
    os.indent(indent) << "if (!("
                      << tgfmt(matcher.getConditionTemplate(),
                               &matchCtx.withSelf("attr"))
                      << ")) return matchFailure();\n";
  }

  // Capture the value
  auto name = tree.getArgName(index);
  if (!name.empty()) {
    os.indent(indent) << getBoundSymbol(name) << " = attr;\n";
  }

  indent -= 2;
  os.indent(indent) << "}\n";
}

void PatternEmitter::emitMatchMethod(DagNode tree) {
  // Emit the heading.
  os << R"(
  PatternMatchResult match(Operation *op0) const override {
    auto ctx = op0->getContext(); (void)ctx;
    auto state = llvm::make_unique<MatchedState>();
    auto &s = *state;
    s.autogeneratedRewritePatternOps[0] = op0;
)";

  emitOpMatch(tree, 0);

  for (auto &appliedConstraint : pattern.getConstraints()) {
    auto &constraint = appliedConstraint.constraint;
    auto &entities = appliedConstraint.entities;

    auto condition = constraint.getConditionTemplate();
    auto cmd = "if (!({0})) return matchFailure();\n";

    if (isa<TypeConstraint>(constraint)) {
      auto self = formatv("({0}->getType())", resolveSymbol(entities.front()));
      os.indent(4) << formatv(cmd,
                              tgfmt(condition, &matchCtx.withSelf(self.str())));
    } else if (isa<AttrConstraint>(constraint)) {
      PrintFatalError(
          loc, "cannot use AttrConstraint in Pattern multi-entity constraints");
    } else {
      // TODO(b/138794486): replace formatv arguments with the exact specified
      // args.
      if (entities.size() > 4) {
        PrintFatalError(loc, "only support up to 4-entity constraints now");
      }
      SmallVector<std::string, 4> names;
      int i = 0;
      for (int e = entities.size(); i < e; ++i)
        names.push_back(resolveSymbol(entities[i]));
      std::string self = appliedConstraint.self;
      if (!self.empty())
        self = resolveSymbol(self);
      for (; i < 4; ++i)
        names.push_back("<unused>");
      os.indent(4) << formatv(cmd,
                              tgfmt(condition, &matchCtx.withSelf(self),
                                    names[0], names[1], names[2], names[3]));
    }
  }

  os.indent(4) << "return matchSuccess(std::move(state));\n  }\n";
}

void PatternEmitter::collectOps(DagNode tree,
                                llvm::SmallPtrSetImpl<const Operator *> &ops) {
  // Check if this tree is an operation.
  if (tree.isOperation())
    ops.insert(&tree.getDialectOp(opMap));

  // Recurse the arguments of the tree.
  for (unsigned i = 0, e = tree.getNumArgs(); i != e; ++i)
    if (auto child = tree.getArgAsNestedDag(i))
      collectOps(child, ops);
}

void PatternEmitter::emit(StringRef rewriteName) {
  // Get the DAG tree for the source pattern
  DagNode tree = pattern.getSourcePattern();

  const Operator &rootOp = pattern.getSourceRootOp();
  auto rootName = rootOp.getOperationName();

  // Collect the set of result operations.
  llvm::SmallPtrSet<const Operator *, 4> results;
  for (unsigned i = 0, e = pattern.getNumResultPatterns(); i != e; ++i)
    collectOps(pattern.getResultPattern(i), results);

  // Emit RewritePattern for Pattern.
  auto locs = pattern.getLocation();
  os << formatv("/* Generated from:\n\t{0:$[ instantiating\n\t]}\n*/\n",
                make_range(locs.rbegin(), locs.rend()));
  os << formatv(R"(struct {0} : public RewritePattern {
  {0}(MLIRContext *context) : RewritePattern("{1}", {{)",
                rewriteName, rootName);
  interleaveComma(results, os, [&](const Operator *op) {
    os << '"' << op->getOperationName() << '"';
  });
  os << formatv(R"(}, {0}, context) {{})", pattern.getBenefit()) << "\n";

  // Emit matched state.
  os << "  struct MatchedState : public PatternState {\n";
  for (const auto &arg : pattern.getSourcePatternBoundArgs()) {
    auto fieldName = arg.first();
    if (auto namedAttr = arg.second.dyn_cast<NamedAttribute *>()) {
      os.indent(4) << namedAttr->attr.getStorageType() << " " << fieldName
                   << ";\n";
    } else {
      os.indent(4) << "Value *" << fieldName << ";\n";
    }
  }
  for (const auto &result : pattern.getSourcePatternBoundOps()) {
    os.indent(4) << "Operation *" << result.getKey() << ";\n";
  }
  // TODO(jpienaar): Change to matchAndRewrite & capture ops with consistent
  // numbering so that it can be reused for fused loc.
  os.indent(4) << "Operation* autogeneratedRewritePatternOps["
               << pattern.getSourcePattern().getNumOps() << "];\n";
  os << "  };\n";

  emitMatchMethod(tree);
  emitRewriteMethod();

  os << "};\n";
}

void PatternEmitter::emitRewriteMethod() {
  const Operator &rootOp = pattern.getSourceRootOp();
  int numExpectedResults = rootOp.getNumResults();
  int numResultPatterns = pattern.getNumResultPatterns();

  // First register all symbols bound to ops generated in result patterns.
  for (const auto &boundOp : pattern.getResultPatternBoundOps()) {
    addSymbol(boundOp.getKey(), boundOp.getValue()->getNumResults());
  }

  // Only the last N static values generated are used to replace the matched
  // root N-result op. We need to calculate the starting index (of the results
  // of the matched op) each result pattern is to replace.
  SmallVector<int, 4> offsets(numResultPatterns + 1, numExpectedResults);
  int replStartIndex = -1;
  for (int i = numResultPatterns - 1; i >= 0; --i) {
    auto numValues = getNodeValueCount(pattern.getResultPattern(i));
    offsets[i] = offsets[i + 1] - numValues;
    if (offsets[i] == 0) {
      replStartIndex = i;
    } else if (offsets[i] < 0 && offsets[i + 1] > 0) {
      auto error = formatv(
          "cannot use the same multi-result op '{0}' to generate both "
          "auxiliary values and values to be used for replacing the matched op",
          pattern.getResultPattern(i).getSymbol());
      PrintFatalError(loc, error);
    }
  }

  if (offsets.front() > 0) {
    const char error[] = "no enough values generated to replace the matched op";
    PrintFatalError(loc, error);
  }

  os << R"(
  void rewrite(Operation *op, std::unique_ptr<PatternState> state,
               PatternRewriter &rewriter) const override {
    auto& s = *static_cast<MatchedState *>(state.get());
    auto loc = rewriter.getFusedLoc({)";
  for (int i = 0, e = pattern.getSourcePattern().getNumOps(); i != e; ++i) {
    os << (i ? ", " : "") << "s.autogeneratedRewritePatternOps[" << i
       << "]->getLoc()";
  }
  os << "}); (void)loc;\n";

  // Collect the replacement value for each result
  llvm::SmallVector<std::string, 2> resultValues;
  for (int i = 0; i < numResultPatterns; ++i) {
    DagNode resultTree = pattern.getResultPattern(i);
    resultValues.push_back(handleRewritePattern(resultTree, offsets[i], 0));
  }

  // Emit the final replaceOp() statement
  os.indent(4) << "rewriter.replaceOp(op, {";
  interleave(
      ArrayRef<std::string>(resultValues).drop_front(replStartIndex),
      [&](const std::string &name) { os << name; }, [&]() { os << ", "; });
  os << "});\n  }\n";
}

std::string PatternEmitter::getUniqueValueName(const Operator *op) {
  return formatv("v{0}{1}", op->getCppClassName(), nextValueId++);
}

std::string PatternEmitter::handleRewritePattern(DagNode resultTree,
                                                 int resultIndex, int depth) {
  if (resultTree.isNativeCodeCall())
    return emitReplaceWithNativeCodeCall(resultTree);

  if (resultTree.isReplaceWithValue())
    return handleReplaceWithValue(resultTree);

  // Create the op and get the local variable for it.
  auto results = emitOpCreate(resultTree, resultIndex, depth);
  // We need to get all the values out of this local variable if we've created a
  // multi-result op.
  const auto &numResults = pattern.getDialectOp(resultTree).getNumResults();
  return formatValuePack("{0}.getOperation()->getResult({1})", results,
                         numResults, /*offset=*/0);
}

std::string PatternEmitter::handleReplaceWithValue(DagNode tree) {
  assert(tree.isReplaceWithValue());

  if (tree.getNumArgs() != 1) {
    PrintFatalError(
        loc, "replaceWithValue directive must take exactly one argument");
  }

  if (!tree.getSymbol().empty()) {
    PrintFatalError(loc, "cannot bind symbol to replaceWithValue");
  }

  return resolveSymbol(tree.getArgName(0));
}

std::string PatternEmitter::handleOpArgument(DagLeaf leaf, StringRef argName) {
  if (leaf.isConstantAttr()) {
    auto constAttr = leaf.getAsConstantAttr();
    return handleConstantAttr(constAttr.getAttribute(),
                              constAttr.getConstantValue());
  }
  if (leaf.isEnumAttrCase()) {
    auto enumCase = leaf.getAsEnumAttrCase();
    if (enumCase.isStrCase())
      return handleConstantAttr(enumCase, enumCase.getSymbol());
    // This is an enum case backed by an IntegerAttr. We need to get its value
    // to build the constant.
    std::string val = std::to_string(enumCase.getValue());
    return handleConstantAttr(enumCase, val);
  }
  pattern.ensureBoundInSourcePattern(argName);
  std::string result = getBoundSymbol(argName).str();
  if (leaf.isUnspecified() || leaf.isOperandMatcher()) {
    return result;
  }
  if (leaf.isNativeCodeCall()) {
    return tgfmt(leaf.getNativeCodeTemplate(), &rewriteCtx.withSelf(result));
  }
  PrintFatalError(loc, "unhandled case when rewriting op");
}

std::string PatternEmitter::emitReplaceWithNativeCodeCall(DagNode tree) {
  auto fmt = tree.getNativeCodeTemplate();
  // TODO(b/138794486): replace formatv arguments with the exact specified args.
  SmallVector<std::string, 8> attrs(8);
  if (tree.getNumArgs() > 8) {
    PrintFatalError(loc, "unsupported NativeCodeCall argument numbers: " +
                             Twine(tree.getNumArgs()));
  }
  for (int i = 0, e = tree.getNumArgs(); i != e; ++i) {
    attrs[i] = handleOpArgument(tree.getArgAsLeaf(i), tree.getArgName(i));
  }
  return tgfmt(fmt, &rewriteCtx, attrs[0], attrs[1], attrs[2], attrs[3],
               attrs[4], attrs[5], attrs[6], attrs[7]);
}

void PatternEmitter::addSymbol(StringRef symbol, int numValues) {
  if (!symbolResolver.add(symbol, numValues))
    PrintFatalError(loc, formatv("symbol '{0}' bound more than once", symbol));
}

std::string PatternEmitter::resolveSymbol(StringRef symbol) {
  auto subst = symbolResolver.query(symbol);
  if (subst.empty())
    PrintFatalError(loc, formatv("referencing unbound symbol '{0}'", symbol));
  return subst;
}

int PatternEmitter::getNodeValueCount(DagNode node) {
  if (node.isOperation()) {
    // First to see whether this op is bound and we just want a specific result
    // of it with `__N` suffix in symbol.
    int count = symbolResolver.getValueCount(node.getSymbol());
    if (count >= 0)
      return count;

    // No symbol. Then we are using all the results.
    return pattern.getDialectOp(node).getNumResults();
  }
  // TODO(antiagainst): This considers all NativeCodeCall as returning one
  // value. Enhance if multi-value ones are needed.
  return 1;
}

std::string PatternEmitter::emitOpCreate(DagNode tree, int resultIndex,
                                         int depth) {
  Operator &resultOp = tree.getDialectOp(opMap);
  auto numOpArgs = resultOp.getNumArgs();

  if (resultOp.isVariadic()) {
    PrintFatalError(loc, formatv("generating op '{0}' with variadic "
                                 "operands/results is unsupported now",
                                 resultOp.getOperationName()));
  }

  if (numOpArgs != tree.getNumArgs()) {
    PrintFatalError(loc, formatv("resultant op '{0}' argument number mismatch: "
                                 "{1} in pattern vs. {2} in definition",
                                 resultOp.getOperationName(), tree.getNumArgs(),
                                 numOpArgs));
  }

  // A map to collect all nested DAG child nodes' names, with operand index as
  // the key. This includes both bound and unbound child nodes. Bound child
  // nodes will additionally be tracked in `symbolResolver` so they can be
  // referenced by other patterns. Unbound child nodes will only be used once
  // to build this op.
  llvm::DenseMap<unsigned, std::string> childNodeNames;

  // First go through all the child nodes who are nested DAG constructs to
  // create ops for them, so that we can use the results in the current node.
  // This happens in a recursive manner.
  for (int i = 0, e = resultOp.getNumOperands(); i != e; ++i) {
    if (auto child = tree.getArgAsNestedDag(i)) {
      childNodeNames[i] = handleRewritePattern(child, i, depth + 1);
    }
  }

  // Use the specified name for this op if available. Generate one otherwise.
  std::string resultValue = tree.getSymbol();
  if (resultValue.empty())
    resultValue = getUniqueValueName(&resultOp);
  // Strip the index to get the name for the value pack. This will be used to
  // name the local variable for the op.
  StringRef valuePackName = getValuePackName(resultValue);

  // Then we build the new op corresponding to this DAG node.

  // Right now we don't have general type inference in MLIR. Except a few
  // special cases listed below, we need to supply types for all results
  // when building an op.
  bool isSameOperandsAndResultType =
      resultOp.hasTrait("SameOperandsAndResultType");
  bool isBroadcastable = resultOp.hasTrait("BroadcastableTwoOperandsOneResult");
  bool useFirstAttr = resultOp.hasTrait("FirstAttrDerivedResultType");
  bool usePartialResults = valuePackName != resultValue;

  if (isSameOperandsAndResultType || isBroadcastable || useFirstAttr ||
      usePartialResults || depth > 0 || resultIndex < 0) {
    os.indent(4) << formatv("auto {0} = rewriter.create<{1}>(loc",
                            valuePackName, resultOp.getQualCppClassName());
  } else {
    // If depth == 0 and resultIndex >= 0, it means we are replacing the values
    // generated from the source pattern root op. Then we can use the source
    // pattern's value types to determine the value type of the generated op
    // here.

    // We need to specify the types for all results.
    auto resultTypes =
        formatValuePack("op->getResult({1})->getType()", valuePackName,
                        resultOp.getNumResults(), resultIndex);

    os.indent(4) << formatv("auto {0} = rewriter.create<{1}>(loc",
                            valuePackName, resultOp.getQualCppClassName())
                 << (resultTypes.empty() ? "" : ", ") << resultTypes;
  }

  // Create the builder call for the result.
  // Add operands.
  int i = 0;
  for (int e = resultOp.getNumOperands(); i < e; ++i) {
    const auto &operand = resultOp.getOperand(i);

    // Start each operand on its own line.
    (os << ",\n").indent(6);

    if (!operand.name.empty())
      os << "/*" << operand.name << "=*/";

    if (tree.isNestedDagArg(i)) {
      os << childNodeNames[i];
    } else {
      DagLeaf leaf = tree.getArgAsLeaf(i);
      auto symbol = resolveSymbol(tree.getArgName(i));
      if (leaf.isNativeCodeCall()) {
        os << tgfmt(leaf.getNativeCodeTemplate(), &rewriteCtx.withSelf(symbol));
      } else {
        os << symbol;
      }
    }
    // TODO(jpienaar): verify types
  }

  // Add attributes.
  for (int e = tree.getNumArgs(); i != e; ++i) {
    // Start each attribute on its own line.
    (os << ",\n").indent(6);
    // The argument in the op definition.
    auto opArgName = resultOp.getArgName(i);
    if (auto subTree = tree.getArgAsNestedDag(i)) {
      if (!subTree.isNativeCodeCall())
        PrintFatalError(loc, "only NativeCodeCall allowed in nested dag node "
                             "for creating attribute");
      os << formatv("/*{0}=*/{1}", opArgName,
                    emitReplaceWithNativeCodeCall(subTree));
    } else {
      auto leaf = tree.getArgAsLeaf(i);
      // The argument in the result DAG pattern.
      auto patArgName = tree.getArgName(i);
      if (leaf.isConstantAttr() || leaf.isEnumAttrCase()) {
        // TODO(jpienaar): Refactor out into map to avoid recomputing these.
        auto argument = resultOp.getArg(i);
        if (!argument.is<NamedAttribute *>())
          PrintFatalError(loc, Twine("expected attribute ") + Twine(i));
        if (!patArgName.empty())
          os << "/*" << patArgName << "=*/";
      } else {
        os << "/*" << opArgName << "=*/";
      }
      os << handleOpArgument(leaf, patArgName);
    }
  }
  os << "\n    );\n";

  return resultValue;
}

void PatternEmitter::emit(StringRef rewriteName, Record *p,
                          RecordOperatorMap *mapper, raw_ostream &os) {
  PatternEmitter(p, mapper, os).emit(rewriteName);
}

static void emitRewriters(const RecordKeeper &recordKeeper, raw_ostream &os) {
  emitSourceFileHeader("Rewriters", os);

  const auto &patterns = recordKeeper.getAllDerivedDefinitions("Pattern");
  auto numPatterns = patterns.size();

  // We put the map here because it can be shared among multiple patterns.
  RecordOperatorMap recordOpMap;

  std::vector<std::string> rewriterNames;
  rewriterNames.reserve(numPatterns);

  std::string baseRewriterName = "GeneratedConvert";
  int rewriterIndex = 0;

  for (Record *p : patterns) {
    std::string name;
    if (p->isAnonymous()) {
      // If no name is provided, ensure unique rewriter names simply by
      // appending unique suffix.
      name = baseRewriterName + llvm::utostr(rewriterIndex++);
    } else {
      name = p->getName();
    }
    PatternEmitter::emit(name, p, &recordOpMap, os);
    rewriterNames.push_back(std::move(name));
  }

  // Emit function to add the generated matchers to the pattern list.
  os << "void populateWithGenerated(MLIRContext *context, "
     << "OwningRewritePatternList *patterns) {\n";
  for (const auto &name : rewriterNames) {
    os << "  patterns->insert<" << name << ">(context);\n";
  }
  os << "}\n";
}

static mlir::GenRegistration
    genRewriters("gen-rewriters", "Generate pattern rewriters",
                 [](const RecordKeeper &records, raw_ostream &os) {
                   emitRewriters(records, os);
                   return false;
                 });
