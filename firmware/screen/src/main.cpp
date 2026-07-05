// Console SCREEN board — the dumb glowing renderer. Drains COBS frame packets off
// the serial link and renders them onto twelve 3x3 WS2812 tiles via the light
// engine + reused pong compositor. Pins + tile order match the Atech 14-port
// screen build (generated old-pong screen), so the physical wiring is unchanged.
#include <Arduino.h>

#include "link_cobs_serial.h"
#include "neo_tile.h"
#include "screen_render_board.h"

// Tile data lines in TILE_MAP / compositor order — line B / pin_b of each screen
// port (these panels are the SK6812/RGBW revision, wired to line B, not line A).
static NeoTile t0(8), t1(4), t2(18), t3(15), t4(10), t5(12), t6(7), t7(41), t8(2),
    t9(44), t10(38), t11(35);

// NB: named g_link (not `link`) — POSIX declares a global `int link(const char*,
// const char*)` that would otherwise shadow-collide in the ctor argument.
static LinkCobsSerial    g_link;  // COBS-framed frames in over USB-CDC serial
static ScreenRenderBoard g_board(g_link);

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);  // never block if the bridge/host isn't reading

    NeoTile* nt[12] = {&t0, &t1, &t2, &t3, &t4, &t5, &t6, &t7, &t8, &t9, &t10, &t11};
    for (int i = 0; i < 12; ++i) nt[i]->begin();

    console::TileSink* tiles[12];
    for (int i = 0; i < 12; ++i) tiles[i] = nt[i];

    // Seed profile until the brain's first SET_LIGHT_PROFILE arrives.
    console::LightProfile lp{40, 80, 200, 96, 100};
    g_board.begin(tiles, 12, lp, millis());
}

void loop() {
    g_board.tick(millis());
}
