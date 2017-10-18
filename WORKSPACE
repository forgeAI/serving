workspace(name = "tf_serving")

local_repository(
    name = "org_tensorflow",
    path = "tensorflow",
)

# TensorFlow depends on "io_bazel_rules_closure" so we need this here.
# Needs to be kept in sync with the same target in TensorFlow's WORKSPACE file.
http_archive(
    name = "io_bazel_rules_closure",
    sha256 = "110fe68753413777944b473c25eed6368c4a0487cee23a7bac1b13cc49d3e257",
    strip_prefix = "rules_closure-4af89ef1db659eb41f110df189b67d4cf14073e1",
    urls = [
        "http://mirror.bazel.build/github.com/bazelbuild/rules_closure/archive/4af89ef1db659eb41f110df189b67d4cf14073e1.tar.gz",
        "https://github.com/bazelbuild/rules_closure/archive/4af89ef1db659eb41f110df189b67d4cf14073e1.tar.gz",  # 2017-08-28
    ],
)

# patched_http_archive(
#     name = "protobuf_archive",
#     urls = [
#         "http://mirror.bazel.build/github.com/google/protobuf/archive/0b059a3d8a8f8aa40dde7bea55edca4ec5dfea66.tar.gz",
#         "https://github.com/google/protobuf/archive/0b059a3d8a8f8aa40dde7bea55edca4ec5dfea66.tar.gz",
#     ],
#     sha256 = "6d43b9d223ce09e5d4ce8b0060cb8a7513577a35a64c7e3dad10f0703bf3ad93",
#     strip_prefix = "protobuf-0b059a3d8a8f8aa40dde7bea55edca4ec5dfea66",
#     # TODO: remove patching when tensorflow stops linking same protos into
#     #       multiple shared libraries loaded in runtime by python.
#     #       This patch fixes a runtime crash when tensorflow is compiled
#     #       with clang -O2 on Linux (see https://github.com/tensorflow/tensorflow/issues/8394)
#     patch_file = str(Label("//third_party/protobuf:add_noinlines.patch")),
# )

# native.bind(
#     name = "protobuf",
#     actual = "@protobuf_archive//:protobuf",
# )

# Please add all new TensorFlow Serving dependencies in workspace.bzl.
load("//tensorflow_serving:workspace.bzl", "tf_serving_workspace")

tf_serving_workspace()

# Specify the minimum required bazel version.
load("@org_tensorflow//tensorflow:workspace.bzl", "check_version")

check_version("0.4.5")
