#pragma once

// Draws the 800x600 console. Runs as a low-priority task on core 1, next to
// the VGA refill interrupt, so its PSRAM traffic never overlaps a refill.
bool dashboard_start();
