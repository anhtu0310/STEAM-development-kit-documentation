#pragma once

// STEAM development board pin map in VGA mode (S6-1 OFF = LVC245 colour
// buffers enabled, S6-2/3/4 OFF). Same GPIO numbers as the demo projects in
// STEAM_THE_END.zip and steam_bench_firmware/components/.../steam_pins.h.

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_types.h"

namespace board {

// VGA: data bit 0 = red LSB, then green, then blue (BBBBBGGGGGGRRRRR).
inline constexpr gpio_num_t VGA_HSYNC = GPIO_NUM_1;
inline constexpr gpio_num_t VGA_VSYNC = GPIO_NUM_2;   // also WS2812B DIN via HCT244: keep S6-2 OFF
inline constexpr int VGA_DATA[16] = {
    4, 5, 6, 7, 8,          // R0..R4
    38, 39, 40, 41, 42, 3,  // G0..G5 (G5 = GPIO3 = buzzer net: no sound in VGA mode)
    15, 16, 17, 18, 21      // B0..B4
};

// Shared SPI bus: W5500 Ethernet and microSD (GPIO matrix, 20 MHz each).
inline constexpr spi_host_device_t SPI_HOST = SPI2_HOST;
inline constexpr gpio_num_t SPI_MOSI = GPIO_NUM_11;
inline constexpr gpio_num_t SPI_MISO = GPIO_NUM_12;
inline constexpr gpio_num_t SPI_SCLK = GPIO_NUM_13;
inline constexpr gpio_num_t W5500_CS = GPIO_NUM_14;
inline constexpr gpio_num_t W5500_RESET = GPIO_NUM_9;
inline constexpr gpio_num_t W5500_INT = GPIO_NUM_10;
inline constexpr gpio_num_t SD_CS = GPIO_NUM_46;

// Shared I2C bus: SH1106 OLED (0x3C) and BMP280 (0x77, falls back to 0x76).
inline constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
inline constexpr gpio_num_t I2C_SDA = GPIO_NUM_47;
inline constexpr gpio_num_t I2C_SCL = GPIO_NUM_48;

inline constexpr gpio_num_t RELAY = GPIO_NUM_45;     // strapping pin, active high
inline constexpr gpio_num_t BUTTON = GPIO_NUM_0;     // strapping pin, active low

}  // namespace board
