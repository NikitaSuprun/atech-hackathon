#pragma once
#include <stdint.h>
#include "color.h"

// The abstract drawing surface. Games and the OS draw ONLY through this — with
// no knowledge of physical LEDs, serpentine wiring, or panel count. Backed by a
// caller-owned Color buffer that the screen adapter serializes and renders.
// Resolution is a parameter (width()/height()) so the same game code scales.

namespace console {

class Canvas {
public:
    Canvas(Color* buf, int w, int h) : buf_(buf), w_(w), h_(h) {}

    int width() const { return w_; }
    int height() const { return h_; }
    Color* buffer() { return buf_; }
    const Color* buffer() const { return buf_; }

    void clear(Color c = BLACK) {
        for (int i = 0; i < w_ * h_; ++i) buf_[i] = c;
    }

    void pixel(int x, int y, Color c) {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return;
        buf_[y * w_ + x] = c;
    }

    Color get(int x, int y) const {
        if (x < 0 || x >= w_ || y < 0 || y >= h_) return BLACK;
        return buf_[y * w_ + x];
    }

    void hline(int x, int y, int len, Color c) {
        for (int i = 0; i < len; ++i) pixel(x + i, y, c);
    }
    void vline(int x, int y, int len, Color c) {
        for (int i = 0; i < len; ++i) pixel(x, y + i, c);
    }
    void rect(int x, int y, int w, int h, Color c) {
        hline(x, y, w, c);
        hline(x, y + h - 1, w, c);
        vline(x, y, h, c);
        vline(x + w - 1, y, h, c);
    }
    void fill(int x, int y, int w, int h, Color c) {
        for (int j = 0; j < h; ++j) hline(x, y + j, w, c);
    }
    // Row-major w*h sprite; caller clips by drawing within bounds.
    void blit(int x, int y, const Color* src, int sw, int sh) {
        for (int j = 0; j < sh; ++j)
            for (int i = 0; i < sw; ++i) pixel(x + i, y + j, src[j * sw + i]);
    }

private:
    Color* buf_;
    int    w_, h_;
};

}  // namespace console
