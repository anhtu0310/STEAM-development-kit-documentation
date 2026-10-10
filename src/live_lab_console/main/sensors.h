#pragma once

// Shared I2C bus on GPIO47/48: SH1106 OLED status screen and BMP280.
// Both run in one core-0 task, so the bus has a single owner.
bool sensors_start();
