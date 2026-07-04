#pragma once
#include <stdint.h>
#include "theme.h"

// The starter theme registry: five hand-authored design-token bundles. Defined
// once in themes.cpp (C++14 needs a real definition, not an inline variable) and
// shared by both boards, the sim, and eval. Wrap with ThemeManager(THEMES, THEME_COUNT).
extern const console::Theme THEMES[];
extern const uint8_t THEME_COUNT;
