#pragma once

// World B: the functions that share the VGA pins. Everything here runs in the
// control task on core 0; nothing is touched while World A owns the pins.

#include <cstdint>

#include "app.h"

namespace periph {

bool init_once();   // LEDC timers only; claims no pins

// Every shared pin becomes a plain input with a pull-up (gpio_reset_pin), so
// nothing drives the board while the presenter changes S6. GPIO0 too.
void park();

// Takes the shared pins for World B, initialises the 7-segment chain, the
// MAX7219, the WS2812B (RMT), the buzzer and the LED, and chirps.
void claim();

// All outputs off, RMT and LEDC released, then park().
void release();

void tick(uint32_t now_ms, const View &view);   // animation, every ~20 ms
uint8_t read_inputs();                          // GPIO4..7 pressed bits
void tune_start();
uint8_t brightness_next();                      // returns the new percent

}  // namespace periph
