
#include <google/cloud/bigtable/table.h>
#include <google/cloud/pubsub/subscriber.h>
#include <grpcpp/grpcpp.h>
#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "arithmetic-service.grpc.pb.h"
#include "geometry-service.pb.h"

namespace mathematics {
namespace {

namespace cbt = ::google::cloud::bigtable;
namespace cloud = ::google::cloud;
namespace pubsub = ::google::cloud::pubsub;

constexpr char kBigtableInstanceId[] = "foobar-instance";
constexpr char kBigtableTableId[] = "foobar-table";
constexpr char kLengthResultColumnFamily[] = "length-result";
constexpr char kProjectId[] = "plum-butter-123";
constexpr char kSubscriptionId[] = "foobar-subscription";

std::string FormatDuration(std::chrono::system_clock::duration t) {
  std::stringstream ss;
  using namespace std::chrono;
  int time_ms = duration_cast<milliseconds>(t).count();
  int s = time_ms / 1000;
  int m = s / 60;
  int h = m / 60;
  if (h > 0) {
    ss << h << "h ";
  }
  if (m > 0) {
    if (h > 0 && m - h * 60 < 10) {
      ss << "0";
    }
    ss << m - h * 60 << "min ";
  }
  if (m > 0 && s - m * 60 < 10) {
    ss << "0";
  }
  ss << s - m * 60 << "." << (time_ms / 100) % 10 << "s";
  return ss.str();
}

class GeometryComputer {
 public:
  GeometryComputer(Arithmetic::Stub* arithmetic)
      : arithmetic_(arithmetic),
        random_(std::chrono::system_clock::now().time_since_epoch().count()) {}

  cloud::Status ComputeLength(
      const ScheduleLengthComputationRequest& request,
      const std::chrono::system_clock::time_point& deadline, double* length) {
    double sum = 0;
    for (const auto& n : request.coordinates()) {
      int square;
      grpc::Status s = ComputeSquare(n, deadline, &square);
      if (!s.ok()) {
        return cloud::Status(
            static_cast<cloud::StatusCode>(s.error_code()),
            s.error_message() + "; calling the arithmetic server.");
      }
      sum += square;
    }

    *length = sqrt(sum);

    return cloud::Status();
  }

 private:
  bool IsRetryableError(const grpc::Status& status) const {
    switch (status.error_code()) {
      case grpc::StatusCode::UNAVAILABLE:
        return true;
      default:
        return false;
    }
  }

  grpc::Status ComputeSquare(
      int n, const std::chrono::system_clock::time_point& deadline,
      int* square) {
    ComputeSquareRequest request;
    request.set_number(n);

    const auto kInitialDelayMs = 200;
    const double kScaling = 1.5;

    auto next_delay_ms = kInitialDelayMs;

    for (;;) {
      ComputeSquareResponse response;
      grpc::ClientContext ctx;
      grpc::Status s = arithmetic_->ComputeSquare(&ctx, request, &response);
      if (s.ok()) {
        *square = response.square();
        return grpc::Status::OK;
      }
      if (!IsRetryableError(s)) {
        return s;
      }

      int delay_ms =
          RandomIntBetween(0.75 * next_delay_ms, 1.25 * next_delay_ms);
      auto delay = std::chrono::milliseconds(delay_ms);
      next_delay_ms *= kScaling;

      if (std::chrono::system_clock::now() + delay > deadline) {
        return grpc::Status(
            grpc::StatusCode::DEADLINE_EXCEEDED,
            "Deadline exceeded calling Arithmetic.ComputeSquare");
      }

      std::cerr << "ComputeSquare request failed: " << s.error_message()
                << "; will retry after " << FormatDuration(delay) << std::endl;

      std::this_thread::sleep_for(delay);
    }
  }

  int RandomIntBetween(int n1, int n2) {
    std::uniform_int_distribution<int> dist(n1, n2);

    std::lock_guard<std::mutex> lock(mu_);
    return dist(random_);
  }

  Arithmetic::Stub* arithmetic_;  // Not owned.
  std::mutex mu_;
  std::default_random_engine random_;  // Guarded by mu_.
};

void Run() {
  pubsub::Subscription subscription(kProjectId, kSubscriptionId);
  pubsub::Subscriber subscriber(pubsub::MakeSubscriberConnection(subscription));

  grpc::ChannelArguments args;
  args.SetLoadBalancingPolicyName("round_robin");
  std::unique_ptr<Arithmetic::Stub> stub(Arithmetic::NewStub(
      grpc::CreateCustomChannel("127.0.0.1:50051",
                                grpc::InsecureChannelCredentials(), args)));
  GeometryComputer computer(stub.get());

  const cbt::Table table(
      cbt::CreateDefaultDataClient(kProjectId, kBigtableInstanceId,
                                   cbt::ClientOptions()),
      kBigtableTableId, cbt::AlwaysRetryMutationPolicy());

  auto session =
      subscriber.Subscribe([&](const pubsub::Message& m, pubsub::AckHandler h) {
        ScheduleLengthComputationRequest request;
        if (!request.ParseFromString(m.data())) {
          std::cerr << "Malformed message, id: " << m.message_id() << std::endl;
          return;
        }

        std::cout << "Received a length computation request with id "
                  << request.id() << std::endl;

        double length;
        const auto deadline =
            std::chrono::system_clock::now() + std::chrono::minutes(1);
        auto status = computer.ComputeLength(request, deadline, &length);
        if (!status.ok()) {
          std::cerr << "Length computation failure: " << status << std::endl;
          return;
        }

        // Table is not thread-safe, so we need to make a copy before using it.
        // (Since subscriber callbacks may run in parallel threads.)
        cbt::Table table_copy = table;

        LengthComputationResult lcr;
        lcr.set_length(length);

        cbt::SingleRowMutation mutation(request.id());
        mutation.emplace_back(cbt::SetCell(kLengthResultColumnFamily, "",
                                           lcr.SerializeAsString()));
        status = table_copy.Apply(std::move(mutation));
        if (!status.ok()) {
          std::cerr << "Bigtable write failure: " << status << std::endl;
          return;
        }

        std::move(h).ack();
      });

  auto status = session.get();
  std::cerr << "Subscription interrupted: " << status << std::endl;
}

}  // namespace
}  // namespace mathematics

int main() { mathematics::Run(); }
