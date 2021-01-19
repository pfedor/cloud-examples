CXX = g++
PROTOC = protoc

CPPFLAGS += `pkg-config --cflags protobuf`
LDFLAGS += `pkg-config --libs protobuf`

PROTOS_PATH = .

all: animals

animals: animals.pb.o animals.o
	$(CXX) $^ $(LDFLAGS) -o $@

.PRECIOUS: %.pb.cc
%.pb.cc: %.proto
	$(PROTOC) -I $(PROTOS_PATH) --cpp_out=. $<

clean:
	rm -f *.o *.pb.cc *.pb.h animals
