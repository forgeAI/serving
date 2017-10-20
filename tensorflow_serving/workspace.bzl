# TensorFlow Serving external dependencies that can be loaded in WORKSPACE
# files.

load('@org_tensorflow//tensorflow:workspace.bzl', 'tf_workspace')

# All TensorFlow Serving external dependencies.
# workspace_dir is the absolute path to the TensorFlow Serving repo. If linked
# as a submodule, it'll likely be '__workspace_dir__ + "/serving"'
def tf_serving_workspace():
    tf_workspace(path_prefix = "", tf_repo_name = "org_tensorflow")

    # native.http_archive(
    #     name = "protobuf_archive",
    #     urls = [
    #         "http://mirror.bazel.build/github.com/google/protobuf/archive/0b059a3d8a8f8aa40dde7bea55edca4ec5dfea66.tar.gz",
    #         "https://github.com/google/protobuf/archive/0b059a3d8a8f8aa40dde7bea55edca4ec5dfea66.tar.gz",
    #     ],
    #     sha256 = "6d43b9d223ce09e5d4ce8b0060cb8a7513577a35a64c7e3dad10f0703bf3ad93",
    #     strip_prefix = "protobuf-0b059a3d8a8f8aa40dde7bea55edca4ec5dfea66",
    # )

    # native.bind(
    #     name = "protobuf",
    #     actual = "@protobuf_archive//:protobuf",
    # )

    # ===== gRPC dependencies =====
    native.bind(
        name = "libssl",
        actual = "@boringssl//:ssl",
    )

    native.bind(
        name = "zlib",
        actual = "@zlib_archive//:zlib",
    )
