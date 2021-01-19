
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <grpc++/security/server_credentials.h>
#include <grpc++/server.h>
#include <grpc++/server_builder.h>
#include <grpc++/server_context.h>
#include <grpc/grpc.h>

#include "arithmetic-service.grpc.pb.h"

namespace mathematics {
namespace {

using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::grpc::ServerContext;
using ::grpc::Status;
using ::grpc::StatusCode;

class ArithmeticServiceImpl final : public Arithmetic::Service {
 public:
  ArithmeticServiceImpl() {}

  Status ComputeSquare(ServerContext *context,
                       const ComputeSquareRequest *request,
                       ComputeSquareResponse *response) override {
    std::cout << "ComputeSquare; " << request->ShortDebugString() << std::endl;
    if (request->number() < 0 || request->number() > 1000) {
      std::stringstream ss;
      ss << "request.number " << request->number()
         << " is outside the valid range 0 .. 1000";
      return Status(StatusCode::INVALID_ARGUMENT, ss.str());
    }

    response->set_square(request->number() * request->number());

    return Status::OK;
  }

  Status ComputeCube(ServerContext *context, const ComputeCubeRequest *request,
                     ComputeCubeResponse *response) override {
    int n = request->number();
    if (n < 0 || n > 1000) {
      std::stringstream ss;
      ss << "request.number " << n << " is outside the valid range 0 .. 1000";
      return Status(StatusCode::INVALID_ARGUMENT, ss.str());
    }
    response->set_cube(n * n * n);

    return Status::OK;
  }
};

void RunServer() {
  std::string server_address("127.0.0.1:50051");
  ArithmeticServiceImpl service;
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);
  std::unique_ptr<Server> server(builder.BuildAndStart());
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
