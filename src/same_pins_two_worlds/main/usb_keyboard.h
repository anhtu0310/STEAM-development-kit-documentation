#pragma once

// USB boot keyboard on the Type-A host port. GPIO19/20 are not shared with
// VGA, so the keyboard works in both worlds.
//   Enter / Space / Right   next step        Esc / Left / B   cancel a step
//   F fault demo   N noise band   C clear counters   P next pin
//   T tune (World B)   L 7-segment brightness (World B)
bool usb_keyboard_start();
