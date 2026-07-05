# End-to-end integration test — composes the real Brain OS + real screen board
# over an in-memory link and asserts the screen reproduces the OS framebuffer.
# Self-contained host build (no hardware). CI gate.
#
#   make test    build + run
#   make clean

CXX ?= g++

GAMES    = eggcatch snake pong racing flappy doodlejump invaders jukebox ambient demo
GAME_INC = $(addprefix -I../../games/,$(GAMES))

INCLUDES = -I. -I../../include -I../../games/_sdk $(GAME_INC) \
           -I../console_os -I../screen_render -I../link -I../pong_screen
CXXFLAGS = -std=c++14 -O2 -Wall -Wextra -DCONSOLE_OS_HOST $(INCLUDES)

SRC = test_e2e.cpp \
      ../console_os/brain_os.cpp \
      ../console_os/builtin_games.cpp \
      ../pong_screen/compositor.cpp \
      ../../include/console/themes.cpp

HDR = $(wildcard ../console_os/*.h) $(wildcard ../screen_render/*.h) \
      $(wildcard ../link/*.h) $(wildcard ../../include/console/*.h) \
      $(wildcard ../../games/_sdk/*.h) $(wildcard ../../games/*/*.h) \
      ../pong_screen/pong_link.h ../pong_screen/compositor.h

BIN = e2e_test

$(BIN): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(BIN)

.PHONY: test clean
test: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
