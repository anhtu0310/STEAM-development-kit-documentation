#pragma once
#include <cstdint>

// Relay on GPIO45 (with an automatic-off timer) and the GPIO0 button.
bool io_start();
void io_relay_set(bool on, const char *source);
void io_relay_toggle(const char *source);
