/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "tensorflow/lite/toco/graph_transformations/graph_transformations.h"
#include "tensorflow/lite/toco/model.h"
#include "tensorflow/lite/toco/tooling_util.h"
#include "tensorflow/core/platform/logging.h"

// This transformation rule tries to identify the PRelu structure generated by
// Keras, and convert it to a single op.
//
// The formula of PReLU is:
// f(x) = alpha * x for x < 0, f(x) = x for x >= 0.
//
// `x` is the input, and `alpha` is a trainable tensor which can be broadcasted
// to the shape of `x`.
//
// There's no native PRelu op in TensorFlow, so Keras generates the following
// structure which does the equivalent calculation:
// f(x) = Relu(x) + (-alpha * Relu(-x))
//
// Practically, alpha is always a constant in the inference graph, and Toco have
// other graph transformations which fold the activation functions to other ops.
// Therefore, we're looking for the structure:
//
// f(x) = Relu(x) + (negative_alpha * Neg(x, activation=Relu))

namespace toco {

::tensorflow::Status IdentifyPRelu::Run(Model* model, std::size_t op_index,
                                        bool* modified) {
  *modified = false;
  const auto add_op_it = model->operators.begin() + op_index;
  const auto* add_op = add_op_it->get();
  if (add_op == nullptr || add_op->type != OperatorType::kAdd ||
      add_op->inputs.size() != 2 ||
      add_op->fused_activation_function != FusedActivationFunctionType::kNone) {
    return ::tensorflow::Status::OK();
  }

  const auto* relu_input_op = GetOpWithOutput(*model, add_op->inputs[0]);
  if (relu_input_op == nullptr || relu_input_op->type != OperatorType::kRelu ||
      relu_input_op->inputs.size() != 1 ||
      relu_input_op->fused_activation_function !=
          FusedActivationFunctionType::kNone) {
    return ::tensorflow::Status::OK();
  }

  // TODO(ycling): Both Add and Mul are commutative. Support the case where
  // the position of operands are exchanged.
  const auto* mul_op = GetOpWithOutput(*model, add_op->inputs[1]);
  if (mul_op == nullptr || mul_op->type != OperatorType::kMul ||
      mul_op->inputs.size() != 2 ||
      mul_op->fused_activation_function != FusedActivationFunctionType::kNone) {
    return ::tensorflow::Status::OK();
  }

  const auto neg_alpha_tensor_name = mul_op->inputs[0];

  const auto* relu_neg_input_op = GetOpWithOutput(*model, mul_op->inputs[1]);

  if (relu_neg_input_op == nullptr ||
      relu_neg_input_op->inputs.size() != 1) {
    return ::tensorflow::Status::OK();
  }

  const Operator* final_input_op;
  if (relu_neg_input_op->type == OperatorType::kNeg &&
      relu_neg_input_op->fused_activation_function ==
          FusedActivationFunctionType::kRelu) {
    // This detects a Neg op with fused Relu activation function.
    final_input_op = relu_neg_input_op;
  } else {
    // This detects a Neg op followed by a separated Relu op.
    const auto* neg_input_op =
        GetOpWithOutput(*model, relu_neg_input_op->inputs[0]);
    if (neg_input_op == nullptr || neg_input_op->inputs.size() != 1 ||
        relu_neg_input_op->type != OperatorType::kRelu ||
        relu_neg_input_op->fused_activation_function !=
            FusedActivationFunctionType::kNone) {
      return ::tensorflow::Status::OK();
    }
    final_input_op = neg_input_op;
  }

  if (relu_input_op->inputs[0] != final_input_op->inputs[0]) {
    return ::tensorflow::Status::OK();
  }

  const auto input_tensor_name = relu_input_op->inputs[0];
  const auto output_tensor_name = add_op->outputs[0];

  // Construct a tensor for positive alpha (double negative).
  const auto alpha_tensor_name =
      AvailableArrayName(*model, neg_alpha_tensor_name + "_neg");
  model->GetOrCreateArray(alpha_tensor_name);

  auto* neg_neg_alpha_op = new NegOperator;
  neg_neg_alpha_op->inputs = {neg_alpha_tensor_name};
  neg_neg_alpha_op->outputs = {alpha_tensor_name};
  model->operators.emplace(add_op_it, neg_neg_alpha_op);

  auto* prelu_op = new PReluOperator;
  prelu_op->inputs = {input_tensor_name, alpha_tensor_name};
  prelu_op->outputs = {output_tensor_name};
  model->operators.emplace(add_op_it, prelu_op);
  AddMessageF("Creating %s replacing equivalent subgraph", LogName(*prelu_op));

  DeleteArrayIfUnusedOutsideOfOp(neg_alpha_tensor_name, neg_neg_alpha_op,
                                 model);
  DeleteArrayIfUnusedOutsideOfOp(mul_op->inputs[1], mul_op, model);
  DeleteOpAndArrays(model, add_op);
  *modified = true;
  return ::tensorflow::Status::OK();
}

}  // namespace toco
