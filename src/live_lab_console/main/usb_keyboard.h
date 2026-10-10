#pragma once

// USB boot keyboard on the Type-A host port (native USB in host mode, so the
// Type-C device port is unavailable while this runs).
//   R / Space  relay      S  stress      X  envelope test
//   C          clear VGA counters        M  mark the SD log
bool usb_keyboard_start();
