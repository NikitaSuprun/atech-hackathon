#include "builtin_games.h"

#include "demo.h"
#include "racing.h"

// Each factory returns THE one instance of its title (function-local static, one
// definition across the program). The OS calls make() to get it and init() to
// (re)start it, so the same object is safely reused across relaunches.

namespace console_os {
namespace {

console::Game* makeDemo() {
    static demo::DemoGame g;
    return &g;
}
console::Game* makeRacing() {
    static racing::RacingGame g;
    return &g;
}

}  // namespace

void registerBuiltinGames(GameRegistry& reg) {
    // name pulled from each game's own meta() so the menu label never drifts.
    reg.add(makeDemo()->meta().name, &makeDemo);
    reg.add(makeRacing()->meta().name, &makeRacing);
}

}  // namespace console_os
