#include "scene.h"

#include <cstdio>
#include <cstring>

#include "app.h"
#include "gfx.h"
#include "pins.h"
#include "vga4.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace scene {
namespace {

using namespace gfx;

constexpr char TAG[] = "sptw_scene";

constexpr uint32_t PALETTE[16] = {
    0x05070C,   // BG
    0x141C2A,   // PANEL
    0x2C3A52,   // EDGE
    0xF2F5FA,   // WHITE
    0x8A97AB,   // MUTED
    0x22D3EE,   // CYAN
    0x34D399,   // GREEN
    0xF59E0B,   // AMBER
    0xEF4444,   // RED
    0x3B82F6,   // BLUE
    0xD946EF,   // MAGENTA
    0x1E293B,   // GRID
    0x000000,   // BLACK
    0x14532D,   // DIM_GREEN
    0x7F1D1D,   // DIM_RED
    0xFDE047,   // YELLOW
};

constexpr int HEADER_H = 64;
constexpr int TABLE_X = 0, TABLE_Y = 72, TABLE_W = 432, TABLE_H = 352;
constexpr int RIGHT_X = 440, RIGHT_Y = 72, RIGHT_W = 360, RIGHT_H = 352;
constexpr int BAND_Y = 432, BAND_H = 128;
constexpr int FOOTER_Y = 560, FOOTER_H = 40;
constexpr int SCOPE_LINE = BAND_Y + 8;   // the scan line the logic view shows

// Line codes: bit 0 = B2 (GPIO17, MAX7219 LOAD/CS), bit 1 = B3 (GPIO18, CLK),
// bit 2 = B4 (GPIO21, DIN).
uint8_t code_of[16];

Canvas screen, header, table, right, footer;
uint32_t header_hash = 0, table_hash = 0, right_hash = 0, footer_hash = 0;

// ---------------------------------------------------------- MAX7219 model
struct Max7219 {
    uint8_t digit[8];
    uint8_t decode, intensity, scan, shutdown, test;
};

struct Analysis {
    bool valid;
    uint32_t clk_edges;
    uint32_t load_edges;
    uint32_t narrow_clk;
    uint32_t writes[16];
    Max7219 state;
    uint8_t line[vga::WIDTH];
    int64_t computed_us;
};

Analysis analysis = {};

void apply(Max7219 &chip, uint16_t word, uint32_t *writes) {
    const uint8_t address = (word >> 8) & 0x0F;
    const uint8_t data = word & 0xFF;
    if (writes != nullptr) writes[address]++;
    switch (address) {
        case 0x9: chip.decode = data; break;
        case 0xA: chip.intensity = data & 0x0F; break;
        case 0xB: chip.scan = data & 0x07; break;
        case 0xC: chip.shutdown = data & 0x01; break;
        case 0xF: chip.test = data & 0x01; break;
        default:
            if (address >= 1 && address <= 8) chip.digit[address - 1] = data;
            break;   // 0x0 no-op, 0xD/0xE unused
    }
}

// Replays one frame of the frame buffer through an ideal MAX7219, the way
// the HCT244 would hand GPIO17/18/21 to it if S6-2 were on. Assumes the data
// lines idle low during blanking. Two passes, so the result does not depend
// on the power-on state; only the second pass is counted.
void analyse() {
    const uint8_t *fb = vga::framebuffer();
    Max7219 chip = {};
    uint16_t shift = 0;
    Analysis result = {};
    const int64_t started = esp_timer_get_time();
    for (int pass = 0; pass < 2; ++pass) {
        const bool count = pass == 1;
        for (int y = 0; y < vga::HEIGHT; ++y) {
            const uint8_t *row = fb + y * vga::STRIDE;
            uint8_t previous = 0;
            int run = 0;
            for (int x = 0; x < vga::WIDTH; ++x) {
                const uint8_t packed = row[x >> 1];
                const uint8_t code = code_of[(x & 1) ? (packed >> 4) : (packed & 0x0F)];
                const uint8_t rise = code & static_cast<uint8_t>(~previous);
                if (code & 2) {
                    run = (rise & 2) ? 1 : run + 1;
                } else if ((previous & 2) && run == 1 && count) {
                    result.narrow_clk++;
                }
                if (rise & 2) {
                    shift = static_cast<uint16_t>((shift << 1) | ((code >> 2) & 1));
                    if (count) result.clk_edges++;
                }
                if (rise & 1) {
                    if (count) result.load_edges++;
                    apply(chip, shift, count ? result.writes : nullptr);
                }
                if (count && y == SCOPE_LINE) result.line[x] = code;
                previous = code;
            }
            if ((previous & 2) && run == 1 && count) result.narrow_clk++;
        }
    }
    result.state = chip;
    result.valid = true;
    result.computed_us = esp_timer_get_time() - started;
    analysis = result;
}

// ---------------------------------------------------------------- drawing
uint8_t bit_colour(const pins::Role &role) {
    if (role.vga[0] == 'R') return RED;
    if (role.vga[0] == 'G') return GREEN;
    if (role.vga[0] == 'B') return BLUE;
    return WHITE;
}

void draw_header(const View &view, const vga::Stats &stats) {
    header.fill(BG);
    header.text(16, 6, "SAME PINS, TWO WORLDS", WHITE, 2);
    char line[96];
    switch (view.stage) {
        case Stage::WORLD_A:
            std::snprintf(line, sizeof(line), "WORLD A: VGA %dx%d at %.0f MHz owns all 18 pins",
                          vga::WIDTH, vga::HEIGHT, stats.pixel_clock_hz / 1e6);
            break;
        case Stage::TO_B: std::snprintf(line, sizeof(line), "Pins released. Set S6 for World B (see OLED)"); break;
        case Stage::WORLD_B: std::snprintf(line, sizeof(line), "WORLD B active: colour lines Hi-Z, GPIO2 = WS2812B data"); break;
        case Stage::TO_A: std::snprintf(line, sizeof(line), "Outputs off. Set S6 back for VGA (see OLED)"); break;
        case Stage::FAULT_ON: std::snprintf(line, sizeof(line), "FAULT DEMO: S6-2 left ON while VGA runs"); break;
        case Stage::FAULT_OFF: std::snprintf(line, sizeof(line), "FAULT DEMO: S6-2 OFF again - the noise stops"); break;
    }
    const bool fault = view.stage == Stage::FAULT_ON || view.stage == Stage::FAULT_OFF;
    header.text(392, 14, fault ? "C2: ISOLATION" : "C1: 14 PINS, 2 JOBS", fault ? AMBER : CYAN);
    header.text(16, 44, line, fault ? AMBER : MUTED);
    header.hline(0, HEADER_H - 1, header.width, EDGE);
}

void draw_table(const View &view) {
    table.fill(PANEL);
    table.frame(0, 0, table.width, table.height, EDGE);
    const bool vga_on = view.stage == Stage::WORLD_A || view.stage == Stage::FAULT_ON ||
                        view.stage == Stage::FAULT_OFF;
    const bool b_on = view.stage == Stage::WORLD_B;
    table.text(8, 4, "GPIO", MUTED);
    table.text(56, 4, "WORLD A", vga_on ? CYAN : MUTED);
    table.text(152, 4, "WORLD B", b_on ? AMBER : MUTED);
    table.text(320, 4, "PATH", MUTED);
    const pins::Role &spot = pins::spotlight(view.spotlight);
    for (int index = 0; index < 18; ++index) {
        const pins::Role &role = pins::ROLES[index];
        const int y = 24 + index * 18;
        if (role.gpio == spot.gpio) table.rect(2, y - 1, table.width - 4, 18, EDGE);
        char gpio[8];
        std::snprintf(gpio, sizeof(gpio), "%2d", role.gpio);
        table.text(12, y, gpio, WHITE);
        table.rect(40, y + 3, 8, 10, bit_colour(role));
        table.text(56, y, role.vga, vga_on ? WHITE : MUTED);
        const uint8_t b_colour = !role.shared ? GRID : (b_on ? (role.gpio == spot.gpio ? YELLOW : AMBER) : MUTED);
        table.text(112, y, role.world_b, b_colour);
        table.text(288, y, role.path, role.shared ? MUTED : GRID);
    }
}

void draw_palette(const View &view) {
    right.text(8, 4, "PALETTE  B4 B3 B2", WHITE);
    right.text(8, 22, "         DIN CLK CS (MAX7219)", MUTED);
    for (int index = 0; index < 16; ++index) {
        const int column = index / 8;
        const int x = 8 + column * 176;
        const int y = 44 + (index % 8) * 22;
        right.rect(x, y, 40, 16, static_cast<uint8_t>(index));
        right.frame(x, y, 40, 16, EDGE);
        char bits[16];
        std::snprintf(bits, sizeof(bits), "%d  %d  %d", (code_of[index] >> 2) & 1,
                      (code_of[index] >> 1) & 1, code_of[index] & 1);
        right.text(x + 56, y, bits, MUTED);
    }
    right.text(8, 230, "Every blue pixel is also a level", WHITE);
    right.text(8, 248, "on GPIO17/18/21. With S6-2 off", WHITE);
    right.text(8, 266, "the HCT244 keeps it from the", WHITE);
    right.text(8, 284, "matrix. Firmware cannot read S6.", WHITE);
    char line[48];
    std::snprintf(line, sizeof(line), "World switches so far: %lu",
                  static_cast<unsigned long>(view.switches));
    right.text(8, 316, line, CYAN);
}

void draw_trace(int y, const char *label, int bit) {
    right.text(8, y + 2, label, MUTED);
    constexpr int X0 = 88, WIDTH = 264, HIGH = 2, LOW = 16;
    for (int x = 0; x < WIDTH; ++x) {
        const int from = x * vga::WIDTH / WIDTH;
        const int to = (x + 1) * vga::WIDTH / WIDTH;
        int ones = 0;
        for (int source = from; source < to; ++source) ones += (analysis.line[source] >> bit) & 1;
        if (ones == 0) right.pixel(X0 + x, y + LOW, GREEN);
        else if (ones == to - from) right.pixel(X0 + x, y + HIGH, GREEN);
        else right.vline(X0 + x, y + HIGH, LOW - HIGH + 1, GREEN);
    }
}

void draw_scope(const View &view) {
    right.text(8, 4, "WHAT THE MAX7219 GETS", AMBER);
    char line[64];
    std::snprintf(line, sizeof(line), "line %d, simulated from the frame", SCOPE_LINE);
    right.text(8, 22, line, MUTED);
    if (!analysis.valid) {
        right.text(8, 60, "computing...", MUTED);
        return;
    }
    draw_trace(44, "CS  17", 0);
    draw_trace(66, "CLK 18", 1);
    draw_trace(88, "DIN 21", 2);
    right.text(8, 116, "Per frame (if S6-2 is on):", WHITE);
    std::snprintf(line, sizeof(line), " CLK edges   %lu", static_cast<unsigned long>(analysis.clk_edges));
    right.text(8, 134, line, YELLOW);
    std::snprintf(line, sizeof(line), " LOAD edges  %lu (= writes)", static_cast<unsigned long>(analysis.load_edges));
    right.text(8, 152, line, YELLOW);
    const unsigned narrow_pct = analysis.clk_edges == 0 ? 0
        : static_cast<unsigned>(100ULL * analysis.narrow_clk / analysis.clk_edges);
    std::snprintf(line, sizeof(line), " 28 ns CLK pulses %u%% (<50 ns)", narrow_pct);
    right.text(8, 170, line, MUTED);
    std::snprintf(line, sizeof(line), " test %lu  shutdown %lu  scan %lu",
                  static_cast<unsigned long>(analysis.writes[0xF]),
                  static_cast<unsigned long>(analysis.writes[0xC]),
                  static_cast<unsigned long>(analysis.writes[0xB]));
    right.text(8, 188, line, MUTED);

    // Predicted matrix after one frame.
    const Max7219 &chip = analysis.state;
    constexpr int CELL = 13, MX = 8, MY = 214;
    right.frame(MX - 2, MY - 2, 8 * CELL + 3, 8 * CELL + 3, EDGE);
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            bool on = false;
            if (chip.test) on = true;
            else if (chip.shutdown && row <= chip.scan) on = (chip.digit[row] >> (7 - column)) & 1;
            right.rect(MX + column * CELL, MY + row * CELL, CELL - 2, CELL - 2, on ? RED : DIM_RED);
        }
    }
    right.text(124, 214, "predicted matrix", WHITE);
    right.text(124, 234, chip.test ? "display test ON" : (chip.shutdown ? "running" : "shutdown"),
               chip.test ? RED : (chip.shutdown ? GREEN : MUTED));
    std::snprintf(line, sizeof(line), "scan 0-%d  int %d", chip.scan, chip.intensity);
    right.text(124, 252, line, MUTED);
    std::snprintf(line, sizeof(line), "decode 0x%02X", chip.decode);
    right.text(124, 270, line, MUTED);
    right.text(124, 296, "GPIO2 VSYNC also", MUTED);
    right.text(124, 314, "hits WS2812B DIN", MUTED);
    right.text(8, 334, view.noise ? "N: freeze the band" : "N: animate the band", CYAN);
}

