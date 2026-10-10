# STEAM "Same Pins, Two Worlds" (showcase project 2)

This firmware shows the board's pin sharing live. The 14 VGA pins that have a
second job (Table 2 of the JST draft) move between two worlds while the board
keeps running:

| World | Who owns the 14 shared pins |
|---|---|
| **A: VGA** | The RGB LCD peripheral: 800 × 600 at 36 MHz, all 18 VGA pins |
| **B: peripherals** | 7-segment display (74HC595), MAX7219 matrix, WS2812B LEDs, buzzer, LED on GPIO8, buttons on GPIO4–7 |

The presenter flips S6 and the **same GPIOs** change jobs. Then a deliberate
**"S6-2 left on during VGA"** fault feeds the VGA blue bits into the MAX7219
and VSYNC into the WS2812B. The VGA screen shows, live, what the matrix is
receiving. This turns claim C2 (selective isolation) into something the
audience can see, and the pin table on screen is claim C1.

Status: this compiles with no warnings on ESP-IDF v5.4.2 (`-Wall -Wextra`).
**It has not run on a board yet.** Treat the first session as a shakedown and
keep the serial log.

## How it switches pins

The RGB panel is created once, on core 1, and **never stops**. Switching
worlds only re-routes the pins in the GPIO matrix:

- World A to B: the shared pins are parked as inputs with pull-ups
  (`gpio_reset_pin`), the presenter changes S6, and on confirmation World B
  takes them (GPIO outputs, RMT for the WS2812B, LEDC for the buzzer and the
  7-segment brightness).
- World B to A: every output is switched off, RMT and LEDC are released, the
  pins are parked, the presenter changes S6, and on confirmation
  `vga::attach_pins()` routes them back to the LCD signals. This is the same
  routing `esp_lcd` uses when it creates the panel, so the picture is back in
  the next frame. No panel teardown, no bounce-buffer reallocation.

The VGA side reuses the Live Lab Console design unchanged: 4-bit frame buffer
with a palette, refill interrupt and renderer on core 1, everything else on
core 0, `CONFIG_LCD_RGB_RESTART_IN_VSYNC` off, XIP from PSRAM and an
IRAM-safe ISR. See `live_lab_console/VGA_STABILITY.md` for the reasoning.
The footer shows the E5 late-refill counters, so you can see that the world
switches do not disturb the refill.

## Hardware setup

Connect the VGA monitor, the USB-UART port (serial log and keys) and,
optionally, a USB keyboard in the **Type-A** port. The keyboard works in both
worlds because GPIO19/20 are not shared. Use the DC input if you can: World B
drives the matrix, the RGB LEDs and the buzzer at once.

Firmware cannot read S6, so the OLED tells the presenter what to set at each
step and waits for a confirmation.

| Switch | World A (VGA) | World B (peripherals) |
|---|---|---|
| S6-1 (B_EN) | OFF: colour buffers on | ON: colour buffers Hi-Z |
| S6-2 (MT_EN) | OFF: MAX7219/WS2812B branch off | ON: branch on |
| S6-3 (7SEG OE) | OFF | ON: GPIO41 drives the 74HC595 OE (brightness PWM) |
| S6-4 (SPK) | OFF: no sound in VGA mode (G5 = GPIO3) | ON: buzzer on GPIO3 |

These are the positions the all-in-one demo uses. **Check S6-2's polarity on
the first run.** The draft's Table 3 says MT_EN has a pull-down and is enabled
unless S6-2 is set, which is the opposite of this table. The fault demo
settles it: the switch position that makes the matrix fill with noise in VGA
mode is the "branch on" position. Then fix this table, the OLED text in
`main.cpp` and Table 3 to agree.

## Build and flash

