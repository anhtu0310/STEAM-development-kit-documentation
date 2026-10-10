#include "vga4.h"

#include <climits>
#include <cstring>

#include "board.h"

#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_private/esp_clk.h"
#include "esp_private/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/lcd_periph.h"
#include "esp32s3/rom/cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace vga {
namespace {

constexpr char TAG[] = "sptw_vga";

// Timing of STEAM_VGA_IDF54_RAW16_OLED, the one already proven on the HP
// monitor: 36 MHz, 1022 x 623 total, positive syncs, about 56.5 Hz.
constexpr uint32_t PIXEL_CLOCK_HZ = 36'000'000;
constexpr int H_FRONT = 22;
constexpr int H_SYNC = 72;
constexpr int H_BACK = 128;
constexpr int V_FRONT = 1;
constexpr int V_SYNC = 2;
constexpr int V_BACK = 20;
constexpr int H_TOTAL = WIDTH + H_FRONT + H_SYNC + H_BACK;
constexpr int V_TOTAL = HEIGHT + V_FRONT + V_SYNC + V_BACK;

constexpr int BOUNCE_LINES = CONFIG_SPTW_BOUNCE_LINES;
static_assert(HEIGHT % BOUNCE_LINES == 0, "600 must be divisible by SPTW_BOUNCE_LINES");
constexpr int CHUNKS = HEIGHT / BOUNCE_LINES;
constexpr int BOUNCE_PIXELS = WIDTH * BOUNCE_LINES;
constexpr size_t FB_BYTES = static_cast<size_t>(STRIDE) * HEIGHT;

uint8_t *fb = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t vsync_sem = nullptr;
SemaphoreHandle_t started_sem = nullptr;
bool start_ok = false;

// Everything below is read by the refill ISR and must live in internal RAM.
DRAM_ATTR uint32_t pair_lut[256];          // byte -> two RGB565 pixels
DRAM_ATTR uint16_t palette565[16];
DRAM_ATTR volatile bool palette_dirty = false;
DRAM_ATTR uint8_t *isr_fb = nullptr;

// ---------------------------------------------------------------- statistics
// Late-refill definition (E5): refills within a frame are triggered every
// T_bb = L x line period. Fit the frame's trigger grid to the least-delayed
// timed refill; refill i is late when it finishes after base + (i + 1) T_bb.
// Refills 0 and 1 run in vertical blanking with >= 23 lines of slack and are
// counted but not timed. A lower bound, like the bench.
struct RawStats {
    uint32_t refills;
    uint32_t late;
    uint32_t irregular_frames;
    uint32_t frames;
    uint32_t max_exec_cycles;
    uint32_t max_late_cycles;
    int32_t min_slack_cycles;
};

DRAM_ATTR RawStats raw = {0, 0, 0, 0, 0, 0, INT32_MAX};
DRAM_ATTR uint32_t vsync_count = 0;
DRAM_ATTR uint32_t entry_cycles[CHUNKS];
DRAM_ATTR uint32_t exit_cycles[CHUNKS];
DRAM_ATTR uint8_t chunk_seen[CHUNKS];
DRAM_ATTR int callbacks_this_frame = 0;
DRAM_ATTR bool frame_open = false;
DRAM_ATTR int32_t bounce_period_cycles = 0;
portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;

IRAM_ATTR void finalize_frame() {
    raw.frames++;
    if (callbacks_this_frame != CHUNKS) raw.irregular_frames++;

    int reference = -1;
    for (int index = 2; index < CHUNKS; ++index) {
        if (chunk_seen[index]) {
            reference = index;
            break;
        }
    }
    if (reference >= 0) {
        const uint32_t origin = entry_cycles[reference];
        int32_t base = INT32_MAX;
        for (int index = reference; index < CHUNKS; ++index) {
            if (!chunk_seen[index]) continue;
            const int32_t relative =
                static_cast<int32_t>(entry_cycles[index] - origin) -
                index * bounce_period_cycles;
            if (relative < base) base = relative;
        }
        for (int index = reference; index < CHUNKS; ++index) {
            if (!chunk_seen[index]) continue;
            const int32_t deadline = base + (index + 1) * bounce_period_cycles;
            const int32_t finished = static_cast<int32_t>(exit_cycles[index] - origin);
            const int32_t slack = deadline - finished;
            if (slack < raw.min_slack_cycles) raw.min_slack_cycles = slack;
            if (slack < 0) {
                raw.late++;
                if (static_cast<uint32_t>(-slack) > raw.max_late_cycles) {
                    raw.max_late_cycles = static_cast<uint32_t>(-slack);
                }
            }
        }
    }
    for (int index = 0; index < CHUNKS; ++index) chunk_seen[index] = 0;
    callbacks_this_frame = 0;
}

IRAM_ATTR void record_refill(int position_pixels, uint32_t entry, uint32_t exit) {
    const int index = position_pixels / BOUNCE_PIXELS;
    portENTER_CRITICAL_ISR(&stats_lock);
    if (index == 0) {
        if (frame_open) finalize_frame();
        frame_open = true;
    }
    if (frame_open && index >= 0 && index < CHUNKS) {
        entry_cycles[index] = entry;
        exit_cycles[index] = exit;
        chunk_seen[index] = 1;
        callbacks_this_frame++;
    }
    raw.refills++;
    const uint32_t exec = exit - entry;
    if (exec > raw.max_exec_cycles) raw.max_exec_cycles = exec;
    portEXIT_CRITICAL_ISR(&stats_lock);
}

IRAM_ATTR void rebuild_lut() {
    for (int value = 0; value < 256; ++value) {
        pair_lut[value] = static_cast<uint32_t>(palette565[value & 0x0F]) |
                          (static_cast<uint32_t>(palette565[value >> 4]) << 16);
    }
    palette_dirty = false;
}

// ---------------------------------------------------------------- callbacks
// Runs on core 1 inside the GDMA EOF interrupt. Reads L x 400 bytes of PSRAM
// (8 kB at L = 20) and writes L x 1600 bytes of internal SRAM.
bool IRAM_ATTR on_bounce_empty(esp_lcd_panel_handle_t, void *bounce,
                               int position_pixels, int length_bytes, void *) {
    const uint32_t entry = esp_cpu_get_cycle_count();
    if (position_pixels == 0 && palette_dirty) rebuild_lut();

    const uint32_t *source =
        reinterpret_cast<const uint32_t *>(isr_fb + position_pixels / 2);
    uint32_t *target = static_cast<uint32_t *>(bounce);
    const uint32_t *lut = pair_lut;
    for (int words = length_bytes / 16; words > 0; --words) {
        const uint32_t packed = *source++;   // 8 pixels
        target[0] = lut[packed & 0xFF];
        target[1] = lut[(packed >> 8) & 0xFF];
        target[2] = lut[(packed >> 16) & 0xFF];
        target[3] = lut[packed >> 24];
        target += 4;
    }

    // Start fetching the next chunk into the data cache while the DMA sends
    // this one, as the driver does for its own PSRAM frame buffers.
    int next = position_pixels + length_bytes / 2;
    if (next >= WIDTH * HEIGHT) next = 0;
    if (Cache_DCache_Preload_Done()) {
        Cache_Start_DCache_Preload(reinterpret_cast<uint32_t>(isr_fb + next / 2),
                                   static_cast<uint32_t>(length_bytes / 4), 0);
    }

    record_refill(position_pixels, entry, esp_cpu_get_cycle_count());
    return false;
}

bool IRAM_ATTR on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t *,
                        void *) {
    vsync_count++;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(vsync_sem, &woken);
    return woken == pdTRUE;
}

