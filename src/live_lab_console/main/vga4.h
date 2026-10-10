#pragma once

// 800x600 VGA out of the ESP32-S3 RGB LCD peripheral, built for running next
// to Wi-Fi, BLE, Ethernet, USB host and SD at the same time.
//
// The frame buffer is 4 bits per pixel (16-colour palette, 240 000 bytes in
// PSRAM). The bounce-buffer interrupt expands it to RGB565 through a 256-entry
// pair table, so VGA reads 18 MB/s from PSRAM instead of 72 MB/s. The panel
// and both of its interrupts are created on core 1; nothing else that is
// timing-critical runs there. See VGA_STABILITY.md for the reasoning.

#include <cstdint>

namespace vga {

constexpr int WIDTH = 800;
constexpr int HEIGHT = 600;
constexpr int STRIDE = WIDTH / 2;   // bytes per frame-buffer row

// Live refill statistics, same definitions as E5_E8_E9_vga_bench.
struct Stats {
    uint32_t refills;            // bounce-buffer refills since the last clear
    uint32_t late;               // refills that finished after their deadline
    uint32_t irregular_frames;   // frames with != 600/L refills (EOF lost or merged)
    uint32_t frames;             // frames seen by the refill bookkeeping
    uint32_t vsyncs;             // VSYNC interrupts (frame counter)
    float max_exec_us;           // longest refill
    float max_late_us;           // worst lateness (0 if none late)
    float min_slack_us;          // smallest margin before a deadline
    float budget_us;             // T_bb = L x line period
    int bounce_lines;
    float refresh_hz;
    uint32_t pixel_clock_hz;
};

// Allocates the frame buffer, then creates and starts the panel from a task
// pinned to core 1. Returns false (and logs why) on failure.
bool start();

// 4bpp frame buffer: byte = (right pixel << 4) | left pixel.
uint8_t *framebuffer();

// RGB888 palette entries, converted to the STEAM BBBBBGGGGGGRRRRR order and
// applied at the start of the next frame.
void set_palette(const uint32_t rgb888[16]);

Stats stats();
void clear_stats();

// Blocks until the next VSYNC (or the timeout); returns false on timeout.
bool wait_vsync(uint32_t timeout_ms);

}  // namespace vga
