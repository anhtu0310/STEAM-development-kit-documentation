#include "sensors.h"

#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "board.h"
#include "font5x7.h"
#include "vga4.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char TAG[] = "llc_i2c";
constexpr int OLED_WIDTH = 128;
constexpr int OLED_PAGES = 8;

i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t oled = nullptr;
i2c_master_dev_handle_t bmp = nullptr;
uint8_t oled_pixels[OLED_WIDTH * OLED_PAGES];

// ------------------------------------------------------------ SH1106 OLED
bool oled_init() {
    if (i2c_master_probe(bus, 0x3C, 50) != ESP_OK) return false;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = 0x3C;
    device.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &device, &oled) != ESP_OK) return false;
    static constexpr uint8_t sequence[] = {
        0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xAD, 0x8B,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0x22, 0xDB, 0x35, 0xA4, 0xA6, 0xAF};
    return i2c_master_transmit(oled, sequence, sizeof(sequence), 100) == ESP_OK;
}

void oled_text(int page, const char *value) {
    int x = 0;
    while (*value != '\0' && x + 5 <= OLED_WIDTH) {
        const uint8_t *glyph = steam_font::glyph(*value++);
        for (int column = 0; column < 5; ++column) oled_pixels[page * OLED_WIDTH + x++] = glyph[column];
        if (x < OLED_WIDTH) oled_pixels[page * OLED_WIDTH + x++] = 0;
    }
}

bool oled_flush() {
    uint8_t packet[OLED_WIDTH + 1];
    packet[0] = 0x40;
    for (int page = 0; page < OLED_PAGES; ++page) {
        const uint8_t command[] = {0x00, static_cast<uint8_t>(0xB0 | page), 0x02, 0x10};
        if (i2c_master_transmit(oled, command, sizeof(command), 100) != ESP_OK) return false;
        std::memcpy(packet + 1, oled_pixels + page * OLED_WIDTH, OLED_WIDTH);
        if (i2c_master_transmit(oled, packet, sizeof(packet), 100) != ESP_OK) return false;
    }
    return true;
}

void oled_draw(const AppState &s, const vga::Stats &v) {
    std::memset(oled_pixels, 0, sizeof(oled_pixels));
    char line[32];
    oled_text(0, s.stress == StressLevel::OFF ? "LIVE LAB CONSOLE"
               : (s.stress == StressLevel::REALISTIC ? "LAB CONSOLE STRESS" : "LAB CONSOLE ENVELOPE"));
    std::snprintf(line, sizeof(line), "LATE %lu IRR %lu", static_cast<unsigned long>(v.late),
                  static_cast<unsigned long>(v.irregular_frames));
    oled_text(1, line);
    std::snprintf(line, sizeof(line), "ISR %.0f/%.0fUS", v.max_exec_us, v.budget_us);
    oled_text(2, line);
    std::snprintf(line, sizeof(line), "CPU0 %u%% CPU1 %u%%", s.cpu_load[0], s.cpu_load[1]);
    oled_text(3, line);
    std::snprintf(line, sizeof(line), "HEAP %luK MIN %luK",
                  static_cast<unsigned long>(s.heap_internal_free / 1024),
                  static_cast<unsigned long>(s.heap_internal_min / 1024));
    oled_text(4, line);
    std::snprintf(line, sizeof(line), "PSRAM %.1f/%.0fM", s.psram_free / 1048576.0f,
                  s.psram_size / 1048576.0f);
    oled_text(5, line);
    std::snprintf(line, sizeof(line), "ETH %s", s.eth_has_ip ? s.eth_ip : (s.eth_link ? "DHCP" : "NO LINK"));
    oled_text(6, line);
    std::snprintf(line, sizeof(line), "AP %u STA %s", s.wifi_stations, s.wifi_ip);
    oled_text(7, line);
}

// ------------------------------------------------------------ BMP280
struct Calibration {
    uint16_t t1; int16_t t2, t3;
    uint16_t p1; int16_t p2, p3, p4, p5, p6, p7, p8, p9;
} cal = {};
int32_t fine_t = 0;

esp_err_t bmp_read(uint8_t reg, uint8_t *data, size_t length) {
    return i2c_master_transmit_receive(bmp, &reg, 1, data, length, 100);
}

esp_err_t bmp_write(uint8_t reg, uint8_t value) {
    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(bmp, data, 2, 100);
}

