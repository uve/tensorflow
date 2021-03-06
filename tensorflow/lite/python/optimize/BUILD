load("//tensorflow:tensorflow.bzl", "tf_py_wrap_cc")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

cc_library(
    name = "calibration_wrapper_lib",
    srcs = ["calibration_wrapper.cc"],
    hdrs = ["calibration_wrapper.h"],
    deps = [
        "//tensorflow/lite:framework",
        "//tensorflow/lite/c:c_api_internal",
        "//tensorflow/lite/kernels:builtin_ops",
        "//tensorflow/lite/python/interpreter_wrapper:numpy",
        "//tensorflow/lite/python/interpreter_wrapper:python_error_reporter",
        "//tensorflow/lite/python/interpreter_wrapper:python_utils",
        "//tensorflow/lite/tools/optimize:quantize_model",
        "//tensorflow/lite/tools/optimize/calibration:calibration_reader",
        "//tensorflow/lite/tools/optimize/calibration:calibrator_lib",
        "//third_party/py/numpy:headers",
        "//third_party/python_runtime:headers",
        "@com_google_absl//absl/memory",
    ],
)

tf_py_wrap_cc(
    name = "tensorflow_lite_wrap_calibration_wrapper",
    srcs = [
        "calibration_wrapper.i",
    ],
    deps = [
        ":calibration_wrapper_lib",
        "//third_party/python_runtime:headers",
    ],
)

py_library(
    name = "calibrator",
    srcs = [
        "calibrator.py",
    ],
    srcs_version = "PY2AND3",
    visibility = ["//visibility:public"],
    deps = [
        "//tensorflow/lite/python/optimize:tensorflow_lite_wrap_calibration_wrapper",
        "//tensorflow/python:util",
        "//third_party/py/numpy",
    ],
)

py_test(
    name = "calibrator_test",
    srcs = ["calibrator_test.py"],
    data = [
        ":test_data",
        "//tensorflow/lite:testdata/multi_add.bin",
    ],
    python_version = "PY2",
    srcs_version = "PY2AND3",
    tags = ["no_oss"],
    deps = [
        ":calibrator",
        "//tensorflow/lite/python:lite_constants",
        "//tensorflow/lite/python/optimize:tensorflow_lite_wrap_calibration_wrapper",
        "//tensorflow/python:client_testlib",
        "//tensorflow/python:framework_test_lib",
        "//tensorflow/python:platform",
        "//third_party/py/numpy",
    ],
)
