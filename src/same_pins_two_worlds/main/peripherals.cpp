#include "peripherals.h"

#include <cstdio>
#include <cstring>

#include "board.h"
#include "font5x7.h"
#include "pins.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "sdkconfig.h"

namespace periph {
namespace {

constexpr char TAG[] = "sptw_periph";

constexpr ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t SEG_TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t SEG_CHANNEL = LEDC_CHANNEL_0;
constexpr ledc_timer_t BUZZER_TIMER = LEDC_TIMER_1;
constexpr ledc_channel_t BUZZER_CHANNEL = LEDC_CHANNEL_1;

constexpr int WS_COUNT = CONFIG_SPTW_WS2812_COUNT;
constexpr uint8_t WS_MAX = 40;   // per channel, keeps the 5 V rail quiet

// Active-high segment patterns, bit order DP G F E D C B A (as the kit's
// 7seg.ino example). The polarity on the wire is set in menuconfig.
constexpr uint8_t SEG_DIGITS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
constexpr uint8_t SEG_G = 0x3D;
constexpr uint8_t SEG_P = 0x73;
constexpr uint8_t SEG_BLANK = 0x00;

rmt_channel_handle_t ws_channel = nullptr;
rmt_encoder_handle_t ws_encoder = nullptr;
bool claimed = false;
uint8_t brightness = 100;

// Matrix scroller: column bitmaps of the spotlight sentence.
uint8_t columns[512];
int column_count = 0;
int column_offset = 0;
int shown_spotlight = -1;
uint32_t next_scroll_ms = 0;
uint32_t next_ws_ms = 0;

struct Note { uint16_t hz; uint16_t ms; };
constexpr Note CHIRP[] = {{1047, 70}, {1319, 70}, {1568, 110}};
constexpr Note TUNE[] = {{523, 160}, {587, 160}, {659, 160}, {698, 160}, {784, 320},
                         {0, 80}, {784, 160}, {659, 320}};
const Note *melody = nullptr;
int melody_length = 0;
int melody_index = 0;
uint32_t note_end_ms = 0;
bool note_playing = false;

void pulse(gpio_num_t pin) {
    gpio_set_level(pin, 1);
    esp_rom_delay_us(1);
    gpio_set_level(pin, 0);
}

// ---------------------------------------------------------------- 7-segment
void seg_shift(uint8_t pattern) {
#if CONFIG_SPTW_SEG_ACTIVE_LOW
    pattern = static_cast<uint8_t>(~pattern);
#endif
    for (int bit = 7; bit >= 0; --bit) {
        gpio_set_level(board::SEG_DATA, (pattern >> bit) & 1U);
        pulse(board::SEG_CLOCK);
    }
}

// digits[0] is the leftmost position. The first byte shifted ends in the
// last 74HC595 of the chain, so the rightmost digit goes first.
void seg_show(const uint8_t digits[4]) {
    gpio_set_level(board::SEG_LATCH, 0);
    for (int index = 0; index < 4; ++index) {
#if CONFIG_SPTW_SEG_REVERSE_DIGITS
        seg_shift(digits[index]);
#else
        seg_shift(digits[3 - index]);
#endif
    }
    pulse(board::SEG_LATCH);
}

void seg_show_gpio(int gpio) {
    uint8_t digits[4] = {SEG_G, SEG_P, SEG_BLANK, SEG_DIGITS[gpio % 10]};
    if (gpio >= 10) digits[2] = SEG_DIGITS[(gpio / 10) % 10];
    seg_show(digits);
}

void seg_brightness(uint8_t percent) {
    // OE is active low: a longer high time means a dimmer display.
    const uint32_t off = 255U - percent * 255U / 100U;
    ledc_set_duty(LEDC_MODE, SEG_CHANNEL, off);
    ledc_update_duty(LEDC_MODE, SEG_CHANNEL);
}

// ---------------------------------------------------------------- MAX7219
void max_write(uint8_t address, uint8_t value) {
    gpio_set_level(board::MAX_CS, 0);
    const uint16_t word = static_cast<uint16_t>((address << 8) | value);
    for (int bit = 15; bit >= 0; --bit) {
        gpio_set_level(board::MAX_DIN, (word >> bit) & 1U);
        pulse(board::MAX_CLK);
    }
    gpio_set_level(board::MAX_CS, 1);
}

// A full re-initialisation: after a fault demo the chip may be in shutdown,
// display test, BCD decode or a short scan limit.
void max_init() {
    max_write(0x0F, 0x00);   // display test off
    max_write(0x0C, 0x01);   // normal operation
    max_write(0x0B, 0x07);   // scan all eight rows
    max_write(0x09, 0x00);   // no decode
    max_write(0x0A, CONFIG_SPTW_MATRIX_INTENSITY);
    for (uint8_t row = 1; row <= 8; ++row) max_write(row, 0);
}

void max_show_window() {
    for (int row = 0; row < 8; ++row) {
        uint8_t value = 0;
        for (int column = 0; column < 8; ++column) {
            const uint8_t bits = columns[(column_offset + column) % column_count];
            if (bits & (1U << row)) value |= static_cast<uint8_t>(0x80U >> column);
        }
        max_write(static_cast<uint8_t>(row + 1), value);
    }
}

void build_sentence(int spotlight) {
    const pins::Role &role = pins::spotlight(spotlight);
    char text[96];
    std::snprintf(text, sizeof(text), "GPIO%d  VGA %s  NOW %s   ", role.gpio, role.vga,
                  role.world_b);
    column_count = 0;
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        const uint8_t *glyph = steam_font::glyph(*cursor);
        for (int column = 0; column < 5 && column_count < static_cast<int>(sizeof(columns)); ++column) {
            columns[column_count++] = glyph[column];
        }
        if (column_count < static_cast<int>(sizeof(columns))) columns[column_count++] = 0;
    }
    column_offset = 0;
}

// ---------------------------------------------------------------- WS2812B
bool ws_start() {
    rmt_tx_channel_config_t channel = {};
    channel.clk_src = RMT_CLK_SRC_DEFAULT;
    channel.gpio_num = board::WS2812;
    channel.mem_block_symbols = 64;
    channel.resolution_hz = 10'000'000;
    channel.trans_queue_depth = 2;
    if (rmt_new_tx_channel(&channel, &ws_channel) != ESP_OK) return false;
    rmt_bytes_encoder_config_t bytes = {};
    bytes.bit0 = {3, 1, 9, 0};   // 0.3 us high, 0.9 us low
    bytes.bit1 = {9, 1, 3, 0};   // 0.9 us high, 0.3 us low
    bytes.flags.msb_first = 1;
    if (rmt_new_bytes_encoder(&bytes, &ws_encoder) != ESP_OK) return false;
    return rmt_enable(ws_channel) == ESP_OK;
}

void ws_show_all(const uint8_t rgb[][3]) {
    if (ws_channel == nullptr || ws_encoder == nullptr) return;
    uint8_t grb[WS_COUNT * 3];
    for (int led = 0; led < WS_COUNT; ++led) {
        grb[led * 3] = rgb[led][1];
        grb[led * 3 + 1] = rgb[led][0];
        grb[led * 3 + 2] = rgb[led][2];
    }
    rmt_transmit_config_t transmit = {};
    if (rmt_transmit(ws_channel, ws_encoder, grb, sizeof(grb), &transmit) == ESP_OK) {
        rmt_tx_wait_all_done(ws_channel, 50);
        esp_rom_delay_us(300);   // reset gap (WS2812B needs > 280 us)
    }
}

void ws_stop() {
    if (ws_channel != nullptr) {
        uint8_t off[WS_COUNT][3] = {};
        ws_show_all(off);
        rmt_disable(ws_channel);
        rmt_del_channel(ws_channel);
        ws_channel = nullptr;
    }
    if (ws_encoder != nullptr) {
        rmt_del_encoder(ws_encoder);
        ws_encoder = nullptr;
    }
}

void wheel(uint32_t phase, uint8_t out[3]) {
    phase %= 768;
    const uint8_t step = static_cast<uint8_t>(phase % 256);
    const uint8_t up = static_cast<uint8_t>(step * WS_MAX / 255);
    const uint8_t down = static_cast<uint8_t>(WS_MAX - up);
    if (phase < 256) { out[0] = down; out[1] = up; out[2] = 0; }
    else if (phase < 512) { out[0] = 0; out[1] = down; out[2] = up; }
    else { out[0] = up; out[1] = 0; out[2] = down; }
}

// ---------------------------------------------------------------- buzzer
void buzzer(uint16_t hz) {
    if (hz == 0) {
        ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, 0);
    } else {
        ledc_set_freq(LEDC_MODE, BUZZER_TIMER, hz);
        ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, 512);
    }
    ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL);
}

