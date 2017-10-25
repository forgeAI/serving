/* Copyright 2016 Google Inc. All Rights Reserved.

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

// gRPC server implementation of
// tensorflow_serving/apis/prediction_service.proto.
//
// It bring up a standard server to serve a single TensorFlow model using
// command line flags, or multiple models via config file.
//
// ModelServer prioritizes easy invocation over flexibility,
// and thus serves a statically configured set of models. New versions of these
// models will be loaded and managed over time using the
// AvailabilityPreservingPolicy at:
//     tensorflow_serving/core/availability_preserving_policy.h.
// by AspiredVersionsManager at:
//     tensorflow_serving/core/aspired_versions_manager.h
//
// ModelServer has inter-request batching support built-in, by using the
// BatchingSession at:
//     tensorflow_serving/batching/batching_session.h
//
// To serve a single model, run with:
//     $path_to_binary/tensorflow_model_server \
//     --model_base_path=[/tmp/my_model | gs://gcs_address]
// IMPORTANT: Be sure the base path excludes the version directory. For
// example for a model at /tmp/my_model/123, where 123 is the version, the base
// path is /tmp/my_model.
//
// To specify model name (default "default"): --model_name=my_name
// To specify port (default 8500): --port=my_port
// To enable batching (default disabled): --enable_batching
// To override the default batching parameters: --batching_parameters_file

#include <unistd.h>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "google/protobuf/wrappers.pb.h"
#include "grpc++/security/server_credentials.h"
#include "grpc++/server.h"
#include "grpc++/server_builder.h"
#include "grpc++/server_context.h"
#include "grpc++/support/status.h"
#include "grpc++/support/status_code_enum.h"
#include "grpc/grpc.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/platform/env.h"
#include "tensorflow/core/platform/init_main.h"
#include "tensorflow/core/platform/protobuf.h"
#include "tensorflow/core/platform/types.h"
#include "tensorflow/core/protobuf/config.pb.h"
#include "tensorflow/core/util/command_line_flags.h"

#include "tensorflow_serving/apis/prediction_service.grpc.pb.h"
#include "tensorflow_serving/apis/prediction_service.pb.h"
#include "tensorflow_serving/servables/tensorflow/predict_impl.h"

#include "tensorflow_serving/config/model_server_config.pb.h"
#include "tensorflow_serving/core/availability_preserving_policy.h"
#include "tensorflow_serving/model_servers/model_platform_types.h"
#include "tensorflow_serving/model_servers/platform_config_util.h"
#include "tensorflow_serving/model_servers/server_core.h"

namespace grpc {
class ServerCompletionQueue;
}  // namespace grpc

namespace tf = tensorflow;
using tf::serving::AspiredVersionsManager;
using tf::serving::AspiredVersionPolicy;
using tf::serving::AvailabilityPreservingPolicy;
using tf::serving::BatchingParameters;
using tf::serving::EventBus;
using tf::serving::FileSystemStoragePathSourceConfig;
using tf::serving::ModelServerConfig;
using tf::serving::ServableState;
using tf::serving::ServerCore;
using tf::serving::SessionBundleConfig;
using tf::serving::TensorflowPredictor;
using tf::serving::UniquePtrWithDeps;
using tf::string;

using tf::serving::PredictRequest;
using tf::serving::PredictResponse;
using tf::serving::PredictionService;

namespace {

tf::Status ParseProtoTextFile(const string &file, google::protobuf::Message *message) {
    std::unique_ptr<tf::ReadOnlyMemoryRegion> file_data;
    TF_RETURN_IF_ERROR(tf::Env::Default()->NewReadOnlyMemoryRegionFromFile(file, &file_data));
    string file_data_str(static_cast<const char *>(file_data->data()), file_data->length());

    if (tf::protobuf::TextFormat::ParseFromString(file_data_str, message)) {
        return tf::Status::OK();
    } else {
        return tf::errors::InvalidArgument("Invalid protobuf file: '", file, "'");
    }
}

template <typename ProtoType>
ProtoType ReadProtoFromFile(const string &file) {
    ProtoType proto;
    TF_CHECK_OK(ParseProtoTextFile(file, &proto));
    return proto;
}

ModelServerConfig BuildSingleModelConfig(const string &model_name, const string &model_base_path) {
    LOG(INFO) << "Building single TensorFlow model file config: "
              << " model_name: " << model_name << " model_base_path: " << model_base_path;
    ModelServerConfig config;
    tf::serving::ModelConfig *single_model = config.mutable_model_config_list()->add_config();
    single_model->set_name(model_name);
    single_model->set_base_path(model_base_path);
    single_model->set_model_platform(tf::serving::kTensorFlowModelPlatform);
    return config;
}

int DeadlineToTimeoutMillis(const gpr_timespec deadline) {
    return gpr_time_to_millis(gpr_time_sub(gpr_convert_clock_type(deadline, GPR_CLOCK_MONOTONIC),
                                           gpr_now(GPR_CLOCK_MONOTONIC)));
}

grpc::Status ToGRPCStatus(const tf::Status &status) {
    const int kErrorMessageLimit = 1024;
    string error_message;
    if (status.error_message().length() > kErrorMessageLimit) {
        error_message = status.error_message().substr(0, kErrorMessageLimit) + "...TRUNCATED";
    } else {
        error_message = status.error_message();
    }
    return grpc::Status(static_cast<grpc::StatusCode>(status.code()), error_message);
}

class PredictionServiceImpl : public PredictionService::Service {
   public:
    explicit PredictionServiceImpl(std::unique_ptr<ServerCore> core, bool use_saved_model)
        : core_(std::move(core)),
          predictor_(new TensorflowPredictor(use_saved_model)),
          use_saved_model_(use_saved_model) {}

    grpc::Status Predict(grpc::ServerContext *context, const PredictRequest *request,
                         PredictResponse *response) override {
        tf::RunOptions run_options = tf::RunOptions();
        // By default, this is infinite which is the same default as
        // RunOptions.
        run_options.set_timeout_in_ms(DeadlineToTimeoutMillis(context->raw_deadline()));
        const grpc::Status status =
            ToGRPCStatus(predictor_->Predict(run_options, core_.get(), *request, response));
        if (!status.ok()) {
            VLOG(1) << "Predict failed: " << status.error_message();
        }
        return status;
    }

   private:
    std::unique_ptr<ServerCore> core_;
    std::unique_ptr<TensorflowPredictor> predictor_;
    bool use_saved_model_;
};

class RESTService {
   public:
    explicit RESTService(std::unique_ptr<ServerCore> core, bool use_saved_model)
        : PredictionServiceImpl(std::move(core), use_saved_model) {}

    grpc::Status Predict(grpc::ServerContext *context, const PredictRequest *request,
                         PredictResponse *response) override {
        return PredictionServiceImpl::Predict(context, request, response);
    }

    /**
     * Builds the PredictRequest object from string and calls Predict().
     */
    void infer(cosnt std::string &doc) {}
};

