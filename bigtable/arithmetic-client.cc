
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "arithmetic-service.grpc.pb.h"

using ::grpc::Channel;
using ::grpc::ChannelArguments;
using ::grpc::ClientContext;
using ::grpc::Status;
using ::mathematics::Arithmetic;
using ::mathematics::ComputeSquareRequest;
using ::mathematics::ComputeSquareResponse;

int main() {
  ChannelArguments args;
  args.SetLoadBalancingPolicyName("round_robin");
  std::unique_ptr<Arithmetic::Stub> stub(Arithmetic::NewStub(
      grpc::CreateCustomChannel("127.0.0.1:50051",
                                grpc::InsecureChannelCredentials(), args)));

  for (int i = 0; i < 10; i++) {
    ComputeSquareRequest request;
    request.set_number(5 + i);
    ComputeSquareResponse response;

    ClientContext context;
    Status status = stub->ComputeSquare(&context, request, &response);
    if (status.ok()) {
      std::cout << response.DebugString();
    } else {
      std::cerr << "ComputeSquare failed: " << status.error_code() << " "
                << status.error_message() << std::endl;
    }
    std::cin.ignore();
  }
}
