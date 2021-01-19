
#include <cmath>
#include <iostream>
#include <memory>
#include <grpc/grpc.h>
#include <grpcpp/grpcpp.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc++/security/server_credentials.h>

#include "arithmetic-service.grpc.pb.h"
#include "geometry-service.grpc.pb.h"

namespace mathematics {
namespace {

using ::grpc::Channel;
using ::grpc::ChannelArguments;
using ::grpc::ClientContext;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

class GeometryServiceImpl final : public Geometry::Service {
 public:
  GeometryServiceImpl(Arithmetic::Stub* arithmetic)
      : arithmetic_(arithmetic) {}

  Status ComputeLength(ServerContext* context,
                       const ComputeLengthRequest* request,
                       ComputeLengthResponse* response) override {
    double sum = 0;
    for (const auto& n : request->coordinates()) {
      ComputeSquareRequest square_req;
      square_req.set_number(n);
      ComputeSquareResponse square_resp;
      ClientContext ctx;
      Status s = arithmetic_->ComputeSquare(&ctx, square_req, &square_resp);
      if (!s.ok()) {
        return Status(s.error_code(),
                      s.error_message() + "; calling the arithmetic server.");
      }
      sum += square_resp.square();
    }
    response->set_length(sqrt(sum));

    return Status::OK;
  }
  
 private:
  Arithmetic::Stub* arithmetic_;  // Not owned.
};

void RunServer() {
  // Connect to the arithmetic server.
  ChannelArguments args;
  args.SetLoadBalancingPolicyName("round_robin");
  std::unique_ptr<Arithmetic::Stub> stub(Arithmetic::NewStub(
      grpc::CreateCustomChannel("127.0.0.1:50051",
                                grpc::InsecureChannelCredentials(),
                                args)));

  // Create the service implementation and start the server.
  std::string server_address("127.0.0.20:40123");
  GeometryServiceImpl service(stub.get());
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    std::cerr << "Failed to start the server at " << server_address << std::endl;
    exit(-1);
  }
  std::cout << "Server listening on " << server_address << std::endl;
  server->Wait();
}

}  // namespace
}  // namespace mathematics

int main() {
  mathematics::RunServer();
}