void draw_right(const View &view) {
    right.fill(PANEL);
    right.frame(0, 0, right.width, right.height, EDGE);
    if (view.stage == Stage::FAULT_ON || view.stage == Stage::FAULT_OFF) draw_scope(view);
    else draw_palette(view);
}

void draw_footer(const View &view, const vga::Stats &stats) {
    footer.fill(BG);
    footer.hline(0, 0, footer.width, EDGE);
    char line[120];
    std::snprintf(line, sizeof(line),
                  "LATE %lu  IRR %lu  ISR %.0f/%.0f us  %.1f Hz  frames %lu  input %s",
                  static_cast<unsigned long>(stats.late),
                  static_cast<unsigned long>(stats.irregular_frames), stats.max_exec_us,
                  stats.budget_us, stats.refresh_hz, static_cast<unsigned long>(stats.vsyncs),
                  view.last_input);
    footer.text(8, 4, line, stats.late == 0 ? GREEN : AMBER);
    const char *keys = "Enter/BOOT next  F fault  N noise  P pin  C clear";
    if (view.stage == Stage::TO_A || view.stage == Stage::TO_B) keys = "Set S6 as the OLED says, then Enter/BOOT/GPIO4.  Esc/B cancels";
    footer.text(8, 22, keys, MUTED);
}

