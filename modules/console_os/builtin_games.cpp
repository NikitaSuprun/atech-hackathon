#include "builtin_games.h"

#include "eggcatch.h"
#include "snake.h"
#include "pong.h"
#include "racing.h"
#include "flappy.h"
#include "doodlejump.h"
#include "invaders.h"
#include "jukebox.h"
#include "ambient.h"
#include "demo.h"

// Each factory returns THE one instance of its title (function-local static, one
// definition across the program). The OS calls make() to get it and init() to
// (re)start it, so the same object is safely reused across relaunches.

namespace console_os {
namespace {

console::Game* makeEggCatch() {
    static eggcatch::EggCatchGame g;
    return &g;
}
console::Game* makeSnake() {
    static snake::SnakeGame g;
    return &g;
}
console::Game* makePong() {
    static pong::PongGame g;
    return &g;
}
console::Game* makeRacing() {
    static racing::RacingGame g;
    return &g;
}
console::Game* makeFlappy() {
    static flappy::FlappyGame g;
    return &g;
}
console::Game* makeDoodleJump() {
    static doodlejump::DoodleJumpGame g;
    return &g;
}
console::Game* makeInvaders() {
    static invaders::InvadersGame g;
    return &g;
}
console::Game* makeJukebox() {
    static jukebox::JukeboxGame g;
    return &g;
}
console::Game* makeAmbient() {
    static ambient::AmbientGame g;
    return &g;
}
console::Game* makeDemo() {
    static demo::DemoGame g;
    return &g;
}

}  // namespace

void registerBuiltinGames(GameRegistry& reg) {
    // Name pulled from each game's own meta() so the menu label never drifts.
    // Registration order == menu order: flagship first, the demo/test title last.
    reg.add(makeEggCatch()->meta().name, &makeEggCatch);
    reg.add(makeSnake()->meta().name, &makeSnake);
    reg.add(makePong()->meta().name, &makePong);
    reg.add(makeRacing()->meta().name, &makeRacing);
    reg.add(makeFlappy()->meta().name, &makeFlappy);
    reg.add(makeDoodleJump()->meta().name, &makeDoodleJump);
    reg.add(makeInvaders()->meta().name, &makeInvaders);
    reg.add(makeJukebox()->meta().name, &makeJukebox);
    reg.add(makeAmbient()->meta().name, &makeAmbient);
    reg.add(makeDemo()->meta().name, &makeDemo);
}

}  // namespace console_os
