#pragma once

// Minimal 4-bit-per-pixel drawing for the dashboard. A Canvas is either the
// live VGA frame buffer or an off-screen panel. Panels are drawn completely
// off screen and then copied row by row into the frame buffer, so the monitor
// never shows a half-cleared widget (the "flash" a direct redraw causes).

#include <cstddef>
#include <cstdint>

namespace gfx {

// Palette indices (colours are set in dashboard.cpp).
enum Color : uint8_t {
    BG = 0, PANEL, EDGE, WHITE, MUTED, CYAN, GREEN, AMBER,
    RED, BLUE, MAGENTA, GRID, BLACK, DIM_GREEN, DIM_RED, YELLOW,
};

struct Canvas {
    uint8_t *pixels = nullptr;
    int width = 0;    // even
    int height = 0;
    int stride = 0;   // bytes per row

    bool allocate(int w, int h);   // PSRAM
    void fill(uint8_t color);
    void pixel(int x, int y, uint8_t color);
    void rect(int x, int y, int w, int h, uint8_t color);
    void frame(int x, int y, int w, int h, uint8_t color);
    void hline(int x, int y, int w, uint8_t color) { rect(x, y, w, 1, color); }
    void vline(int x, int y, int h, uint8_t color) { rect(x, y, 1, h, color); }
    void line(int x0, int y0, int x1, int y1, uint8_t color);
    // 8x16 font, integer scale. Returns the x after the last character.
    int text(int x, int y, const char *value, uint8_t color, int scale = 1);
    static int text_width(const char *value, int scale = 1);
    uint32_t hash() const;
    // Copies this canvas into `target` at (x, y); x must be even.
    void blit_to(Canvas &target, int x, int y) const;
};

}  // namespace gfx
