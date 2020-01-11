/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#ifndef TENSORFLOW_COMPILER_MLIR_LITE_FLATBUFFER_IMPORT_H_
#define TENSORFLOW_COMPILER_MLIR_LITE_FLATBUFFER_IMPORT_H_

#include "absl/strings/string_view.h"
#include "mlir/IR/Location.h"  // TF:local_config_mlir
#include "mlir/IR/MLIRContext.h"  // TF:local_config_mlir
#include "mlir/IR/Module.h"  // TF:local_config_mlir

namespace tflite {
// Converts a TFLite flatbuffer stored in `buffer` to a MLIR module
// The buffer must live for the duration of the function call,
// The caller receives ownership of the module.
// `base_loc` is used for error reporting and debug info.
// Returns nullptr on failure, and more specific errors will be emitted
// via the context.
mlir::OwningModuleRef FlatBufferToMlir(absl::string_view buffer,
                                       mlir::MLIRContext* context,
                                       mlir::Location base_loc);
}  // namespace tflite

#endif  // TENSORFLOW_COMPILER_MLIR_LITE_FLATBUFFER_IMPORT_H_
