#include "oled.h"

#include <cstring>

#include "board.h"
#include "font5x7.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

namespace oled {
namespace {

constexpr char TAG[] = "sptw_oled";
constexpr int WIDTH = 128;
constexpr int PAGES = 8;
constexpr int COLUMNS = 21;

i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t device = nullptr;
uint8_t pixels[WIDTH * PAGES];
char shown[PAGES][COLUMNS + 1];

void draw_text(int page, const char *value) {
    int x = 0;
    while (*value != '\0' && x + 5 <= WIDTH) {
        const uint8_t *glyph = steam_font::glyph(*value++);
        for (int column = 0; column < 5; ++column) pixels[page * WIDTH + x++] = glyph[column];
        if (x < WIDTH) pixels[page * WIDTH + x++] = 0;
    }
}

bool flush_page(int page) {
    const uint8_t command[] = {0x00, static_cast<uint8_t>(0xB0 | page), 0x02, 0x10};
    if (i2c_master_transmit(device, command, sizeof(command), 100) != ESP_OK) return false;
    uint8_t packet[WIDTH + 1];
    packet[0] = 0x40;
    std::memcpy(packet + 1, pixels + page * WIDTH, WIDTH);
    return i2c_master_transmit(device, packet, sizeof(packet), 100) == ESP_OK;
}

}  // namespace

bool start() {
    i2c_master_bus_config_t config = {};
    config.i2c_port = board::I2C_PORT;
    config.sda_io_num = board::I2C_SDA;
    config.scl_io_num = board::I2C_SCL;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&config, &bus) != ESP_OK) return false;
    if (i2c_master_probe(bus, 0x3C, 50) != ESP_OK) {
        ESP_LOGW(TAG, "No SH1106 at 0x3C");
        return false;
    }
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = 0x3C;
    dev.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(bus, &dev, &device) != ESP_OK) return false;
    static constexpr uint8_t sequence[] = {
        0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0xAD, 0x8B,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x7F, 0xD9, 0x22, 0xDB, 0x35, 0xA4, 0xA6, 0xAF};
    if (i2c_master_transmit(device, sequence, sizeof(sequence), 100) != ESP_OK) return false;
    std::memset(shown, 0xFF, sizeof(shown));   // force the first draw
    return true;
}

void show(const char *const lines[8]) {
    if (device == nullptr) return;
    for (int page = 0; page < PAGES; ++page) {
        char line[COLUMNS + 1] = {};
        if (lines[page] != nullptr) std::strncpy(line, lines[page], COLUMNS);
        if (std::memcmp(line, shown[page], sizeof(line)) == 0) continue;
        std::memcpy(shown[page], line, sizeof(line));
        std::memset(pixels + page * WIDTH, 0, WIDTH);
        draw_text(page, line);
        if (!flush_page(page)) ESP_LOGW(TAG, "OLED write failed");
    }
}

}  // namespace oled
