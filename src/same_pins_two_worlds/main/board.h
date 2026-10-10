#pragma once

// STEAM development board pin map for both worlds. Same GPIO numbers as the
// demo projects in STEAM_THE_END.zip, the Live Lab Console and
// steam_bench_firmware. Table 2 of the JST draft lists the shared pins.

#include "driver/gpio.h"
#include "driver/i2c_types.h"

namespace board {

// ------------------------------------------------------------ World A: VGA
// Data bit 0 = red LSB, then green, then blue (BBBBBGGGGGGRRRRR).
inline constexpr gpio_num_t VGA_HSYNC = GPIO_NUM_1;   // direct to the connector
inline constexpr gpio_num_t VGA_VSYNC = GPIO_NUM_2;   // direct, and WS2812B DIN via HCT244
inline constexpr int VGA_DATA[16] = {
    4, 5, 6, 7, 8,          // R0..R4
    38, 39, 40, 41, 42, 3,  // G0..G5
    15, 16, 17, 18, 21      // B0..B4
};

// -------------------------------------------------- World B: peripherals
// 74HC595 chain (four digits). Latch, clock and data have no buffer.
inline constexpr gpio_num_t SEG_LATCH = GPIO_NUM_0;      // also the BOOT button
inline constexpr gpio_num_t SEG_CLOCK = GPIO_NUM_42;     // VGA G4
inline constexpr gpio_num_t SEG_DATA = GPIO_NUM_16;      // VGA B1
inline constexpr gpio_num_t SEG_RESET_N = GPIO_NUM_15;   // VGA B0
inline constexpr gpio_num_t SEG_OE_N = GPIO_NUM_41;      // VGA G3, S6-3 (PWM brightness)
// MAX7219 and WS2812B through the 74HCT244 (MT_EN, S6-2).
inline constexpr gpio_num_t MAX_CS = GPIO_NUM_17;        // VGA B2
inline constexpr gpio_num_t MAX_CLK = GPIO_NUM_18;       // VGA B3
inline constexpr gpio_num_t MAX_DIN = GPIO_NUM_21;       // VGA B4
inline constexpr gpio_num_t WS2812 = GPIO_NUM_2;         // VGA VSYNC
inline constexpr gpio_num_t BUZZER = GPIO_NUM_3;         // VGA G5, S6-4
inline constexpr gpio_num_t LED = GPIO_NUM_8;            // VGA R4
// Buttons on GPIO4..7 (VGA R0..R3), active low.
inline constexpr gpio_num_t INPUTS[4] = {GPIO_NUM_4, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_7};

// Every VGA pin that World B takes over. Pins 1, 38, 39 and 40 stay VGA.
inline constexpr int SHARED[] = {2, 3, 4, 5, 6, 7, 8, 15, 16, 17, 18, 21, 41, 42};
inline constexpr int SHARED_COUNT = sizeof(SHARED) / sizeof(SHARED[0]);

// ------------------------------------------------------- both worlds
inline constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
inline constexpr gpio_num_t I2C_SDA = GPIO_NUM_47;   // SH1106 OLED 0x3C
inline constexpr gpio_num_t I2C_SCL = GPIO_NUM_48;
inline constexpr gpio_num_t RELAY = GPIO_NUM_45;     // strapping pin, held LOW
inline constexpr gpio_num_t BUTTON = GPIO_NUM_0;     // BOOT, read only while GPIO0 is an input

}  // namespace board