bool bmp_init() {
    uint8_t address = 0;
    if (i2c_master_probe(bus, 0x77, 50) == ESP_OK) address = 0x77;
    else if (i2c_master_probe(bus, 0x76, 50) == ESP_OK) address = 0x76;
    else return false;
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device.device_address = address;
    device.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &device, &bmp) != ESP_OK) return false;
    uint8_t id = 0;
    if (bmp_read(0xD0, &id, 1) != ESP_OK || id != 0x58) {
        ESP_LOGW(TAG, "Device at 0x%02x is not a BMP280 (id 0x%02x)", address, id);
        return false;
    }
    uint8_t d[24];
    if (bmp_read(0x88, d, sizeof(d)) != ESP_OK) return false;
    auto u16 = [&](int i) { return static_cast<uint16_t>(d[i] | (d[i + 1] << 8)); };
    auto s16 = [&](int i) { return static_cast<int16_t>(u16(i)); };
    cal = {u16(0), s16(2), s16(4), u16(6), s16(8), s16(10), s16(12), s16(14),
           s16(16), s16(18), s16(20), s16(22)};
    // Normal mode, x1 temperature, x4 pressure, 0.5 s standby, IIR 16.
    return bmp_write(0xF5, 0x10 | 0x00) == ESP_OK && bmp_write(0xF4, 0x2F) == ESP_OK;
}

int32_t compensate_t(int32_t adc) {
    const int32_t a = ((((adc >> 3) - (static_cast<int32_t>(cal.t1) << 1))) * cal.t2) >> 11;
    const int32_t b = (((((adc >> 4) - cal.t1) * ((adc >> 4) - cal.t1)) >> 12) * cal.t3) >> 14;
    fine_t = a + b;
    return (fine_t * 5 + 128) >> 8;
}

uint32_t compensate_p(int32_t adc) {
    int64_t a = static_cast<int64_t>(fine_t) - 128000;
    int64_t b = a * a * cal.p6;
    b += (a * cal.p5) << 17;
    b += static_cast<int64_t>(cal.p4) << 35;
    a = ((a * a * cal.p3) >> 8) + ((a * cal.p2) << 12);
    a = (((static_cast<int64_t>(1) << 47) + a) * cal.p1) >> 33;
    if (a == 0) return 0;
    int64_t p = 1048576 - adc;
    p = (((p << 31) - b) * 3125) / a;
    a = (static_cast<int64_t>(cal.p9) * (p >> 13) * (p >> 13)) >> 25;
    b = (static_cast<int64_t>(cal.p8) * p) >> 19;
    p = ((p + a + b) >> 8) + (static_cast<int64_t>(cal.p7) << 4);
    return static_cast<uint32_t>(p >> 8);   // Pa
}

void bmp_sample() {
    uint8_t raw[6];
    if (bmp == nullptr || bmp_read(0xF7, raw, sizeof(raw)) != ESP_OK) {
        app_state_update([](AppState &s, void *) { s.bmp_ok = false; }, nullptr);
        return;
    }
    const int32_t adc_p = (raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4);
    const int32_t adc_t = (raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4);
    struct Reading { int32_t t; uint32_t p; } reading = {compensate_t(adc_t), compensate_p(adc_p)};
    app_state_update([](AppState &s, void *r) {
        auto *value = static_cast<Reading *>(r);
        s.bmp_ok = true;
        s.temp_centi_c = value->t;
        s.pressure_pa = value->p;
    }, &reading);
}

void i2c_task(void *) {
    const bool have_oled = oled_init();
    const bool have_bmp = bmp_init();
    app_event("I2C: OLED %s, BMP280 %s", have_oled ? "ok" : "missing", have_bmp ? "ok" : "missing");
    TickType_t last_sample = 0;
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        if (have_bmp && now - last_sample >= pdMS_TO_TICKS(1000)) {
            last_sample = now;
            bmp_sample();
        }
        if (have_oled) {
            oled_draw(app_state_snapshot(), vga::stats());
            if (!oled_flush()) ESP_LOGW(TAG, "OLED write failed");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

}  // namespace

bool sensors_start() {
    i2c_master_bus_config_t config = {};
    config.i2c_port = board::I2C_PORT;
    config.sda_io_num = board::I2C_SDA;
    config.scl_io_num = board::I2C_SCL;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&config, &bus) != ESP_OK) return false;
    return spawn(i2c_task, "i2c", 4096, nullptr, 3, 0);
}
