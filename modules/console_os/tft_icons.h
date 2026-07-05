#pragma once
#include <stdint.h>
#include "tft_surface.h"

// 7x7 pixel glyphs — one per built-in game (registry order: eggcatch..demo) plus
// a settings cog. Authored as readable ASCII bitmaps ('#' = lit); drawn scaled &
// tinted through the Tft interface, so device and host render them identically.
// These same masks become the on-device icon art.

namespace console_os {

struct Icon {
    uint8_t            w, h;
    const char* const* rows;
};

namespace icons {

static const char* const R_EGG[]   = {"  ###  ", " ##### ", "#######", "#######",
                                       "#######", " ##### ", "  ###  "};
static const char* const R_SNAKE[] = {"##     ", "###    ", " ###   ", "  ###  ",
                                       "   ### ", "    ###", "    ###"};
static const char* const R_PONG[]  = {"#     #", "#     #", "#     #", "#  #  #",
                                       "#     #", "#     #", "#     #"};
static const char* const R_CAR[]   = {" #   # ", "#######", "## # ##", "#######",
                                       "## # ##", "#######", " #   # "};
static const char* const R_BIRD[]  = {"  ###  ", " ##### ", "### ###", "#######",
                                       "#### ##", " ##### ", "  ###  "};
static const char* const R_DOODLE[]= {"####   ", "       ", "  ##   ", " #### #",
                                       "  ##  #", "      #", "##  ###"};
static const char* const R_INV[]   = {"  # #  ", " ##### ", "## # ##", "#######",
                                       "# ### #", "#  #  #", "  # #  "};
static const char* const R_NOTE[]  = {"    ###", "    # #", "    #  ", "    #  ",
                                       "##  #  ", "###    ", "###    "};
static const char* const R_WAVE[]  = {"  ###  ", " #   # ", "#  #  #", "# ### #",
                                       "#  #  #", " #   # ", "  ###  "};
static const char* const R_CHIP[]  = {" # # # ", "#######", "# ### #", "# # # #",
                                       "# ### #", "#######", " # # # "};
static const char* const R_GEAR[]  = {"  ###  ", " #   # ", "## # ##", "#  #  #",
                                       "## # ##", " #   # ", "  ###  "};

static const Icon EGGCATCH{7, 7, R_EGG};
static const Icon SNAKE   {7, 7, R_SNAKE};
static const Icon PONG    {7, 7, R_PONG};
static const Icon RACING  {7, 7, R_CAR};
static const Icon FLAPPY  {7, 7, R_BIRD};
static const Icon DOODLE  {7, 7, R_DOODLE};
static const Icon INVADERS{7, 7, R_INV};
static const Icon JUKEBOX {7, 7, R_NOTE};
static const Icon AMBIENT {7, 7, R_WAVE};
static const Icon DEMO    {7, 7, R_CHIP};
static const Icon SETTINGS{7, 7, R_GEAR};

}  // namespace icons

// Icon + tagline by registry index (0..9). Clamped so out-of-range is safe.
inline const Icon& gameIcon(int i) {
    static const Icon* const T[10] = {
        &icons::EGGCATCH, &icons::SNAKE,   &icons::PONG,    &icons::RACING, &icons::FLAPPY,
        &icons::DOODLE,   &icons::INVADERS, &icons::JUKEBOX, &icons::AMBIENT, &icons::DEMO};
    if (i < 0) i = 0;
    if (i > 9) i = 9;
    return *T[i];
}

inline const char* gameTag(int i) {
    static const char* const T[10] = {
        "CATCH THE FALLING EGGS", "RELATIVE-TURN SNAKE", "TWO-KNOB LOCAL PONG",
        "BRICK-GAME CAR DODGE",   "TAP TO FLAP AND FLY", "AUTO-BOUNCE CLIMBER",
        "FORMATION SHOOTER",      "RTTTL PLAYER + VU",   "GENERATIVE SCENES",
        "REFERENCE GAME"};
    if (i < 0) i = 0;
    if (i > 9) i = 9;
    return T[i];
}

// Draw an icon centered at (cx,cy), each lit cell a `cell`x`cell` block in color c.
inline void drawIcon(Tft& t, const Icon& ic, int cx, int cy, int cell, uint16_t c) {
    int x0 = cx - ic.w * cell / 2;
    int y0 = cy - ic.h * cell / 2;
    for (int r = 0; r < ic.h; ++r) {
        const char* row = ic.rows[r];
        for (int col = 0; col < ic.w; ++col)
            if (row[col] == '#') t.fillRect(x0 + col * cell, y0 + r * cell, cell, cell, c);
    }
}

}  // namespace console_os