```bash
cd same_pins_two_worlds
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Use ESP-IDF 5.4.x. `components/usb_host_hid` is a vendored copy of
espressif/usb_host_hid 1.2.0, so the build needs no component registry.

Without a build, flash the prebuilt image from `prebuilt/`:
`python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash @flash_args`
(run it inside `prebuilt/`).

The image has an 8 MB flash header and a 4 MB layout, so it boots on N8R8,
N8R16 or N16R8 modules. The boot log prints the flash and PSRAM sizes the
chip reports. The app is about 0.4 MB, so it also fits any launcher slot.

## Controls

| Input | World A | Switch steps | World B |
|---|---|---|---|
| BOOT (GPIO0), short press | Go to World B | Confirm | (not read: GPIO0 is the 7-segment latch) |
| BOOT, hold 1.5 s | Fault demo | | |
| GPIO4 button | (VGA R0) | Confirm | Back to World A |
| GPIO5 / GPIO6 / GPIO7 | | | Tune / next pin / 7-segment brightness |
| Enter, Space, Right | Next step | Confirm | Back to World A |
| Esc, Left, B | | Cancel the step | |
| F | Fault demo | | |
| N | Moving noise band on/off | | |
| P | Next spotlight pin | | Next spotlight pin |
| C | Clear the VGA counters | | |
| T / L | | | Tune / brightness |

The keys work on the USB keyboard and on the serial console (`h` prints the
list). A button press held across a pin change is ignored until it is
released.

**Do not press BOOT in World B.** GPIO0 is then driving the 74HC595 latch,
and the button would short that output to ground.

## Suggested 3-minute run on stage

1. **World A.** The screen shows the 18 VGA pins and the World B job of each
   one. Press P a few times to walk the spotlight across the shared pins.
   Point out that the 7-segment display may show garbage, because its data,
   clock and latch have no buffer.
2. **Switch to World B.** Press BOOT. The OLED says "PINS RELEASED. SET:".
   Flip S6-1, S6-2, S6-3 and S6-4 ON, then press GPIO4. The buzzer chirps,
   the RGB LEDs cycle, GPIO8 blinks, the 7-segment shows the spotlight pin
   (for example `GP17`) and the matrix scrolls
   `GPIO17  VGA B2  NOW MAX7219 CS`. Press GPIO6 to move to the next pin, so
   the matrix and the 7-segment tell the audience what each pin does in each
   world. GPIO5 plays a tune; GPIO7 dims the 7-segment display.
3. **Switch back.** Press GPIO4. Every output turns off. Flip all four
   switches OFF and press GPIO4 (or BOOT). The VGA picture returns in the
   next frame; the counters in the footer should still read LATE 0.
4. **The wrong switch.** Hold BOOT for 1.5 s (or press F). Flip **S6-2 ON**
   while VGA runs. The matrix fills with noise and the RGB LEDs light or
   flicker. The right panel now shows what the matrix is receiving: the
   CS, CLK and DIN levels of one scan line, the number of clock edges and
   register writes per frame, and the matrix image the simulation predicts.
   Press N to freeze the noise band and the matrix noise becomes a steady
   garbage pattern; press N again and it moves. Press BOOT, flip **S6-2 OFF**
   and the noise stops. The matrix keeps its last garbage until World B
   re-initialises it. Press BOOT to finish.
5. For the scope: put a probe on the MAX7219 CLK pin (after the HCT244) with
   S6-2 on and off, as in the Fig. 3 setup, and show it on a second screen.

## What is on the screen

| Area | Content |
|---|---|
| Header | Title, current step, the claim being shown (C1 or C2) |
| Pin table | All 18 VGA pins: GPIO, World A signal, World B function, what connects it (LVC245, HCT244, S6-3, S6-4 or no buffer). The spotlight pin is highlighted. |
| Right panel | The 16-colour palette with each colour's B4/B3/B2 bits (MAX7219 DIN/CLK/CS). In the fault demo: the simulated logic view and predicted matrix. |
| Band | Random 8 × 4-pixel colour cells, redrawn every second frame, so GPIO17/18/21 toggle at up to 4.5 MHz along each line. N switches it to static colour bars. |
| Footer | Late refills, irregular frames, longest refill against the budget, refresh rate, frame count and the last input |

### How the fault view is computed

Once a second, the renderer replays the whole frame buffer through an ideal
MAX7219 model: GPIO18 rising edges shift GPIO21 into a 16-bit register, and
GPIO17 rising edges latch it into the addressed register (digits, decode,
intensity, scan limit, shutdown, display test). It plays the frame twice so
the result does not depend on the start state, and counts the second pass.
This is a **prediction**, not a measurement:

- it assumes the data lines idle low in the blanking intervals;
- one pixel is 28 ns, shorter than the MAX7219's 50 ns minimum clock pulse,
  so the real chip may miss the one-pixel pulses (the panel shows what share
  of the clock pulses are one pixel wide);
- the order of a CLK and a LOAD edge in the same pixel is assumed CLK first.

The real matrix and the predicted one will not match pixel for pixel. The
point of the slide is that thousands of register writes per frame reach the
matrix when S6-2 is wrong and none when it is right.

## Differences from the showcase plan

- **The monitor loses VSYNC in World B.** The plan says the screen goes black
  while the sync stays alive. HSYNC stays alive, but VSYNC drives the
  connector directly (Table 3) and GPIO2 carries the WS2812B data in World B,
  so expect the monitor to go black or report no signal. The picture comes
  back as soon as World A takes GPIO2 back.
- **Buttons only on GPIO4–7.** Touch is left out, as in the peripheral
  showcase: it needs per-board calibration, and switching the pins between
  the touch sensor and the GPIO matrix adds risk on stage.
- **7-segment polarity.** The kit's `7seg.ino` lights a segment with a LOW
  74HC595 output; `STEAM_PERIPHERAL_SHOWCASE_IDF54` sends the opposite. This
  firmware follows `7seg.ino`. If every digit shows its complement, turn off
  `SPTW_SEG_ACTIVE_LOW` in `menuconfig`. If `GP17` reads backwards, turn on
  `SPTW_SEG_REVERSE_DIGITS`.

## First-boot checklist

- The serial log prints `VGA 800x600 4bpp, 36.00 MHz, 20-line bounce buffers
  on core 1` and a "running" line for OLED, VGA, scene, buttons and USB host.
- If the monitor shows nothing in World A, check S6-1 first. The timing is the
  slideshow's (36 MHz, 1022 × 623, positive sync).
- If the matrix scroll is mirrored or upside down, swap the bit order in
  `max_show_window()` in `peripherals.cpp`.
- If a World B output does nothing, check its S6 position, then the serial log
  (`World B: shared pins claimed`).

## Layout

```
main/main.cpp           start-up, the walkthrough (stages), OLED text
main/app.*              shared view state, command queue, readable-input mask
main/pins.h             the 18 VGA pins and their World B jobs (Table 2)
main/vga4.*             4bpp VGA driver on core 1 (from the Live Lab Console)
                        + attach_pins() to hand pins back to the LCD
main/scene.*            800x600 picture, noise band, MAX7219 fault model
main/gfx.*, font8x16.h  drawing primitives, generated 8x16 font
main/peripherals.*      World B: 74HC595, MAX7219, WS2812B (RMT), buzzer and
                        7-segment PWM (LEDC), LED, park/claim/release
main/oled.*, font5x7.h  SH1106 OLED (works in both worlds)
main/input.*            BOOT and GPIO4-7 buttons, serial console keys
main/usb_keyboard.*     USB boot keyboard on the Type-A port
components/usb_host_hid vendored espressif/usb_host_hid 1.2.0
tools/gen_font.py       regenerates font8x16.h
prebuilt/               binaries from the compile check (ESP-IDF v5.4.2)
```

Built from the Live Lab Console (VGA driver, core split, OLED, USB keyboard),
the mode-switch flow in `STEAM_BOARD_IDF54_ALL_IN_ONE_DEMO`
(`peripheral_demo.cpp`) and the drivers in `STEAM_PERIPHERAL_SHOWCASE_IDF54`
(`showcase_outputs.cpp`).