void play(const Note *notes, int count) {
    melody = notes;
    melody_length = count;
    melody_index = 0;
    note_playing = false;
}

void melody_tick(uint32_t now) {
    if (melody == nullptr) return;
    if (note_playing && static_cast<int32_t>(now - note_end_ms) < 0) return;
    if (melody_index >= melody_length) {
        buzzer(0);
        melody = nullptr;
        note_playing = false;
        return;
    }
    const Note &note = melody[melody_index++];
    buzzer(note.hz);
    note_end_ms = now + note.ms;
    note_playing = true;
}

void attach_ledc(gpio_num_t pin, ledc_timer_t timer, ledc_channel_t channel, uint32_t duty) {
    ledc_channel_config_t config = {};
    config.gpio_num = pin;
    config.speed_mode = LEDC_MODE;
    config.channel = channel;
    config.timer_sel = timer;
    config.duty = duty;
    ledc_channel_config(&config);
}

}  // namespace

bool init_once() {
    ledc_timer_config_t seg = {};
    seg.speed_mode = LEDC_MODE;
    seg.duty_resolution = LEDC_TIMER_8_BIT;
    seg.timer_num = SEG_TIMER;
    seg.freq_hz = 5000;
    seg.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config_t tone = seg;
    tone.duty_resolution = LEDC_TIMER_10_BIT;
    tone.timer_num = BUZZER_TIMER;
    tone.freq_hz = 1000;
    return ledc_timer_config(&seg) == ESP_OK && ledc_timer_config(&tone) == ESP_OK;
}

