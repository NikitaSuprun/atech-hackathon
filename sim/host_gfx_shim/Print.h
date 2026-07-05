#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Minimal Arduino `Print` base so the vendored Adafruit_GFX compiles on the host.
// Adafruit_GFX overrides write(uint8_t) to draw a glyph; print(const char*) walks
// the string a byte at a time — the only path TftDashboard's text() exercises.

class Print {
public:
    virtual ~Print() {}

    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t* buf, size_t size) {
        size_t n = 0;
        while (size--) n += write(*buf++);
        return n;
    }

    size_t write(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
    size_t print(const char* s) { return s ? write((const uint8_t*)s, strlen(s)) : 0; }
};