void blit_if_changed(Canvas &canvas, uint32_t &last_hash, int x, int y) {
    const uint32_t hash = canvas.hash();
    if (hash == last_hash) return;
    last_hash = hash;
    canvas.blit_to(screen, x, y);
}

uint32_t random_state = 0x12345678;
inline uint32_t next_random() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

void draw_band(bool moving) {
    if (moving) {
        // 8x4-pixel cells, so B2..B4 toggle at up to 4.5 MHz along a line.
        for (int cy = 0; cy < BAND_H; cy += 4) {
            uint8_t *row = screen.pixels + (BAND_Y + cy) * screen.stride;
            for (int cx = 0; cx < vga::STRIDE; cx += 4) {
                const uint8_t colour = static_cast<uint8_t>(next_random() & 0x0F);
                std::memset(row + cx, colour * 0x11, 4);
            }
            for (int line = 1; line < 4; ++line) {
                std::memcpy(row + line * screen.stride, row, screen.stride);
            }
        }
    } else {
        for (int bar = 0; bar < 16; ++bar) screen.rect(bar * 50, BAND_Y, 50, BAND_H, static_cast<uint8_t>(bar));
    }
}

void renderer(void *) {
    uint32_t frame = 0;
    int64_t next_panels = 0;
    int64_t next_analysis = 0;
    View last_view = {};
    bool band_static_drawn = false;
    while (true) {
        vga::wait_vsync(100);
        ++frame;
        const View view = view_get();
        const int64_t now = esp_timer_get_time();
        const bool fault = view.stage == Stage::FAULT_ON || view.stage == Stage::FAULT_OFF;
        const bool moving = view.noise && view.stage != Stage::WORLD_B;

        if (moving && (frame & 1) == 0) {
            draw_band(true);
            band_static_drawn = false;
        } else if (!moving && !band_static_drawn) {
            draw_band(false);
            band_static_drawn = true;
        }

        if (fault && now >= next_analysis) {
            analyse();
            next_analysis = now + 1'000'000;
            next_panels = 0;
        }
        if (!fault) analysis.valid = false;

        if (now >= next_panels || std::memcmp(&view, &last_view, sizeof(view)) != 0) {
            const vga::Stats stats = vga::stats();
            draw_header(view, stats);
            draw_table(view);
            draw_right(view);
            draw_footer(view, stats);
            blit_if_changed(header, header_hash, 0, 0);
            blit_if_changed(table, table_hash, TABLE_X, TABLE_Y);
            blit_if_changed(right, right_hash, RIGHT_X, RIGHT_Y);
            blit_if_changed(footer, footer_hash, 0, FOOTER_Y);
            last_view = view;
            next_panels = now + 250'000;
        }
    }
}