// ---------------------------------------------------------------- start-up
void init_task(void *) {
    esp_lcd_rgb_panel_config_t config = {};
    config.clk_src = LCD_CLK_SRC_PLL240M;
    config.timings.pclk_hz = PIXEL_CLOCK_HZ;
    config.timings.h_res = WIDTH;
    config.timings.v_res = HEIGHT;
    config.timings.hsync_pulse_width = H_SYNC;
    config.timings.hsync_back_porch = H_BACK;
    config.timings.hsync_front_porch = H_FRONT;
    config.timings.vsync_pulse_width = V_SYNC;
    config.timings.vsync_back_porch = V_BACK;
    config.timings.vsync_front_porch = V_FRONT;
    config.timings.flags.hsync_idle_low = 1;   // positive HSYNC
    config.timings.flags.vsync_idle_low = 1;   // positive VSYNC
    config.timings.flags.pclk_active_neg = 0;
    config.data_width = 16;
    config.bits_per_pixel = 16;
    config.num_fbs = 0;
    config.bounce_buffer_size_px = BOUNCE_PIXELS;
    config.dma_burst_size = 64;
    config.hsync_gpio_num = board::VGA_HSYNC;
    config.vsync_gpio_num = board::VGA_VSYNC;
    config.de_gpio_num = -1;
    config.pclk_gpio_num = -1;
    config.disp_gpio_num = -1;
    for (int bit = 0; bit < 16; ++bit) config.data_gpio_nums[bit] = board::VGA_DATA[bit];
    config.flags.no_fb = 1;

    esp_err_t error = esp_lcd_new_rgb_panel(&config, &panel);
    if (error == ESP_OK) {
        esp_lcd_rgb_panel_event_callbacks_t callbacks = {};
        callbacks.on_bounce_empty = on_bounce_empty;
        callbacks.on_vsync = on_vsync;
        error = esp_lcd_rgb_panel_register_event_callbacks(panel, &callbacks, nullptr);
    }
    if (error == ESP_OK) error = esp_lcd_panel_reset(panel);
    if (error == ESP_OK) error = esp_lcd_panel_init(panel);   // starts streaming
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel start failed: %s", esp_err_to_name(error));
    } else {
        ESP_LOGI(TAG, "VGA 800x600 4bpp, %.2f MHz, %d-line bounce buffers on core %d",
                 PIXEL_CLOCK_HZ / 1e6, BOUNCE_LINES, xPortGetCoreID());
    }
    start_ok = error == ESP_OK;
    xSemaphoreGive(started_sem);
    vTaskDelete(nullptr);
}

