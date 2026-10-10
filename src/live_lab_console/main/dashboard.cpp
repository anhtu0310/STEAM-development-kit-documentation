#include "dashboard.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "gfx.h"
#include "vga4.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace gfx;

namespace {

constexpr char TAG[] = "llc_dash";

// 16-colour palette (RGB888). Index names are in gfx.h.
constexpr uint32_t PALETTE[16] = {
    0x060D18,  // BG
    0x0E1B2C,  // PANEL
    0x22405E,  // EDGE
    0xE8F2F8,  // WHITE
    0x829DAE,  // MUTED
    0x2DD5E0,  // CYAN
    0x44D68B,  // GREEN
    0xFFB84D,  // AMBER
    0xFF5B69,  // RED
    0x5A8CFF,  // BLUE
    0xDC6EF0,  // MAGENTA
    0x16263A,  // GRID
    0x000000,  // BLACK
    0x1E5A3C,  // DIM_GREEN
    0x6E1E28,  // DIM_RED
    0xFAE65A,  // YELLOW
};

struct Panel {
    int x, y, w, h;
    Canvas canvas;
    uint32_t last_hash;
};

enum PanelId {
    HEADER, ETH, WIFI, BLE, USB, RELAY, SD, GRAPH, HEALTH, EVENTS, SYSTEM, FOOTER,
    PANEL_COUNT
};

Panel panels[PANEL_COUNT] = {
    {0, 0, 800, 44, {}, 0},
    {8, 52, 256, 120, {}, 0},
    {272, 52, 256, 120, {}, 0},
    {536, 52, 256, 120, {}, 0},
    {8, 180, 256, 120, {}, 0},
    {272, 180, 256, 120, {}, 0},
    {536, 180, 256, 120, {}, 0},
    {8, 308, 520, 150, {}, 0},
    {536, 308, 256, 150, {}, 0},
    {8, 466, 520, 106, {}, 0},
    {536, 466, 256, 106, {}, 0},
    {0, 578, 800, 22, {}, 0},
};

Canvas screen;

// ---- history (1 Hz)
constexpr int HISTORY = 250;
float temperature_history[HISTORY];
float pressure_history[HISTORY];
uint16_t late_history[60];      // late refills per second
uint16_t irregular_history[60];
int history_count = 0;
int late_count = 0;

// ------------------------------------------------------------------ helpers
void card(Canvas &c, const char *title, uint8_t accent) {
    c.fill(PANEL);
    c.frame(0, 0, c.width, c.height, EDGE);
    c.rect(0, 0, 4, c.height, accent);
    c.text(14, 8, title, MUTED);
}

void line_text(Canvas &c, int y, const char *format, uint8_t color, ...)
    __attribute__((format(printf, 3, 5)));
void line_text(Canvas &c, int y, const char *format, uint8_t color, ...) {
    char text[64];
    va_list args;
    va_start(args, color);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    c.text(14, y, text, color);
}

void big(Canvas &c, const char *value, uint8_t color) {
    c.text(14, 30, value, color, 2);
}

void hms(char *out, size_t size, uint32_t seconds) {
    std::snprintf(out, size, "%02lu:%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 3600),
                  static_cast<unsigned long>((seconds / 60) % 60),
                  static_cast<unsigned long>(seconds % 60));
}

// ------------------------------------------------------------------ panels
void draw_header(Canvas &c, const AppState &s, const vga::Stats &v, uint32_t tick) {
    c.fill(PANEL);
    c.rect(0, c.height - 2, c.width, 2, CYAN);
    c.text(16, 6, "STEAM", CYAN, 2);
    c.text(16 + 6 * 16, 6, "LIVE LAB CONSOLE", WHITE, 2);

    char text[48];
    std::snprintf(text, sizeof(text), "800x600 %.1fHz 4bpp", v.refresh_hz);
    c.text(440, 4, text, MUTED);
    char up[16];
    hms(up, sizeof(up), s.uptime_s);
    std::snprintf(text, sizeof(text), "UP %s", up);
    c.text(440, 22, text, WHITE);

    const char *badge = "STRESS OFF";
    uint8_t badge_bg = EDGE;
    uint8_t badge_fg = MUTED;
    if (s.stress == StressLevel::REALISTIC) {
        badge = "STRESS ON";
        badge_bg = AMBER;
        badge_fg = BLACK;
    } else if (s.stress == StressLevel::ENVELOPE) {
        badge = "ENVELOPE";
        badge_bg = RED;
        badge_fg = WHITE;
    }
    c.rect(636, 7, 152, 28, badge_bg);
    c.text(636 + (152 - Canvas::text_width(badge)) / 2, 13, badge, badge_fg);

    // Under stress a marker sweeps the header every frame, so the panel is
    // recomposed and copied at frame rate (renderer PSRAM load).
    if (s.stress != StressLevel::OFF) {
        const int x = static_cast<int>((tick * 8) % (c.width - 40));
        c.rect(x, c.height - 6, 40, 4, badge_bg);
    }
}

void draw_eth(Canvas &c, const AppState &s) {
    const uint8_t accent = s.eth_link ? GREEN : (s.eth_started ? AMBER : RED);
    card(c, "ETHERNET  W5500", accent);
    if (!s.eth_started) {
        big(c, "NO CHIP", RED);
    } else {
        big(c, s.eth_link ? "LINK UP" : "NO LINK", s.eth_link ? GREEN : AMBER);
    }
    if (s.eth_link) {
        line_text(c, 72, "%s %s", WHITE, s.eth_has_ip ? s.eth_ip : "waiting...",
                  s.eth_static_fallback ? "(static)" : (s.eth_has_ip ? "(DHCP)" : ""));
    } else {
        line_text(c, 72, "plug a cable", MUTED);
    }
    line_text(c, 92, "RX %5.1f Mbit/s %5lu MB", s.eth_rx_mbps > 0.05f ? CYAN : MUTED,
              s.eth_rx_mbps, static_cast<unsigned long>(s.eth_rx_mb));
}

void draw_wifi(Canvas &c, const AppState &s) {
    card(c, "WI-FI SOFT-AP", s.wifi_up ? GREEN : RED);
    char text[24];
    std::snprintf(text, sizeof(text), "%u CLIENT%s", s.wifi_stations,
                  s.wifi_stations == 1 ? "" : "S");
    big(c, s.wifi_up ? text : "OFF", s.wifi_up ? (s.wifi_stations ? GREEN : WHITE) : RED);
    line_text(c, 72, "%s  %s", WHITE, s.wifi_ssid, s.wifi_ip);
    line_text(c, 92, "RX %5.1f Mbit/s %5lu MB", s.wifi_rx_mbps > 0.05f ? CYAN : MUTED,
              s.wifi_rx_mbps, static_cast<unsigned long>(s.wifi_rx_mb));
}

void draw_ble(Canvas &c, const AppState &s) {
    const bool connected = std::strncmp(s.ble_state, "CONNECTED", 9) == 0;
    card(c, "BLE REMOTE", connected ? GREEN : AMBER);
    big(c, s.ble_state, connected ? GREEN : AMBER);
    if (s.ble_battery >= 0) {
        line_text(c, 72, "battery %d%%", WHITE, s.ble_battery);
    } else {
        line_text(c, 72, "AB Shutter3 clicker", MUTED);
    }
    line_text(c, 92, "last %s  (%lu)", WHITE, s.ble_last,
              static_cast<unsigned long>(s.ble_presses));
}

void draw_usb(Canvas &c, const AppState &s) {
    const bool keyboard = std::strcmp(s.usb_state, "KEYBOARD") == 0;
    card(c, "USB HOST  TYPE-A", keyboard ? GREEN : AMBER);
    big(c, s.usb_state, keyboard ? GREEN : AMBER);
    line_text(c, 72, "last key %s", WHITE, s.usb_last);
    line_text(c, 92, "%lu key presses", MUTED, static_cast<unsigned long>(s.usb_keys));
}

void draw_relay(Canvas &c, const AppState &s) {
    card(c, "RELAY  GPIO45", s.relay_on ? GREEN : MUTED);
    big(c, s.relay_on ? "ON" : "OFF", s.relay_on ? GREEN : WHITE);
    if (s.relay_on) {
        line_text(c, 72, "by %s, off in %lus", WHITE, s.relay_source,
                  static_cast<unsigned long>((s.relay_remaining_ms + 999) / 1000));
    } else {
        line_text(c, 72, "last: %s", MUTED, s.relay_source);
    }
    line_text(c, 92, "GPIO0 presses %lu", MUTED, static_cast<unsigned long>(s.button_presses));
}

void draw_sd(Canvas &c, const AppState &s) {
    card(c, "microSD LOG", s.sd_ok ? (s.sd_errors ? AMBER : GREEN) : RED);
    if (!s.sd_ok) {
        big(c, "NO CARD", RED);
        line_text(c, 72, "insert a FAT card", MUTED);
        return;
    }
    if (s.stress != StressLevel::OFF) {
        char text[24];
        std::snprintf(text, sizeof(text), "%.0f kB/s", s.sd_write_kbps);
        big(c, text, AMBER);
    } else {
        big(c, "LOGGING", GREEN);
    }
    line_text(c, 72, "%lu lines  %lu MB stress", WHITE,
              static_cast<unsigned long>(s.sd_log_lines),
              static_cast<unsigned long>(s.sd_stress_mb));
    line_text(c, 92, "%u kHz  errors %lu", s.sd_errors ? RED : MUTED, s.sd_khz,
              static_cast<unsigned long>(s.sd_errors));
}

void plot(Canvas &c, const float *values, int count, int x, int y, int w, int h,
          uint8_t color, float *out_min, float *out_max) {
    float low = values[0];
    float high = values[0];
    for (int index = 1; index < count; ++index) {
        low = std::min(low, values[index]);
        high = std::max(high, values[index]);
    }
    const float span = std::max(high - low, 0.2f);
    const float mid = (high + low) / 2;
    low = mid - span * 0.6f;
    high = mid + span * 0.6f;
    int previous_x = -1;
    int previous_y = 0;
    for (int index = 0; index < count; ++index) {
        const int px = x + w - (count - index) * w / HISTORY;
        const int py = y + h - 1 - static_cast<int>((values[index] - low) / (high - low) * (h - 1));
        if (previous_x >= 0) c.line(previous_x, previous_y, px, py, color);
        previous_x = px;
        previous_y = py;
    }
    *out_min = low;
    *out_max = high;
}

void draw_graph(Canvas &c, const AppState &s) {
    card(c, "BMP280  I2C 0x77", s.bmp_ok ? CYAN : AMBER);
    char text[64];
    if (s.bmp_ok) {
        const int32_t t = s.temp_centi_c;
        std::snprintf(text, sizeof(text), "%s%ld.%02ld C", t < 0 ? "-" : "",
                      static_cast<long>(std::abs(t) / 100), static_cast<long>(std::abs(t) % 100));
        c.text(160, 4, text, CYAN, 1);
        std::snprintf(text, sizeof(text), "%lu.%02lu hPa",
                      static_cast<unsigned long>(s.pressure_pa / 100),
                      static_cast<unsigned long>(s.pressure_pa % 100));
        c.text(270, 4, text, AMBER, 1);
    } else {
        c.text(160, 8, "sensor not found", AMBER);
    }
    const int gx = 14, gy = 30, gw = c.width - 28 - 60, gh = c.height - 42;
    c.rect(gx, gy, gw, gh, BG);
    for (int row = 1; row < 4; ++row) c.hline(gx, gy + row * gh / 4, gw, GRID);
    for (int column = 1; column < 5; ++column) c.vline(gx + column * gw / 5, gy, gh, GRID);
    const int count = std::min(history_count, HISTORY);
    if (count >= 2) {
        float low, high;
        const int start = history_count > HISTORY ? history_count % HISTORY : 0;
        static float t_lin[HISTORY];
        static float p_lin[HISTORY];
        for (int index = 0; index < count; ++index) {
            t_lin[index] = temperature_history[(start + index) % HISTORY];
            p_lin[index] = pressure_history[(start + index) % HISTORY];
        }
        plot(c, p_lin, count, gx, gy, gw, gh, AMBER, &low, &high);
        std::snprintf(text, sizeof(text), "%.1f", high);
        c.text(gx + gw + 6, gy + 48, text, AMBER);
        std::snprintf(text, sizeof(text), "%.1f", low);
        c.text(gx + gw + 6, gy + gh - 16, text, AMBER);
        plot(c, t_lin, count, gx, gy, gw, gh, CYAN, &low, &high);
        std::snprintf(text, sizeof(text), "%.2f", high);
        c.text(gx + gw + 6, gy, text, CYAN);
        std::snprintf(text, sizeof(text), "%.2f", low);
        c.text(gx + gw + 6, gy + gh - 36, text, CYAN);
    }
    c.text(c.width - 120, 4, "last 250 s", MUTED);
}

void draw_health(Canvas &c, const vga::Stats &v) {
    const bool clean = v.late == 0 && v.irregular_frames == 0;
    card(c, "VGA REFILL HEALTH", clean ? GREEN : (v.irregular_frames ? RED : AMBER));
    char text[32];
    std::snprintf(text, sizeof(text), "LATE %lu", static_cast<unsigned long>(v.late));
    c.text(14, 26, text, clean ? GREEN : AMBER, 2);
    line_text(c, 62, "irregular frames %lu", v.irregular_frames ? RED : WHITE,
              static_cast<unsigned long>(v.irregular_frames));
    line_text(c, 80, "ISR max %.0f / %.0f us", WHITE, v.max_exec_us, v.budget_us);
    line_text(c, 98, "min slack %.0f us", v.min_slack_us < 0 ? RED : MUTED, v.min_slack_us);
    // Late refills per second, last 60 s: one 4-px column per second.
    const int bx = 14, by = c.height - 30, bh = 24;
    c.rect(bx, by, 240, bh, BG);
    const int count = std::min(late_count, 60);
    for (int index = 0; index < count; ++index) {
        const int slot = (late_count - count + index) % 60;
        const int x = bx + (60 - count + index) * 4;
        if (irregular_history[slot]) {
            c.rect(x, by, 3, bh, RED);
        } else if (late_history[slot]) {
            const int height = std::min<int>(bh, 4 + late_history[slot] * 2);
            c.rect(x, by + bh - height, 3, height, AMBER);
        } else {
            c.rect(x, by + bh - 2, 3, 2, DIM_GREEN);
        }
    }
}

void draw_events(Canvas &c, const AppState &s) {
    card(c, "EVENTS", BLUE);
    for (int index = 0; index < 4; ++index) {
        c.text(14, 28 + index * 19, s.events[index], index == 0 ? WHITE : MUTED);
    }
}

void draw_system(Canvas &c, const AppState &s, const vga::Stats &v) {
    card(c, "SYSTEM", MAGENTA);
    line_text(c, 28, "CPU0 %3u%%   CPU1 %3u%%", WHITE, s.cpu_load[0], s.cpu_load[1]);
    line_text(c, 46, "SRAM %luk free, min %luk", s.heap_internal_min < 20000 ? AMBER : WHITE,
              static_cast<unsigned long>(s.heap_internal_free / 1024),
              static_cast<unsigned long>(s.heap_internal_min / 1024));
    line_text(c, 64, "PSRAM %.1f/%.0f MB free", WHITE, s.psram_free / 1048576.0f,
              s.psram_size / 1048576.0f);
    line_text(c, 82, "flash %lu MB  L=%d  %lu fr", MUTED,
              static_cast<unsigned long>(s.flash_size / 1048576), v.bounce_lines,
              static_cast<unsigned long>(v.vsyncs));
}

void draw_footer(Canvas &c) {
    c.fill(BG);
    c.text(8, 3, "USB: R relay S stress X envelope C clear M mark | "
                 "BLE: big=stress small=relay | GPIO0: relay", MUTED);
}

// ------------------------------------------------------------------ loop
void sample_history(const AppState &s, const vga::Stats &v) {
    static uint32_t last_late = 0;
    static uint32_t last_irregular = 0;
    const int slot = history_count % HISTORY;
    temperature_history[slot] = s.bmp_ok ? s.temp_centi_c / 100.0f
        : (history_count ? temperature_history[(slot + HISTORY - 1) % HISTORY] : 0);
    pressure_history[slot] = s.bmp_ok ? s.pressure_pa / 100.0f
        : (history_count ? pressure_history[(slot + HISTORY - 1) % HISTORY] : 0);
    ++history_count;
    if (v.late < last_late || v.irregular_frames < last_irregular) {
        last_late = 0;   // counters were cleared
        last_irregular = 0;
    }
    late_history[late_count % 60] = static_cast<uint16_t>(std::min<uint32_t>(v.late - last_late, 999));
    irregular_history[late_count % 60] =
        static_cast<uint16_t>(std::min<uint32_t>(v.irregular_frames - last_irregular, 999));
    ++late_count;
    last_late = v.late;
    last_irregular = v.irregular_frames;
}

void dashboard_task(void *) {
    uint32_t tick = 0;
    int64_t next_sample = esp_timer_get_time();
    while (true) {
        const AppState s = app_state_snapshot();
        const vga::Stats v = vga::stats();
        const bool stress = s.stress != StressLevel::OFF;
        if (esp_timer_get_time() >= next_sample) {
            sample_history(s, v);
            next_sample += 1000000;
        }

        draw_header(panels[HEADER].canvas, s, v, tick);
        draw_eth(panels[ETH].canvas, s);
        draw_wifi(panels[WIFI].canvas, s);
        draw_ble(panels[BLE].canvas, s);
        draw_usb(panels[USB].canvas, s);
        draw_relay(panels[RELAY].canvas, s);
        draw_sd(panels[SD].canvas, s);
        draw_graph(panels[GRAPH].canvas, s);
        draw_health(panels[HEALTH].canvas, v);
        draw_events(panels[EVENTS].canvas, s);
        draw_system(panels[SYSTEM].canvas, s, v);

        for (Panel &panel : panels) {
            if (&panel == &panels[FOOTER]) continue;
            const uint32_t hash = panel.canvas.hash();
            if (stress || hash != panel.last_hash) {
                panel.canvas.blit_to(screen, panel.x, panel.y);
                panel.last_hash = hash;
            }
        }
        ++tick;
        if (stress) {
            vga::wait_vsync(50);   // recompose and copy every frame
        } else {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

}  // namespace

bool dashboard_start() {
    vga::set_palette(PALETTE);
    screen.pixels = vga::framebuffer();
    screen.width = vga::WIDTH;
    screen.height = vga::HEIGHT;
    screen.stride = vga::STRIDE;
    if (screen.pixels == nullptr) return false;
    screen.fill(BG);
    for (Panel &panel : panels) {
        if (!panel.canvas.allocate(panel.w, panel.h)) {
            ESP_LOGE(TAG, "No PSRAM for a %dx%d panel", panel.w, panel.h);
            return false;
        }
    }
    draw_footer(panels[FOOTER].canvas);
    panels[FOOTER].canvas.blit_to(screen, panels[FOOTER].x, panels[FOOTER].y);
    // Priority 2 on core 1: below every driver task, above idle. The refill
    // interrupt preempts it, so the two never touch PSRAM at the same time.
    return spawn(dashboard_task, "dashboard", 6144, nullptr, 2, 1);
}
