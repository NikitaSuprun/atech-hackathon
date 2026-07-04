CXX ?= g++
CXXFLAGS = -std=c++14 -O2 -Wall -I../modules/pong_screen

pong_sim: main.cpp ../modules/pong_screen/pong_engine.cpp ../modules/pong_screen/pong_engine.h ../modules/pong_screen/pong_frame.h ../modules/pong_screen/pong_shared.h ../modules/pong_screen/pong_proto.h ../modules/pong_screen/pong_config.h ../modules/pong_screen/net_config.h
	$(CXX) $(CXXFLAGS) main.cpp ../modules/pong_screen/pong_engine.cpp -o pong_sim

.PHONY: clean test
test: pong_sim
	./pong_sim --selftest

clean:
	rm -f pong_sim
