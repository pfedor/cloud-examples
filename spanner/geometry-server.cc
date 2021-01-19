
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

#include <google/cloud/pubsub/publisher.h>
#include <google/cloud/spanner/client.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "geometry-service.grpc.pb.h"

namespace mathematics {
namespace {

namespace cloud = ::google::cloud;
namespace pubsub = ::google::cloud::pubsub;
namespace spanner = ::google::cloud::spanner;

constexpr char kProjectId[] = "plum-butter-123";
constexpr char kTopicId[] = "foobar-topic";
constexpr char kSpannerInstanceId[] = "foobar-instance";
constexpr char kDatabaseId[] = "geometry";

class GeometryServiceImpl final : public Geometry::Service {
 public:
  GeometryServiceImpl(std::shared_ptr<pubsub::PublisherConnection> pubsub_conn,
                      spanner::Client spanner_client)
      : publisher_(pubsub_conn), spanner_client_(spanner_client) {}

  grpc::Status ScheduleLengthComputation(
      grpc::ServerContext *context,
      const ScheduleLengthComputationRequest *request,
      ScheduleLengthComputationResponse *response) {
    // from pubsub::Publisher's documentation:
    // "Instances of this class created via copy-construction or copy-assignment
    // share the underlying pool of connections. Access to these copies via
    // multiple threads is guaranteed to work. Two threads operating on the same
    // instance of this class is not guaranteed to work."
    auto publisher = publisher_;
    auto message_id = publisher
                          .Publish(pubsub::MessageBuilder()
                                       .SetData(request->SerializeAsString())
                                       .Build())
                          .get();
    if (!message_id.ok()) {
      return grpc::Status(
          static_cast<grpc::StatusCode>(message_id.status().code()),
          message_id.status().message() +
              "; publishing a length computation request to pubsub.");
    }
    return grpc::Status::OK;
  }

  grpc::Status LookupLength(grpc::ServerContext *context,
                            const LookupLengthRequest *request,
                            LookupLengthResponse *response) {
    // From spanner::Client's documentation:
    // "Instances of this class created via copy-construction
    // or copy-assignment share the underlying pool of connections.
    // Access to these copies via multiple threads is guaranteed to work.
    // Two threads operating on the same instance of this class
    // is not guaranteed to work."
    auto spanner_client = spanner_client_;

    spanner::SqlStatement sql(
        "SELECT version, length, error_details FROM computed_length "
        "WHERE id = @id",
        {{"id", spanner::Value(request->id())}});
    auto result = spanner_client.ExecuteQuery(std::move(sql));

    std::int64_t version;
    absl::optional<double> length;
    absl::optional<spanner::Bytes> error_details;

    using RowType = std::tuple<std::int64_t, absl::optional<double>,
                               absl::optional<spanner::Bytes>>;
    int cnt = 0;
    for (const auto &row : spanner::StreamOf<RowType>(result)) {
      cnt++;
      if (cnt > 1) {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "Got multiple rows with computed_length.id " + request->id());
      }
      if (!row.ok()) {
        return grpc::Status(static_cast<grpc::StatusCode>(row.status().code()),
                            row.status().message() +
                                "; reading computed_length for id " +
                                request->id());
      }
      version = std::get<0>(*row);
      length = std::get<1>(*row);
      error_details = std::get<2>(*row);
    }
    if (cnt == 0) {
      return grpc::Status(grpc::StatusCode::NOT_FOUND,
                          "Computed length not found, id: " + request->id());
    }
    if (length == absl::nullopt && error_details == absl::nullopt) {
      return grpc::Status(
          grpc::StatusCode::DATA_LOSS,
          "length and error_details are both NULL for computed_length.id " +
              request->id());
    }
    if (length != absl::nullopt && error_details != absl::nullopt) {
      return grpc::Status(
          grpc::StatusCode::DATA_LOSS,
          "length and error_details are both not NULL for computed_length.id " +
              request->id());
    }
    response->set_version(version);
    if (length != absl::nullopt) {
      response->set_length(*length);
      return grpc::Status::OK;
    }

    LengthComputationErrorDetails error;
    if (!error.ParseFromString(error_details->get<std::string>())) {
      return grpc::Status(
          grpc::StatusCode::DATA_LOSS,
          "Corrupted data in computed_length.error_details for id " +
              request->id());
    }

    *response->mutable_error_details() = error;
    return grpc::Status::OK;
  }

 private:
  const pubsub::Publisher publisher_;
  const spanner::Client spanner_client_;
};

void RunServer() {
  // Connect to pubsub for publishing.
  std::shared_ptr<pubsub::PublisherConnection> pubsub_conn(
      pubsub::MakePublisherConnection(pubsub::Topic(kProjectId, kTopicId), {}));

  // Connect to Spanner.
  const spanner::Database db(kProjectId, kSpannerInstanceId, kDatabaseId);
  const spanner::Client spanner_client(spanner::MakeConnection(db));

  // Create the service implementation and start the server.
  std::string server_address("127.0.0.20:40123");
  GeometryServiceImpl service(pubsub_conn, spanner_client);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    std::cerr << "Failed to start the server at " << server_address
              << std::endl;
    exit(-1);
  }
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

}  // namespace
}  // namespace mathematics

int main() { mathematics::RunServer(); }
