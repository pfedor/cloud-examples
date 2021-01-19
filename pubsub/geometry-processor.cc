
#include <google/cloud/pubsub/subscriber.h>
#include <grpcpp/grpcpp.h>
#include <iostream>

#include "arithmetic-service.grpc.pb.h"
#include "geometry-service.pb.h"

namespace mathematics {
namespace {

namespace cloud = ::google::cloud;
namespace pubsub = ::google::cloud::pubsub;

constexpr char kProjectId[] = "plum-butter-123";
constexpr char kSubscriptionId[] = "foobar-subscription";

class GeometryComputer {
 public:
  GeometryComputer(Arithmetic::Stub* arithmetic) : arithmetic_(arithmetic) {}

  grpc::Status ComputeLength(const ScheduleLengthComputationRequest& request,
                             double* length) {
    double sum = 0;
    for (const auto& n : request.coordinates()) {
      ComputeSquareRequest square_req;
      square_req.set_number(n);
      ComputeSquareResponse square_resp;
      grpc::ClientContext ctx;
      grpc::Status s =
          arithmetic_->ComputeSquare(&ctx, square_req, &square_resp);
      if (!s.ok()) {
        return grpc::Status(
            s.error_code(),
            s.error_message() + "; calling the arithmetic server.");
      }
      sum += square_resp.square();
    }

    *length = sqrt(sum);

    return grpc::Status::OK;
  }

 private:
  Arithmetic::Stub* arithmetic_;  // Not owned.
};

void Run() {
  pubsub::Subscription subscription(kProjectId, kSubscriptionId);
  pubsub::Subscriber subscriber(pubsub::MakeSubscriberConnection(subscription));

  std::unique_ptr<Arithmetic::Stub> stub(
      Arithmetic::NewStub(grpc::CreateChannel(
          "127.0.0.1:50051", grpc::InsecureChannelCredentials())));
  GeometryComputer computer(stub.get());

  auto session =
      subscriber.Subscribe([&](const pubsub::Message& m, pubsub::AckHandler h) {
        std::cout << "Received message " << m << std::endl;
        ScheduleLengthComputationRequest request;
        if (!request.ParseFromString(m.data())) {
          std::cerr << "Malformed message, id: " << m.message_id() << std::endl;
          return;
        }
        std::cout << "Length computation request:\n"
                  << request.DebugString() << std::endl;

        double length;
        auto status = computer.ComputeLength(request, &length);
        if (!status.ok()) {
          std::cerr << "Length computation failure: " << status.error_message();
          return;
        }

        std::cout << "length: " << length << std::endl;

        // ...

        std::move(h).ack();
      });

  auto status = session.get();
  std::cerr << "Subscription interrupted: " << status << std::endl;
}

}  // namespace
}  // namespace mathematics

int main() { mathematics::Run(); }
