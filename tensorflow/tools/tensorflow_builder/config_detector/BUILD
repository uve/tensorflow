# Description:
#   TensorFlow builder (TensorFlow on Demand project).

package(
    default_visibility = [
        "//tensorflow/tools/tensorflow_builder/config_detector:__subpackages__",
    ],
    licenses = ["notice"],  # Apache 2.0
)

py_binary(
    name = "config_detector",
    srcs = ["config_detector.py"],
    data = [
        "//tensorflow/tools/tensorflow_builder/config_detector/data/golden:cuda_cc_golden",
    ],
    python_version = "PY2",
    srcs_version = "PY2AND3",
    deps = [
        ":cuda_compute_capability",
        "@absl_py//absl:app",
        "@absl_py//absl/flags",
    ],
)

py_binary(
    name = "cuda_compute_capability",
    srcs = ["data/cuda_compute_capability.py"],
    python_version = "PY2",
    srcs_version = "PY2AND3",
    deps = [
        "@absl_py//absl:app",
        "@absl_py//absl/flags",
    ],
)
