//===- OpDocGen.cpp - MLIR operation documentation generator --------------===//
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
// OpDocGen uses the description of operations to generate documentation for the
// operations.
//
//===----------------------------------------------------------------------===//

#include "mlir/TableGen/GenInfo.h"
#include "mlir/TableGen/Operator.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Signals.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

using namespace llvm;
using namespace mlir;

using mlir::tblgen::Operator;

// Emit the description by aligning the text to the left per line (e.g.,
// removing the minimum indentation across the block).
//
// This expects that the description in the tablegen file is already formatted
// in a way the user wanted but has some additional indenting due to being
// nested in the op definition.
static void emitDescription(StringRef description, raw_ostream &os) {
  // Determine the minimum number of spaces in a line.
  size_t min_indent = -1;
  StringRef remaining = description;
  while (!remaining.empty()) {
    auto split = remaining.split('\n');
    size_t indent = split.first.find_first_not_of(" \t");
    if (indent != StringRef::npos)
      min_indent = std::min(indent, min_indent);
    remaining = split.second;
  }

  // Print out the description indented.
  os << "\n";
  remaining = description;
  bool printed = false;
  while (!remaining.empty()) {
    auto split = remaining.split('\n');
    if (split.second.empty()) {
      // Skip last line with just spaces.
      if (split.first.ltrim().empty())
        break;
    }
    // Print empty new line without spaces if line only has spaces, unless no
    // text has been emitted before.
    if (split.first.ltrim().empty()) {
      if (printed)
        os << "\n";
    } else {
      os << split.first.substr(min_indent) << "\n";
      printed = true;
    }
    remaining = split.second;
  }
}

static void emitOpDoc(const RecordKeeper &recordKeeper, raw_ostream &os) {
  const auto &defs = recordKeeper.getAllDerivedDefinitions("Op");
  os << "<!-- Autogenerated by mlir-tblgen; don't manually edit -->\n";

  // TODO: Group by dialect.
  // TODO: Add docs for types used (maybe dialect specific ones?) and link
  // between use and def.
  os << "# Operation definition\n";
  for (auto *def : defs) {
    Operator op(def);
    os << "## " << op.getOperationName() << " (" << op.getQualCppClassName()
       << ")";

    // Emit summary & description of operator.
    if (op.hasSummary())
      os << "\n" << op.getSummary() << "\n";
    os << "\n### Description:\n";
    if (op.hasDescription())
      emitDescription(op.getDescription(), os);

    // Emit operands & type of operand. All operands are numbered, some may be
    // named too.
    os << "\n### Operands:\n";
    for (const auto &operand : op.getOperands()) {
      os << "1. ";
      if (!operand.name.empty())
        os << "`" << operand.name << "`: ";
      else
        os << "&laquo;unnamed&raquo;: ";
      os << operand.constraint.getDescription() << "\n";
    }

    // Emit attributes.
    // TODO: Attributes are only documented by TableGen name, with no further
    // info. This should be improved.
    os << "\n### Attributes:\n";
    if (op.getNumAttributes() > 0) {
      os << "| Attribute | MLIR Type | Description |\n"
         << "| :-------: | :-------: | ----------- |\n";
    }
    for (auto namedAttr : op.getAttributes()) {
      os << "| `" << namedAttr.name << "` | `"
         << namedAttr.attr.getStorageType() << "` | "
         << namedAttr.attr.getDescription() << " attribute |\n";
    }

    // Emit results.
    os << "\n### Results:\n";
    for (unsigned i = 0, e = op.getNumResults(); i < e; ++i) {
      os << "1. ";
      auto name = op.getResultName(i);
      if (name.empty())
        os << "&laquo;unnamed&raquo;: ";
      else
        os << "`" << name << "`: ";
      os << op.getResultTypeConstraint(i).getDescription() << "\n";
    }

    os << "\n";
  }
}

static mlir::GenRegistration
    genRegister("gen-op-doc", "Generate operation documentation",
                [](const RecordKeeper &records, raw_ostream &os) {
                  emitOpDoc(records, os);
                  return false;
                });
