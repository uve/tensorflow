package(
    default_visibility = [
        "//tensorflow:internal",
    ],
    licenses = ["notice"],  # Apache 2.0
)

exports_files(["LICENSE"])

cc_library(
    name = "grpc_eager_service",
    srcs = ["grpc_eager_service.h"],
    hdrs = ["grpc_eager_service.h"],
    deps = [
        "//tensorflow:grpc++",
        "//tensorflow/core:eager_service_proto_cc",
        "//tensorflow/stream_executor/platform",
    ],
)

cc_library(
    name = "grpc_eager_client",
    srcs = ["grpc_eager_client.cc"],
    hdrs = ["grpc_eager_client.h"],
    deps = [
        ":grpc_eager_service",
        "//tensorflow:grpc++",
        "//tensorflow/core:eager_service_proto_cc",
        "//tensorflow/core:lib",
        "//tensorflow/core/distributed_runtime/eager:eager_client",
        "//tensorflow/core/distributed_runtime/rpc:grpc_channel",
        "//tensorflow/core/distributed_runtime/rpc:grpc_client_cq_tag",
        "//tensorflow/core/distributed_runtime/rpc:grpc_state",
        "//tensorflow/core/distributed_runtime/rpc:grpc_util",
    ],
)

cc_library(
    name = "grpc_eager_service_impl",
    srcs = ["grpc_eager_service_impl.cc"],
    hdrs = ["grpc_eager_service_impl.h"],
    deps = [
        ":grpc_eager_service",
        "//tensorflow:grpc++",
        "//tensorflow/core:eager_service_proto_cc",
        "//tensorflow/core:framework",
        "//tensorflow/core:ptr_util",
        "//tensorflow/core/distributed_runtime/eager:eager_service_impl",
        "//tensorflow/core/distributed_runtime/rpc:async_service_interface",
        "//tensorflow/core/distributed_runtime/rpc:grpc_call",
        "//tensorflow/core/distributed_runtime/rpc:grpc_channel",
        "//tensorflow/core/distributed_runtime/rpc:grpc_util",
        "//tensorflow/core/distributed_runtime/rpc:grpc_worker_cache",
    ],
)
