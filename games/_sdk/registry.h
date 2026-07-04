#pragma once
#include "console/game.h"

// The host<->game link: exactly one linked game translation unit defines these
// (via REGISTER_GAME). The host binary calls createGame() to get its one app.

namespace host {
console::Game* createGame();
const char*    gameName();
}  // namespace host

// Place in a game's .cpp, e.g. REGISTER_GAME(demo::DemoGame)
#define REGISTER_GAME(CLS)                                                 \
    namespace host {                                                       \
    console::Game* createGame() { static CLS instance; return &instance; } \
    const char* gameName() { return createGame()->meta().name; }           \
    }
