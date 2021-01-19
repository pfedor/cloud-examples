
#include <chrono>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include <google/cloud/pubsub/subscriber.h>
#include <google/cloud/spanner/client.h>
#include <grpcpp/grpcpp.h>

#include "arithmetic-service.grpc.pb.h"
#include "geometry-service.pb.h"

namespace mathematics {
namespace {

namespace cloud = ::google::cloud;
namespace pubsub = ::google::cloud::pubsub;
namespace spanner = ::google::cloud::spanner;

constexpr char kSpannerInstanceId[] = "foobar-instance";
constexpr char kDatabaseId[] = "geometry";
constexpr char kComputedLengthTableName[] = "computed_length";
constexpr char kIdColumn[] = "id";
constexpr char kVersionColumn[] = "version";
constexpr char kLengthColumn[] = "length";
constexpr char kErrorDetailsColumn[] = "error_details";

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
  GeometryComputer(Arithmetic::Stub *arithmetic)
      : arithmetic_(arithmetic),
        random_(std::chrono::system_clock::now().time_since_epoch().count()) {}

  cloud::StatusOr<double> ComputeLength(
      const ScheduleLengthComputationRequest &request,
      const std::chrono::system_clock::time_point &deadline) {
    double sum = 0;
    for (const auto &n : request.coordinates()) {
      auto square = ComputeSquare(n, deadline);
      if (!square.ok()) {
        return square.status();
      }
      sum += *square;
    }

    return sqrt(sum);
  }

 private:
  bool IsRetryableError(const grpc::Status &status) const {
    switch (status.error_code()) {
      case grpc::StatusCode::UNAVAILABLE:
        return true;
      default:
        return false;
    }
  }

  cloud::StatusOr<int> ComputeSquare(
      int n, const std::chrono::system_clock::time_point &deadline) {
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
        return response.square();
      }
      if (!IsRetryableError(s)) {
        return cloud::Status(
            static_cast<cloud::StatusCode>(s.error_code()),
            s.error_message() + "; calling the arithmetic server.");
      }

      int delay_ms =
          RandomIntBetween(0.75 * next_delay_ms, 1.25 * next_delay_ms);
      auto delay = std::chrono::milliseconds(delay_ms);
      next_delay_ms *= kScaling;

      if (std::chrono::system_clock::now() + delay > deadline) {
        return cloud::Status(
            cloud::StatusCode::kDeadlineExceeded,
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

  Arithmetic::Stub *arithmetic_;  // Not owned.
  std::mutex mu_;
  std::default_random_engine random_;  // Guarded by mu_.
};

class GeometryDatabase {
 public:
  GeometryDatabase(spanner::Client client) : client_(client) {}

  cloud::Status MaybeUpdateComputedLength(
      const std::string &id, std::int64_t version,
      const cloud::StatusOr<double> &length) {
    // From spanner::Client's documentation:
    // "Instances of this class created via copy-construction
    // or copy-assignment share the underlying pool of connections.
    // Access to these copies via multiple threads is guaranteed to work.
    // Two threads operating on the same instance of this class
    // is not guaranteed to work."
    auto client = client_;

    auto commit = client.Commit([&](const spanner::Transaction &txn)
                                    -> cloud::StatusOr<spanner::Mutations> {
      const auto key = spanner::KeySet().AddKey(spanner::MakeKey(id));
      auto result = client.Read(txn, kComputedLengthTableName, std::move(key),
                                {kVersionColumn, kLengthColumn});
      std::int64_t read_version;
      absl::optional<double> read_length;
      using RowType = std::tuple<std::int64_t, absl::optional<double>>;
      int cnt = 0;
      for (const auto &row : spanner::StreamOf<RowType>(result)) {
        cnt++;
        if (cnt > 1) {
          return cloud::Status(
              cloud::StatusCode::kInternal,
              "Got multiple rows with computed_length.id " + id);
        }
        if (!row.ok()) {
          return cloud::Status(row.status().code(),
                               row.status().message() +
                                   "; reading computed_length for id " + id);
        }
        read_version = std::get<0>(*row);
        read_length = std::get<1>(*row);
      }
      if (cnt == 1) {
        // There is already a row with our id. Decide whether
        // to overwrite it:
        // 1. If all we have is an error, but there is a successfully
        // computed value in Spanner, don't overwrite it regardless
        // of versions.
        // 2. If, on the other hand, we have a value but the database
        // has an error, replace the current row with our value,
        // also regardless of versions.
        // 3. Otherwise, write the computed value iff our version
        // is newer than the one in Spanner.
        if (read_length != absl::nullopt && !length.ok()) {
          return spanner::Mutations{};
        }
        if ((read_length != absl::nullopt || !length.ok()) &&
            read_version >= version) {
          return spanner::Mutations{};
        }
      }

      absl::optional<double> length_or_null;
      absl::optional<spanner::Bytes> serialized_error_or_null;

      if (length.ok()) {
        length_or_null = *length;
      } else {
        LengthComputationErrorDetails error;
        error.set_code(static_cast<int>(length.status().code()));
        error.set_message(length.status().message());
        serialized_error_or_null.emplace(error.SerializeAsString());
      }

      return spanner::Mutations{
          spanner::MakeInsertOrUpdateMutation(
              kComputedLengthTableName,
              {kIdColumn, kVersionColumn, kLengthColumn, kErrorDetailsColumn},
              id, version, length_or_null, serialized_error_or_null),
      };
    });
    return commit.status();
  }

 private:
  const spanner::Client client_;
};

void Run() {
  // Open a client connection to the arithmetic server.
  grpc::ChannelArguments args;
  args.SetLoadBalancingPolicyName("round_robin");
  std::unique_ptr<Arithmetic::Stub> stub(Arithmetic::NewStub(
      grpc::CreateCustomChannel("127.0.0.1:50051",
                                grpc::InsecureChannelCredentials(), args)));

  GeometryComputer computer(stub.get());

  // Connect to Spanner.
  const spanner::Client spanner_client(spanner::MakeConnection(
      spanner::Database(kProjectId, kSpannerInstanceId, kDatabaseId)));
  GeometryDatabase db(spanner_client);

  // Subscribe to pubsub.
  pubsub::Subscription subscription(kProjectId, kSubscriptionId);
  pubsub::Subscriber subscriber(pubsub::MakeSubscriberConnection(subscription));
  auto session =
      subscriber.Subscribe([&](const pubsub::Message &m, pubsub::AckHandler h) {
        ScheduleLengthComputationRequest request;
        if (!request.ParseFromString(m.data())) {
          std::cerr << "Malformed message, id: " << m.message_id() << std::endl;
          return;
        }

        std::cout << "Received a length computation request with id "
                  << request.id() << std::endl;

        const auto deadline =
            std::chrono::system_clock::now() + std::chrono::minutes(1);
        const auto length = computer.ComputeLength(request, deadline);

        const auto commit_status = db.MaybeUpdateComputedLength(
            request.id(), request.version(), length);
        if (!commit_status.ok()) {
          std::cerr << "Spanner write failure: " << commit_status << std::endl;
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