constexpr uint8_t blue_bits(uint32_t rgb) {
    // 5-bit blue as the RGB565 conversion in vga4.cpp: B0..B4 = bits 0..4.
    return static_cast<uint8_t>((rgb & 0xFF) >> 3);
}

}  // namespace

bool start() {
    vga::set_palette(PALETTE);
    for (int index = 0; index < 16; ++index) {
        const uint8_t blue = blue_bits(PALETTE[index]);
        code_of[index] = static_cast<uint8_t>(((blue >> 2) & 1) | (((blue >> 3) & 1) << 1) |
                                              (((blue >> 4) & 1) << 2));
    }
    screen.pixels = vga::framebuffer();
    screen.width = vga::WIDTH;
    screen.height = vga::HEIGHT;
    screen.stride = vga::STRIDE;
    if (screen.pixels == nullptr) return false;
    if (!header.allocate(vga::WIDTH, HEADER_H) || !table.allocate(TABLE_W, TABLE_H) ||
        !right.allocate(RIGHT_W, RIGHT_H) || !footer.allocate(vga::WIDTH, FOOTER_H)) {
        ESP_LOGE(TAG, "No PSRAM for the panels");
        return false;
    }
    random_state ^= esp_random();
    return xTaskCreatePinnedToCore(renderer, "render", 6144, nullptr, 2, nullptr, 1) == pdPASS;
}

}  // namespace scene