constexpr uint16_t to565(uint32_t rgb) {
    const uint32_t red = (rgb >> 16) & 0xFF;
    const uint32_t green = (rgb >> 8) & 0xFF;
    const uint32_t blue = rgb & 0xFF;
    return static_cast<uint16_t>((red >> 3) | ((green >> 2) << 5) | ((blue >> 3) << 11));
}

}  // namespace

bool start() {
    fb = static_cast<uint8_t *>(heap_caps_aligned_calloc(
        64, 1, FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fb == nullptr) {
        ESP_LOGE(TAG, "No PSRAM for the %u-byte frame buffer", static_cast<unsigned>(FB_BYTES));
        return false;
    }
    isr_fb = fb;
    if (palette_dirty) rebuild_lut();

    const double line_s = static_cast<double>(H_TOTAL) / PIXEL_CLOCK_HZ;
    bounce_period_cycles = static_cast<int32_t>(
        BOUNCE_LINES * line_s * static_cast<double>(esp_clk_cpu_freq()));

    vsync_sem = xSemaphoreCreateBinary();
    started_sem = xSemaphoreCreateBinary();
    if (vsync_sem == nullptr || started_sem == nullptr) return false;
    // Both LCD interrupts (GDMA EOF and VSYNC) are allocated on the core that
    // creates the panel, so this task decides where the refills run.
    if (xTaskCreatePinnedToCore(init_task, "vga_init", 4096, nullptr, 20,
                                nullptr, 1) != pdPASS) {
        return false;
    }
    xSemaphoreTake(started_sem, portMAX_DELAY);
    return start_ok;
}

uint8_t *framebuffer() { return fb; }

void set_palette(const uint32_t rgb888[16]) {
    for (int index = 0; index < 16; ++index) palette565[index] = to565(rgb888[index]);
    palette_dirty = true;
}

Stats stats() {
    RawStats copy;
    uint32_t vsyncs;
    portENTER_CRITICAL(&stats_lock);
    copy = raw;
    vsyncs = vsync_count;
    portEXIT_CRITICAL(&stats_lock);
    const float per_us = static_cast<float>(esp_clk_cpu_freq()) / 1e6f;
    Stats result = {};
    result.refills = copy.refills;
    result.late = copy.late;
    result.irregular_frames = copy.irregular_frames;
    result.frames = copy.frames;
    result.vsyncs = vsyncs;
    result.max_exec_us = copy.max_exec_cycles / per_us;
    result.max_late_us = copy.max_late_cycles / per_us;
    result.min_slack_us = copy.min_slack_cycles == INT32_MAX
        ? 0.0f : copy.min_slack_cycles / per_us;
    result.budget_us = bounce_period_cycles / per_us;
    result.bounce_lines = BOUNCE_LINES;
    result.refresh_hz = static_cast<float>(PIXEL_CLOCK_HZ) / (H_TOTAL * V_TOTAL);
    result.pixel_clock_hz = PIXEL_CLOCK_HZ;
    return result;
}

void clear_stats() {
    portENTER_CRITICAL(&stats_lock);
    const uint32_t vsyncs = vsync_count;
    raw = {0, 0, 0, 0, 0, 0, INT32_MAX};
    frame_open = false;
    callbacks_this_frame = 0;
    for (int index = 0; index < CHUNKS; ++index) chunk_seen[index] = 0;
    vsync_count = vsyncs;
    portEXIT_CRITICAL(&stats_lock);
}

void attach_pins(const int *gpios, int count) {
    const auto &signals = lcd_periph_rgb_signals.panels[0];
    for (int index = 0; index < count; ++index) {
        const int gpio = gpios[index];
        int signal = -1;
        if (gpio == board::VGA_HSYNC) signal = signals.hsync_sig;
        if (gpio == board::VGA_VSYNC) signal = signals.vsync_sig;
        for (int bit = 0; bit < 16; ++bit) {
            if (board::VGA_DATA[bit] == gpio) signal = signals.data_sigs[bit];
        }
        if (signal < 0) continue;
        const auto pin = static_cast<gpio_num_t>(gpio);
        gpio_set_pull_mode(pin, GPIO_FLOATING);
        gpio_func_sel(pin, PIN_FUNC_GPIO);
        esp_rom_gpio_connect_out_signal(gpio, signal, false, false);   // also enables the output
    }
}

bool wait_vsync(uint32_t timeout_ms) {
    return xSemaphoreTake(vsync_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

}  // namespace vga
