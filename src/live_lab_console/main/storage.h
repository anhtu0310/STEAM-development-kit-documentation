#pragma once

// microSD on the shared SPI bus (with the W5500). A CSV log line every 2 s,
// and in stress mode a continuous 16 kB write loop.
bool storage_start();           // mounts the card; the bus must already exist
void storage_set_stress(bool on);
void storage_mark(const char *source);
