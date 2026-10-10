#include "gfx.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "font8x16.h"

#include "esp_heap_caps.h"

namespace gfx {

bool Canvas::allocate(int w, int h) {
    width = (w + 1) & ~1;
    height = h;
    stride = width / 2;
    pixels = static_cast<uint8_t *>(heap_caps_aligned_calloc(
        16, 1, static_cast<size_t>(stride) * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return pixels != nullptr;
}

void Canvas::fill(uint8_t color) {
    std::memset(pixels, (color & 0x0F) * 0x11, static_cast<size_t>(stride) * height);
}

void Canvas::pixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    uint8_t &cell = pixels[y * stride + x / 2];
    cell = (x & 1) ? static_cast<uint8_t>((cell & 0x0F) | (color << 4))
                   : static_cast<uint8_t>((cell & 0xF0) | (color & 0x0F));
}

void Canvas::rect(int x, int y, int w, int h, uint8_t color) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width, x + w);
    const int y1 = std::min(height, y + h);
    if (x0 >= x1 || y0 >= y1) return;
    const uint8_t both = (color & 0x0F) * 0x11;
    for (int row = y0; row < y1; ++row) {
        int left = x0;
        int right = x1;
        if (left & 1) pixel(left++, row, color);
        if (right & 1) pixel(--right, row, color);
        if (right > left) std::memset(pixels + row * stride + left / 2, both, (right - left) / 2);
    }
}

void Canvas::frame(int x, int y, int w, int h, uint8_t color) {
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint8_t color) {
    const int dx = std::abs(x1 - x0);
    const int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

int Canvas::text(int x, int y, const char *value, uint8_t color, int scale) {
    if (value == nullptr) return x;
    for (; *value != '\0'; ++value) {
        if (x >= width) break;
        const uint8_t *glyph = font8x16::glyph(*value);
        for (int row = 0; row < font8x16::HEIGHT; ++row) {
            const uint8_t bits = glyph[row];
            if (bits == 0) continue;
            for (int column = 0; column < font8x16::WIDTH; ++column) {
                if ((bits & (0x80 >> column)) == 0) continue;
                if (scale == 1) {
                    pixel(x + column, y + row, color);
                } else {
                    rect(x + column * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += font8x16::WIDTH * scale;
    }
    return x;
}

int Canvas::text_width(const char *value, int scale) {
    return value == nullptr ? 0
        : static_cast<int>(std::strlen(value)) * font8x16::WIDTH * scale;
}

uint32_t Canvas::hash() const {
    // FNV-1a over 32-bit words: enough to skip copying unchanged panels.
    uint32_t value = 2166136261u;
    const auto *words = reinterpret_cast<const uint32_t *>(pixels);
    const size_t count = static_cast<size_t>(stride) * height / 4;
    for (size_t index = 0; index < count; ++index) {
        value = (value ^ words[index]) * 16777619u;
    }
    return value;
}

void Canvas::blit_to(Canvas &target, int x, int y) const {
    const int rows = std::min(height, target.height - y);
    const int bytes = std::min(stride, target.stride - x / 2);
    if (rows <= 0 || bytes <= 0 || x < 0 || y < 0) return;
    for (int row = 0; row < rows; ++row) {
        std::memcpy(target.pixels + (y + row) * target.stride + x / 2,
                    pixels + row * stride, static_cast<size_t>(bytes));
    }
}

}  // namespace gfx
