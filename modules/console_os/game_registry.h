#pragma once
#include "console/game.h"

// The OS-owned game registry the MENU fans out against. Each entry is a name +
// a factory that returns a persistent console::Game* (one instance per title,
// mirroring the SDK's REGISTER_GAME contract). The registry is a data table the
// menu enumerates — adding a game is a one-line add(), never a menu edit.
//
// Why not reuse games/_sdk/registry.h? REGISTER_GAME defines a single
// host::createGame() per translation unit (one game per binary), so linking N
// games would collide. The OS instead owns a multi-title table and instantiates
// the (header-only) games directly (see builtin_games.cpp).

namespace console_os {

using GameFactory = console::Game* (*)();

struct GameListing {
    const char* name;
    GameFactory make;  // returns a persistent instance; init() re-arms per launch
};

class GameRegistry {
public:
    static constexpr int MAX_GAMES = 32;

    bool add(const char* name, GameFactory make) {
        if (count_ >= MAX_GAMES || !name || !make) return false;
        items_[count_++] = {name, make};
        return true;
    }

    int                count() const { return count_; }
    const GameListing& at(int i) const { return items_[i]; }

    // -1 if not present.
    int indexOf(const char* name) const {
        for (int i = 0; i < count_; ++i)
            if (namesEqual(items_[i].name, name)) return i;
        return -1;
    }

private:
    static bool namesEqual(const char* a, const char* b) {
        if (a == b) return true;
        if (!a || !b) return false;
        while (*a && *a == *b) { ++a; ++b; }
        return *a == *b;
    }

    GameListing items_[MAX_GAMES] = {};
    int         count_ = 0;
};

}  // namespace console_os
