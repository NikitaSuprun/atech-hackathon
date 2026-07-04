#pragma once
#include "game_registry.h"

// Populates the registry with every game shipped in the ROM. Defined in one .cpp
// so the header-only games are each instantiated exactly once (no ODR clash).

namespace console_os {

void registerBuiltinGames(GameRegistry& reg);

}  // namespace console_os
