
#include <cmath>
#include <iostream>
#include <memory>
#include <thread>

#include <google/cloud/pubsub/publisher.h>
#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>

#include "geometry-service.grpc.pb.h"

namespace mathematics {
namespace {

namespace pubsub = ::google::cloud::pubsub;

constexpr char kProjectId[] = "plum-butter-123";
constexpr char kTopicId[] = "foobar-topic";

class GeometryServiceImpl final : public Geometry::Service {
 public:
  GeometryServiceImpl(std::shared_ptr<pubsub::PublisherConnection> pubsub_conn)
      : publisher_(pubsub_conn) {}

  grpc::Status ScheduleLengthComputation(
      grpc::ServerContext* context,
      const ScheduleLengthComputationRequest* request,
      ScheduleLengthComputationResponse* response) override {
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

 private:
  const pubsub::Publisher publisher_;
};

void RunServer() {
  // Connect to pubsub for publishing.
  std::shared_ptr<pubsub::PublisherConnection> pubsub_conn(
      pubsub::MakePublisherConnection(pubsub::Topic(kProjectId, kTopicId), {}));

  // Create the service implementation and start the server.
  std::string server_address("127.0.0.20:40123");
  GeometryServiceImpl service(pubsub_conn);
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
