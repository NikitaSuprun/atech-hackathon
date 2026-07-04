CXX ?= g++
ROOT = ../..
CXXFLAGS = -std=c++14 -O2 -Wall -Wextra \
           -I$(ROOT)/include -I$(ROOT)/modules/pong_screen \
           -I$(ROOT)/modules/screen_render -I$(ROOT)/modules/link

DEPS = light_engine.h screen_render.h \
       $(ROOT)/include/console/frame_proto.h $(ROOT)/include/console/host_proto.h \
       $(ROOT)/modules/link/link_frame.h $(ROOT)/modules/link/link_espnow.h \
       $(ROOT)/modules/pong_screen/compositor.h

test_console: test_console.cpp $(ROOT)/modules/pong_screen/compositor.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) test_console.cpp $(ROOT)/modules/pong_screen/compositor.cpp -o test_console

.PHONY: test clean
test: test_console
	./test_console

clean:
	rm -f test_console
