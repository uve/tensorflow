# Description:
#   OpKernels for boosted trees ops.

load(
    "//tensorflow:tensorflow.bzl",
    "tf_cc_test",
    "tf_kernel_library",
)
load(
    "//tensorflow/core/platform:default/build_config.bzl",
    "tf_proto_library",
)

package(
    default_visibility = [
        "//tensorflow:__subpackages__",
        "//tensorflow:internal",
    ],
    licenses = ["notice"],  # Apache 2.0
)

tf_proto_library(
    name = "boosted_trees_proto",
    srcs = [
        "boosted_trees.proto",
    ],
    cc_api_version = 2,
    visibility = ["//visibility:public"],
)

tf_kernel_library(
    name = "prediction_ops",
    srcs = ["prediction_ops.cc"],
    deps = [
        ":resource_ops",
        ":resources",
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//tensorflow/core:lib_internal",
        "//tensorflow/core/kernels/boosted_trees:boosted_trees_proto_cc",
    ],
)

cc_library(
    name = "resources",
    srcs = ["resources.cc"],
    hdrs = ["resources.h"],
    deps = [
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//tensorflow/core/kernels/boosted_trees:boosted_trees_proto_cc",
    ],
)

cc_library(
    name = "tree_helper",
    hdrs = ["tree_helper.h"],
    deps = [
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//third_party/eigen3",
    ],
)

tf_cc_test(
    name = "tree_helper_test",
    srcs = ["tree_helper_test.cc"],
    deps = [
        ":tree_helper",
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//tensorflow/core:test",
        "//tensorflow/core:test_main",
        "//tensorflow/core:testlib",
        "//third_party/eigen3",
    ],
)

tf_kernel_library(
    name = "resource_ops",
    srcs = ["resource_ops.cc"],
    deps = [
        ":resources",
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//tensorflow/core:lib_internal",
        "//tensorflow/core/kernels/boosted_trees:boosted_trees_proto_cc",
    ],
)

tf_kernel_library(
    name = "stats_ops",
    srcs = ["stats_ops.cc"],
    deps = [
        ":tree_helper",
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//third_party/eigen3",
    ],
)

tf_kernel_library(
    name = "training_ops",
    srcs = ["training_ops.cc"],
    deps = [
        ":resources",
        ":tree_helper",
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//tensorflow/core:lib_internal",
        "//tensorflow/core/kernels/boosted_trees:boosted_trees_proto_cc",
        "//third_party/eigen3",
    ],
)

tf_kernel_library(
    name = "quantile_ops",
    srcs = ["quantile_ops.cc"],
    deps = [
        "//tensorflow/core:framework",
        "//tensorflow/core:lib",
        "//tensorflow/core:lib_internal",
        "//tensorflow/core/kernels/boosted_trees/quantiles:weighted_quantiles",
    ],
)

tf_kernel_library(
    name = "boosted_trees_ops",
    deps = [
        ":prediction_ops",
        ":quantile_ops",
        ":resource_ops",
        ":stats_ops",
        ":training_ops",
    ],
)
