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

#ifndef TENSORFLOW_CORE_GRAPPLER_OPTIMIZERS_DATA_INJECT_PREFETCH_H_
#define TENSORFLOW_CORE_GRAPPLER_OPTIMIZERS_DATA_INJECT_PREFETCH_H_

#include "tensorflow/core/grappler/optimizers/data/optimizer_base.h"

namespace tensorflow {
namespace grappler {

// This optimization adds `Prefetch(AUTOTUNE)` after all asynchronous tf.data
// transformations. This reduces the problem of tuning buffer sizes of these
// asynchronous transformations to tuning buffer sizes of the prefetch
// transformation.
class InjectPrefetch : public TFDataOptimizerBase {
 public:
  InjectPrefetch() = default;
  ~InjectPrefetch() override = default;

  string name() const override { return "autotune_buffers"; };

  Status Init(
      const tensorflow::RewriterConfig_CustomGraphOptimizer* config) override {
    return Status::OK();
  }

  Status OptimizeAndCollectStats(Cluster* cluster, const GrapplerItem& item,
                                 GraphDef* output,
                                 OptimizationStats* stats) override;

  void Feedback(Cluster* cluster, const GrapplerItem& item,
                const GraphDef& optimize_output, double result) override;
};

}  // namespace grappler
}  // namespace tensorflow

#endif  // TENSORFLOW_CORE_GRAPPLER_OPTIMIZERS_DATA_INJECT_PREFETCH_H_
