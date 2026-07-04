# Brain OS — own host build. Self-contained: touches only this module + shared
# read-only headers/registry data. Does NOT depend on sim/Makefile.
#
#   make test    build + run the headless acceptance test (CI gate)
#   make os_test build only
#   make clean

CXX ?= g++
INCLUDES = -I. -I../../include -I../../games/_sdk -I../../games/demo -I../../games/racing
CXXFLAGS = -std=c++14 -O2 -Wall -Wextra -DCONSOLE_OS_HOST $(INCLUDES)

# console::THEMES[] lives in the shared header set; compiled in (read-only), not modified.
THEMES_SRC = ../../include/console/themes.cpp

SRC = test_os.cpp brain_os.cpp builtin_games.cpp $(THEMES_SRC)
HDR = $(wildcard *.h) $(wildcard ../../include/console/*.h) \
      $(wildcard ../../games/_sdk/*.h) ../../games/demo/demo.h ../../games/racing/racing.h

BIN = os_test

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

.PHONY: test clean
test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
