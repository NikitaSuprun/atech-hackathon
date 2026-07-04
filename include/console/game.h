#pragma once
#include <stdint.h>
#include "audio.h"
#include "canvas.h"
#include "input.h"
#include "theme.h"

// Every game is an isolated app. It draws ONLY to Canvas (via theme tokens),
// reads ONLY Input, and never touches hardware. The OS owns the fixed-rate loop,
// injects input + the active theme, and drives the app lifecycle. This is the
// single interface the whole game library fans out against.

namespace console {

// Services the OS hands a game (audio sink, active theme, a per-run rng seed).
struct GameContext {
    Audio*       audio;
    const Theme* theme;
    uint32_t     rngSeed;
};

enum GameEvent : uint8_t { EV_PAUSE, EV_RESUME, EV_MENU, EV_EXIT };

struct GameMeta {
    const char*    name;
    const uint8_t* icon;     // small menu sprite, may be null
    uint8_t        players;  // 1 or 2
};

class Game {
public:
    virtual ~Game() {}

    virtual const GameMeta& meta() const = 0;
    virtual void init(const GameContext& ctx) = 0;
    virtual void update(const Input& in, uint32_t dtMs) = 0;
    virtual void draw(Canvas& c, const Theme& t) = 0;

    virtual void teardown() {}
    virtual void onEvent(GameEvent) {}
};

}  // namespace console
