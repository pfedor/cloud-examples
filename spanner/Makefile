
CXX = g++
PROTOC = protoc
GRPC_CPP_PLUGIN = grpc_cpp_plugin
GRPC_CPP_PLUGIN_PATH ?= `which $(GRPC_CPP_PLUGIN)`

CPPFLAGS += `pkg-config --cflags protobuf grpc pubsub_client spanner_client`
LDFLAGS += `pkg-config --libs protobuf grpc++`

PROTOS_PATH = .

all: arithmetic-server arithmetic-client geometry-server geometry-processor

arithmetic-server: arithmetic-service.pb.o arithmetic-service.grpc.pb.o arithmetic-server.o
	$(CXX) $^ $(LDFLAGS) -Wl,--whole-archive -lgrpc++_reflection -Wl,--no-whole-archive -o $@

geometry-server: geometry-service.pb.o geometry-service.grpc.pb.o geometry-server.o
	$(CXX) $^ $(LDFLAGS) `pkg-config --libs pubsub_client spanner_client` -Wl,--whole-archive -lgrpc++_reflection -Wl,--no-whole-archive -o $@

geometry-processor: arithmetic-service.pb.o arithmetic-service.grpc.pb.o geometry-processor.o geometry-service.pb.o
	$(CXX) $^ $(LDFLAGS) `pkg-config --libs pubsub_client spanner_client` -o $@

arithmetic-client: arithmetic-service.pb.o arithmetic-service.grpc.pb.o arithmetic-client.o
	$(CXX) $^ $(LDFLAGS) -o $@


.PRECIOUS: %.grpc.pb.cc
%.grpc.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --grpc_out=. --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN_PATH) $<

.PRECIOUS: %.pb.cc
%.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<

clean:
	rm -f *.o *.pb.cc *.pb.h arithmetic-server arithmetic-client geometry-server geometry-processor

