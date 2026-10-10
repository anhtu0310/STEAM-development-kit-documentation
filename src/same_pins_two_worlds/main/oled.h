#pragma once

// SH1106 128x64 OLED on the shared I2C bus (GPIO47/48). It is the only screen
// that works in both worlds, so it carries the switch instructions.

namespace oled {

bool start();
// Up to eight lines of up to 21 characters (5x7 font, upper case).
void show(const char *const lines[8]);

}  // namespace oled
