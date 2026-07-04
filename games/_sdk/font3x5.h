#pragma once
#include <stdint.h>
#include "console/canvas.h"
#include "console/color.h"

// Tiny 3x5 bitmap font for the 6-wide LED matrix (scores, short labels, scrollers).
// Each glyph row uses the low 3 bits, MSB = leftmost column.

namespace sdk {

constexpr int FONT_W = 3;
constexpr int FONT_H = 5;
constexpr int GLYPH_ADVANCE = FONT_W + 1;   // 3px glyph + 1px gap

// 0-9
static const uint8_t FONT_DIGITS[10][FONT_H] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b010, 0b010, 0b010},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
};

// A-Z
static const uint8_t FONT_LETTERS[26][FONT_H] = {
    {0b010, 0b101, 0b111, 0b101, 0b101},  // A
    {0b110, 0b101, 0b110, 0b101, 0b110},  // B
    {0b011, 0b100, 0b100, 0b100, 0b011},  // C
    {0b110, 0b101, 0b101, 0b101, 0b110},  // D
    {0b111, 0b100, 0b110, 0b100, 0b111},  // E
    {0b111, 0b100, 0b110, 0b100, 0b100},  // F
    {0b011, 0b100, 0b101, 0b101, 0b011},  // G
    {0b101, 0b101, 0b111, 0b101, 0b101},  // H
    {0b111, 0b010, 0b010, 0b010, 0b111},  // I
    {0b011, 0b001, 0b001, 0b101, 0b010},  // J
    {0b101, 0b110, 0b100, 0b110, 0b101},  // K
    {0b100, 0b100, 0b100, 0b100, 0b111},  // L
    {0b101, 0b111, 0b111, 0b101, 0b101},  // M
    {0b101, 0b111, 0b111, 0b111, 0b101},  // N
    {0b010, 0b101, 0b101, 0b101, 0b010},  // O
    {0b110, 0b101, 0b110, 0b100, 0b100},  // P
    {0b010, 0b101, 0b101, 0b111, 0b011},  // Q
    {0b110, 0b101, 0b110, 0b101, 0b101},  // R
    {0b011, 0b100, 0b010, 0b001, 0b110},  // S
    {0b111, 0b010, 0b010, 0b010, 0b010},  // T
    {0b101, 0b101, 0b101, 0b101, 0b111},  // U
    {0b101, 0b101, 0b101, 0b101, 0b010},  // V
    {0b101, 0b101, 0b111, 0b111, 0b101},  // W
    {0b101, 0b101, 0b010, 0b101, 0b101},  // X
    {0b101, 0b101, 0b010, 0b010, 0b010},  // Y
    {0b111, 0b001, 0b010, 0b100, 0b111},  // Z
};

// 5 packed rows for ch, or nullptr for blank/unknown (space included).
inline const uint8_t* fontGlyph(char ch) {
    if (ch >= '0' && ch <= '9') return FONT_DIGITS[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return FONT_LETTERS[ch - 'A'];
    if (ch >= 'a' && ch <= 'z') return FONT_LETTERS[ch - 'a'];
    static const uint8_t colon[FONT_H] = {0b000, 0b010, 0b000, 0b010, 0b000};
    static const uint8_t dash[FONT_H]  = {0b000, 0b000, 0b111, 0b000, 0b000};
    static const uint8_t bang[FONT_H]  = {0b010, 0b010, 0b010, 0b000, 0b010};
    static const uint8_t dot[FONT_H]   = {0b000, 0b000, 0b000, 0b000, 0b010};
    static const uint8_t slash[FONT_H] = {0b001, 0b001, 0b010, 0b100, 0b100};
    switch (ch) {
        case ':': return colon;
        case '-': return dash;
        case '!': return bang;
        case '.': return dot;
        case '/': return slash;
        default:  return nullptr;
    }
}

// Draw one glyph, top-left at (x,y).
inline void drawChar(console::Canvas& cv, int x, int y, char ch, console::Color fg) {
    const uint8_t* g = fontGlyph(ch);
    if (!g) return;
    for (int r = 0; r < FONT_H; ++r)
        for (int c = 0; c < FONT_W; ++c)
            if (g[r] & (1 << (FONT_W - 1 - c))) cv.pixel(x + c, y + r, fg);
}

// Draw a string left-to-right (4px per char). Negative x scrolls. Returns end x.
inline int drawText(console::Canvas& cv, int x, int y, const char* s, console::Color fg) {
    for (; *s; ++s) { drawChar(cv, x, y, *s, fg); x += GLYPH_ADVANCE; }
    return x;
}

// Pixel width of a string (no trailing gap).
inline int textWidth(const char* s) {
    int n = 0;
    for (; *s; ++s) ++n;
    return n ? n * GLYPH_ADVANCE - 1 : 0;
}

// Draw an unsigned integer at (x,y). Returns end x.
inline int drawNumber(console::Canvas& cv, int x, int y, uint32_t n, console::Color fg) {
    char tmp[11];
    int i = 0;
    if (n == 0) tmp[i++] = '0';
    while (n) { tmp[i++] = char('0' + n % 10); n /= 10; }
    for (int j = i - 1; j >= 0; --j) { drawChar(cv, x, y, tmp[j], fg); x += GLYPH_ADVANCE; }
    return x;
}

}  // namespace sdk
