#pragma once
// Minimal Arduino shim: JUST enough for the vendored Adafruit_GFX.cpp, glcdfont.c
// and the FreeSansBold font headers to compile on the desktop host. No hardware,
// no Wire/SPI — PROGMEM collapses to nothing and pgm_read_* become plain derefs
// (Adafruit_GFX.cpp defines those itself when no MCU arch macro is set).

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef F
#define F(s) (s)
#endif

// Arduino angle helpers (Adafruit_GFX uses radians() in its rotated-rect path).
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295769236907684886
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG 57.295779513082320876798154814105
#endif
#ifndef radians
#define radians(deg) ((deg) * DEG_TO_RAD)
#endif
#ifndef degrees
#define degrees(rad) ((rad) * RAD_TO_DEG)
#endif

// Flash-string marker: Adafruit_GFX only ever takes its address / casts it.
class __FlashStringHelper;

#include "Print.h"

// Tiny stand-in for Arduino's String. Adafruit_GFX.cpp's String getTextBounds
// overload only calls length()/c_str(); TftDashboard itself uses const char*.
class String {
public:
    String() {}
    String(const char* p) : p_(p), n_(p ? strlen(p) : 0) {}
    size_t      length() const { return n_; }
    const char* c_str() const { return p_ ? p_ : ""; }

private:
    const char* p_ = nullptr;
    size_t      n_ = 0;
};
