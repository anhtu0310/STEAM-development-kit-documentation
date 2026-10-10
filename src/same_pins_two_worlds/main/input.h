#pragma once

// Board buttons and the serial console. Both only post commands.
//   BOOT (GPIO0)  short: next step    hold 1.5 s: fault demo   (when GPIO0 is an input)
//   GPIO4         next step                                    (switch steps, World B)
//   GPIO5 tune    GPIO6 next pin    GPIO7 7-segment brightness (World B)
// Serial: Enter/Space next, b/Esc cancel, f fault, n noise, c clear, p pin,
// t tune, l brightness, h help.
bool input_start();
