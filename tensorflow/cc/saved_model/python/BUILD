# Description:
# CLIF wrappers for TensorFlow SavedModels.

load("//tensorflow/core/platform:default/build_config.bzl", "tf_py_clif_cc")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

tf_py_clif_cc(
    name = "loader",
    srcs = ["loader.clif"],
    deps = [
        "//tensorflow/cc/saved_model:loader",
    ],
)
