# Description:
#   contains parts of TensorFlow that are experimental or unstable and which are not supported.

load("//tensorflow:tensorflow.bzl", "py_test")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

exports_files(["LICENSE"])

py_library(
    name = "rate",
    srcs = [
        "rate.py",
    ],
    srcs_version = "PY2AND3",
    deps = [
        "//tensorflow/python:array_ops",
        "//tensorflow/python:check_ops",
        "//tensorflow/python:control_flow_ops",
        "//tensorflow/python:framework",
        "//tensorflow/python:framework_for_generated_wrappers",
        "//tensorflow/python:math_ops",
        "//tensorflow/python:sparse_ops",
        "//tensorflow/python:state_ops",
        "//tensorflow/python:util",
        "//tensorflow/python:variable_scope",
        "//tensorflow/python:variables",
    ],
)

py_test(
    name = "rate_test",
    size = "small",
    srcs = ["rate_test.py"],
    python_version = "PY2",
    tags = [
        "manual",  # TODO(b/120555555)
        "no_oss",  # TODO(b/120555555)
        "notap",  # TODO(b/120555555)
    ],
    deps = [
        ":rate",
        "//tensorflow/python:array_ops",
        "//tensorflow/python:client_testlib",
        "//tensorflow/python:data_flow_ops",
        "//tensorflow/python:errors",
        "//tensorflow/python:framework",
        "//tensorflow/python:framework_for_generated_wrappers",
        "//tensorflow/python:variables",
        "//tensorflow/python/eager:test",
    ],
)
