#pragma once

// The 800x600 VGA picture, drawn by a renderer task on core 1 (the same core
// as the refill interrupt, so its PSRAM traffic never overlaps a refill):
//   header      title and the current stage
//   pin table   all 18 VGA pins and their World B jobs (Table 2)
//   right panel palette with each colour's B2..B4 bits, or, in the fault
//               demo, a simulated logic view of what the MAX7219 receives
//   noise band  moving random colours that toggle B2..B4 every frame
//   footer      E5 late-refill counters and the key help
namespace scene {

bool start();

}  // namespace scene