void RunServer(int port, std::unique_ptr<ServerCore> core, bool use_saved_model) {
    // "0.0.0.0" is the way to listen on localhost in gRPC.
    const string server_address = "0.0.0.0:" + std::to_string(port);
    BrandonPredictionService service(std::move(core), use_saved_model);
    grpc::ServerBuilder builder;
    std::shared_ptr<grpc::ServerCredentials> creds = grpc::InsecureServerCredentials();
    builder.AddListeningPort(server_address, creds);
    builder.RegisterService(&service);
    builder.SetMaxMessageSize(tf::kint32max);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    LOG(INFO) << "Running ModelServer at " << server_address << " ...";
    server->Wait();
}

}  // namespace

int main(int argc, char **argv) {
    string model_base_path;
    string batching_parameters_file;
    bool enable_batching = false;
    const bool use_saved_model = true;
    tf::int32 port = 8500;
    string model_name = "default";
    tf::int32 file_system_poll_wait_seconds = 1;
    string model_config_file;
    // Tensorflow session parallelism of zero means that both inter and intra op
    // thread pools will be auto configured.
    tf::int64 tensorflow_session_parallelism = 0;

    std::vector<tf::Flag> flag_list = {
        tf::Flag("port", &port, "port to listen on"),
        tf::Flag("enable_batching", &enable_batching, "enable batching"),
        tf::Flag("batching_parameters_file", &batching_parameters_file,
                 "If non-empty, read an ascii BatchingParameters "
                 "protobuf from the supplied file name and use the "
                 "contained values instead of the defaults."),
        tf::Flag("model_config_file", &model_config_file,
                 "If non-empty, read an ascii ModelServerConfig "
                 "protobuf from the supplied file name, and serve the "
                 "models in that file. This config file can be used to "
                 "specify multiple models to serve and other advanced "
                 "parameters including non-default version policy. (If "
                 "used, --model_name, --model_base_path are ignored.)"),
        tf::Flag("model_name", &model_name,
                 "name of model (ignored "
                 "if --model_config_file flag is set"),
        tf::Flag("model_base_path", &model_base_path,
                 "path to export (ignored if --model_config_file flag "
                 "is set, otherwise required)"),
        tf::Flag("file_system_poll_wait_seconds", &file_system_poll_wait_seconds,
                 "interval in seconds between each poll of the file "
                 "system for new model version"),
        tf::Flag("tensorflow_session_parallelism", &tensorflow_session_parallelism,
                 "Number of threads to use for running a "
                 "Tensorflow session. Auto-configured by default.")};

    string usage = tf::Flags::Usage(argv[0], flag_list);
    const bool parse_result = tf::Flags::Parse(&argc, argv, flag_list);
    if (!parse_result || (model_base_path.empty() && model_config_file.empty())) {
        std::cout << usage;
        return -1;
    }
    tf::port::InitMain(argv[0], &argc, &argv);
    if (argc != 1) {
        std::cout << "unknown argument: " << argv[1] << "\n" << usage;
    }

    // For ServerCore Options, we leave servable_state_monitor_creator unspecified
    // so the default servable_state_monitor_creator will be used.
    ServerCore::Options options;

    // model server config
    if (model_config_file.empty()) {
        options.model_server_config = BuildSingleModelConfig(model_name, model_base_path);
    } else {
        options.model_server_config = ReadProtoFromFile<ModelServerConfig>(model_config_file);
    }

    SessionBundleConfig session_bundle_config;
    // Batching config
    if (enable_batching) {
        BatchingParameters *batching_parameters =
            session_bundle_config.mutable_batching_parameters();
        if (batching_parameters_file.empty()) {
            batching_parameters->mutable_thread_pool_name()->set_value(
                "model_server_batch_threads");
        } else {
            *batching_parameters = ReadProtoFromFile<BatchingParameters>(batching_parameters_file);
        }
    } else if (!batching_parameters_file.empty()) {
        LOG(FATAL)  // Crash ok
            << "You supplied --batching_parameters_file without --enable_batching";
    }

    session_bundle_config.mutable_session_config()->set_intra_op_parallelism_threads(
        tensorflow_session_parallelism);
    session_bundle_config.mutable_session_config()->set_inter_op_parallelism_threads(
        tensorflow_session_parallelism);
    options.platform_config_map =
        CreateTensorFlowPlatformConfigMap(session_bundle_config, use_saved_model);

    options.aspired_version_policy =
        std::unique_ptr<AspiredVersionPolicy>(new AvailabilityPreservingPolicy);
    options.file_system_poll_wait_seconds = file_system_poll_wait_seconds;

    std::unique_ptr<ServerCore> core;
    TF_CHECK_OK(ServerCore::Create(std::move(options), &core));
    RunServer(port, std::move(core), use_saved_model);

    return 0;
}
