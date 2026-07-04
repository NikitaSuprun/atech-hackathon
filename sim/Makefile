CXX ?= g++
CXXFLAGS = -std=c++14 -O2 -Wall -I../modules/pong_screen

pong_sim: main.cpp ../modules/pong_screen/pong_engine.cpp ../modules/pong_screen/pong_engine.h ../modules/pong_screen/pong_frame.h ../modules/pong_screen/pong_shared.h ../modules/pong_screen/pong_proto.h ../modules/pong_screen/pong_config.h ../modules/pong_screen/net_config.h
	$(CXX) $(CXXFLAGS) main.cpp ../modules/pong_screen/pong_engine.cpp -o pong_sim

.PHONY: clean test gamebin gamerun gamedump gameselftest
test: pong_sim
	./pong_sim --selftest

# ---- generic console::Game host: build/run/dump ANY game, zero hardware ----
GAME      ?= demo
GAME_DIR   = ../games/$(GAME)
HOST_BIN   = gamehost_$(GAME)
HOST_FLAGS = -std=c++14 -O2 -Wall -I../include -I../games/_sdk -I$(GAME_DIR)
HOST_DEPS  = game_main.cpp stub_theme.h $(wildcard ../games/_sdk/*.h) \
             $(GAME_DIR)/$(GAME).cpp $(wildcard $(GAME_DIR)/*.h) \
             $(wildcard ../include/console/*.h)

$(HOST_BIN): $(HOST_DEPS)
	$(CXX) $(HOST_FLAGS) game_main.cpp $(GAME_DIR)/$(GAME).cpp -o $(HOST_BIN)

gamebin: $(HOST_BIN)
gamerun: $(HOST_BIN)
	./$(HOST_BIN)
gamedump: $(HOST_BIN)
	./$(HOST_BIN) --dump
gameselftest: $(HOST_BIN)
	./$(HOST_BIN) --selftest

clean:
	rm -f pong_sim gamehost_*
