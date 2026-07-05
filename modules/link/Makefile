# Link module — host build for the transport adapter + loopback.
#
#   make test    build + run the adapter round-trip test (CI gate)
#   make clean
#
# The concrete serial/ESP-NOW links are Arduino-only (firmware); this host test
# covers the pure pieces: LinkFrameSink (encode -> PongLink) and LinkLoopback
# (in-memory COBS transport), which the E2E test and sim reuse.

CXX ?= g++
INCLUDES = -I. -I../../include -I../console_os -I../pong_screen
CXXFLAGS = -std=c++14 -O2 -Wall -Wextra $(INCLUDES)

SRC = test_link.cpp
HDR = $(wildcard *.h) ../console_os/frame_sink.h ../pong_screen/pong_link.h \
      $(wildcard ../../include/console/*.h)

BIN = link_test

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

.PHONY: test clean
test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
