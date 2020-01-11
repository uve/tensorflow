/* Copyright 2017 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#ifndef TENSORFLOW_COMPILER_XLA_SERVICE_COPY_INSERTION_H_
#define TENSORFLOW_COMPILER_XLA_SERVICE_COPY_INSERTION_H_

#include "tensorflow/compiler/xla/service/hlo_alias_analysis.h"
#include "tensorflow/compiler/xla/service/hlo_computation.h"
#include "tensorflow/compiler/xla/service/hlo_instruction.h"
#include "tensorflow/compiler/xla/service/hlo_module.h"
#include "tensorflow/compiler/xla/service/hlo_pass_interface.h"

namespace xla {

// Copy insertion is a legalization HLO pass which inserts copies (kCopy
// instructions) to eliminate several kinds of problems in the HLO module.
//
//   (1) Entry parameter or a constant live out of the entry computation.  Entry
//       computation arguments and constants have different lifetimes than the
//       computation result and cannot share the same allocation. Parameters and
//       constants live out of non-entry computations do not need copies.
//
//   (2) Different values which are simultaneously live and which must be held
//       in the same buffer. This can occur in while bodies. Specifically, the
//       while loop state (the arguments to the while instruction) is updated
//       in-place and the update may clobber the value from the previous
//       iteration before the previous value is dead. Computations called from
//       kCall instructions do not need such copies because kCall has no update
//       in-place semantics.
//
//   (3) The buffer set of the root instruction of the entry computation must be
//       unambiguous and distinct. That is, InstructionAliasSet::IsAmbiguous and
//       InstructionAliasSet::IsDistinct return true.
class CopyInsertion : public HloModulePass {
 public:
  absl::string_view name() const override { return "copy-insertion"; }

  // backend specific function that decides whether an instruction
  // can share buffer with its operand.
  //
  // TODO(b/80315712): Find a better way to tell whether a fusion can share
  // buffer.
  explicit CopyInsertion(
      const HloDataflowAnalysis::CanShareBuffer& can_share_buffer = nullptr)
      : can_share_buffer_(can_share_buffer) {}

  // Run the pass on the given module. Returns whether the module was changed
  // (copies were inserted).
  StatusOr<bool> Run(HloModule* module) override;

  // Try to remove as many copies from the module as possible without
  // introducing live range interference. Only copy instructions that are
  // eligible for copy elision are considered for removal.
  Status RemoveUnnecessaryCopies(const HloOrdering& ordering,
                                 HloModule* module);

  // Add copies to address special constraints on the roots of computations not
  // related to live range interference:
  //
  //    (1) Entry computation root must be unambiguous and distinct.
  //
  //    (2) Any computation called by a kCall instruction must have an
  //        unambiguous root.
  //
  //    (3) Constants and parameters cannot be live out of the entry computation
  //
  Status AddSpecialCaseCopies(HloModule* module);

 protected:
  // Override which requires the caller to pass in a call graph.
  virtual Status AddSpecialCaseCopies(const CallGraph& call_graph,
                                      HloModule* module);

  // Backend specific function that decides whether an instruction can share
  // buffer with its operand.
  HloDataflowAnalysis::CanShareBuffer can_share_buffer_;

 private:
  Status AddCopiesToResolveInterference(HloModule* module);
};

}  // namespace xla

#endif  // TENSORFLOW_COMPILER_XLA_SERVICE_COPY_INSERTION_H_
