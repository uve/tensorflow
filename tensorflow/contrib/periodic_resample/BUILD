load("//tensorflow:tensorflow.bzl", "py_test")
load(
    "//tensorflow:tensorflow.bzl",
    "tf_cc_test",
    "tf_custom_op_library",
    "tf_gen_op_libs",
    "tf_gen_op_wrapper_py",
)
load("//tensorflow:tensorflow.bzl", "tf_custom_op_py_library")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

exports_files(["LICENSE"])

cc_library(
    name = "all_ops",
    srcs = [":custom_op_sources"],
    hdrs = [":custom_op_headers"],
    deps = [
        "//tensorflow/core:framework_headers_lib",
        "//third_party/eigen3",
        "@com_google_protobuf//:protobuf_headers",
    ],
    alwayslink = 1,
)

tf_custom_op_library(
    name = "python/ops/_periodic_resample_op.so",
    srcs = [
        ":custom_op_headers",
        ":custom_op_sources",
    ],
)

tf_gen_op_libs(
    op_lib_names = ["array_ops"],
)

tf_gen_op_wrapper_py(
    name = "gen_periodic_resample_op_py",
    out = "python/ops/gen_periodic_resample_op.py",
    deps = [":array_ops_op_lib"],
)

tf_custom_op_py_library(
    name = "periodic_resample_op_py",
    srcs = ["python/ops/periodic_resample_op.py"],
    dso = ["python/ops/_periodic_resample_op.so"],
    kernels = [
        ":array_ops_op_lib",
    ],
    srcs_version = "PY2AND3",
    deps = [
        ":gen_periodic_resample_op_py",
        "//tensorflow/core:protos_all_py",
        "//tensorflow/python:framework_for_generated_wrappers",
    ],
)

py_library(
    name = "init_py",
    srcs = [
        "__init__.py",
        "python/__init__.py",
    ],
    srcs_version = "PY2AND3",
    tags = [
        "notap",
    ],
    deps = [
        ":periodic_resample_op_py",
    ],
)

py_test(
    name = "periodic_resample_op_test",
    srcs = ["python/kernel_tests/periodic_resample_op_test.py"],
    python_version = "PY2",
    srcs_version = "PY2AND3",
    tags = [
        "notap",
    ],
    deps = [
        ":init_py",
        "//tensorflow/contrib/util:util_py",
        "//tensorflow/python:framework_test_lib",
        "//tensorflow/python:gradient_checker",
    ],
)

tf_cc_test(
    name = "periodic_resample_op_cc_test",
    size = "small",
    srcs = [
        "ops/array_ops_test.cc",
    ],
    deps = [
        ":all_ops",
        "//tensorflow/core:framework",
        "//tensorflow/core:test",
        "//tensorflow/core:test_main",
        "//tensorflow/core:testlib",
    ],
)

# py_library(
#     name = "periodic_resample_op_py",
#     srcs = ["python/ops/periodic_resample_op.py"],
#     data = ["python/ops/_periodic_resample_op.so"],
#     srcs_version = "PY2AND3",
# )

filegroup(
    name = "custom_op_sources",
    srcs = glob(
        [
            "ops/*.cc",
            "kernels/*.cc",
        ],
        exclude = [
            "ops/*_test.cc",
            "kernels/*_test.cc",
        ],
    ),
)

filegroup(
    name = "custom_op_headers",
    srcs = glob(
        [
            "kernels/*.h",
            "ops/*.h",
        ],
    ),
)