void park() {
    for (int index = 0; index < board::SHARED_COUNT; ++index) {
        gpio_reset_pin(static_cast<gpio_num_t>(board::SHARED[index]));
    }
    gpio_reset_pin(board::BUTTON);
}

void claim() {
    if (claimed) return;
    // Idle levels first, then outputs, so no pin glitches into a clock edge.
    gpio_set_level(board::SEG_LATCH, 0);
    gpio_set_level(board::SEG_CLOCK, 0);
    gpio_set_level(board::SEG_DATA, 0);
    gpio_set_level(board::SEG_RESET_N, 1);
    gpio_set_level(board::MAX_CS, 1);
    gpio_set_level(board::MAX_CLK, 0);
    gpio_set_level(board::MAX_DIN, 0);
    gpio_set_level(board::LED, 0);
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << board::SEG_LATCH) | (1ULL << board::SEG_CLOCK) |
                           (1ULL << board::SEG_DATA) | (1ULL << board::SEG_RESET_N) |
                           (1ULL << board::MAX_CS) | (1ULL << board::MAX_CLK) |
                           (1ULL << board::MAX_DIN) | (1ULL << board::LED);
    outputs.mode = GPIO_MODE_OUTPUT;
    gpio_config(&outputs);

    gpio_config_t inputs = {};
    for (gpio_num_t pin : board::INPUTS) inputs.pin_bit_mask |= 1ULL << pin;
    inputs.mode = GPIO_MODE_INPUT;
    inputs.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&inputs);

    attach_ledc(board::SEG_OE_N, SEG_TIMER, SEG_CHANNEL, 255);   // blank first
    attach_ledc(board::BUZZER, BUZZER_TIMER, BUZZER_CHANNEL, 0);
    if (!ws_start()) ESP_LOGW(TAG, "WS2812B RMT channel failed");

    max_init();
    shown_spotlight = -1;
    seg_brightness(brightness);
    play(CHIRP, sizeof(CHIRP) / sizeof(CHIRP[0]));
    claimed = true;
    ESP_LOGI(TAG, "World B: shared pins claimed");
}

void release() {
    if (claimed) {
        melody = nullptr;
        buzzer(0);
        ledc_stop(LEDC_MODE, BUZZER_CHANNEL, 0);
        ws_stop();
        for (uint8_t row = 1; row <= 8; ++row) max_write(row, 0);
        max_write(0x0C, 0x00);   // shutdown
        const uint8_t blank[4] = {SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK};
        seg_show(blank);
        seg_brightness(0);
        ledc_stop(LEDC_MODE, SEG_CHANNEL, 1);   // OE high = blank
        gpio_set_level(board::LED, 0);
        claimed = false;
        ESP_LOGI(TAG, "World B: outputs off, pins released");
    }
    park();
}

void tick(uint32_t now, const View &view) {
    if (!claimed) return;
    melody_tick(now);
    if (view.spotlight != shown_spotlight) {
        shown_spotlight = view.spotlight;
        seg_show_gpio(pins::spotlight(view.spotlight).gpio);
        build_sentence(view.spotlight);
        max_show_window();
        next_scroll_ms = now + 400;
    }
    if (static_cast<int32_t>(now - next_scroll_ms) >= 0 && column_count > 0) {
        column_offset = (column_offset + 1) % column_count;
        max_show_window();
        next_scroll_ms = now + 80;
    }
    if (static_cast<int32_t>(now - next_ws_ms) >= 0) {
        uint8_t colours[WS_COUNT][3];
        for (int led = 0; led < WS_COUNT; ++led) wheel(now / 4 + led * 256, colours[led]);
        ws_show_all(colours);
        next_ws_ms = now + 40;
    }
    gpio_set_level(board::LED, (now % 1000) < 120 ? 1 : 0);
}

uint8_t read_inputs() {
    if (!claimed) return 0;
    uint8_t pressed = 0;
    for (int index = 0; index < 4; ++index) {
        if (gpio_get_level(board::INPUTS[index]) == 0) pressed |= static_cast<uint8_t>(1U << index);
    }
    return pressed;
}

void tune_start() {
    if (claimed) play(TUNE, sizeof(TUNE) / sizeof(TUNE[0]));
}

uint8_t brightness_next() {
    brightness = brightness >= 100 ? 15 : (brightness < 40 ? 50 : 100);
    if (claimed) seg_brightness(brightness);
    return brightness;
}

}  // namespace periph
