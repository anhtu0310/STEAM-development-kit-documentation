# STEAM Live Lab Console (showcase project 1)

This firmware runs every VGA-mode function of the STEAM ESP32-S3 board at
the same time and shows them on one 800 × 600 VGA dashboard:

- W5500 Ethernet (DHCP, with a static fallback)
- Wi-Fi soft-AP
- BLE HID clicker (AB Shutter3)
- USB keyboard on the Type-A host port
- microSD logging
- BMP280 and SH1106 OLED on the shared I²C bus
- Relay and the GPIO0 button

A laptop opens the same dashboard over Ethernet, and a phone opens it over
the AP. A live **late-refill counter** shows the VGA stability claim (C3)
being tested while the audience watches. A **stress key** loads the board on
purpose.

Why the VGA output flickered in the earlier demos, and how this project
avoids it, is in [VGA_STABILITY.md](VGA_STABILITY.md). Read it before you
change `sdkconfig.defaults`.

Status: this compiles with no warnings on ESP-IDF v5.4.2 (`-Wall -Wextra`).
**It has not run on a board yet**, so treat the first session as a
shakedown and keep the serial log.

## Hardware setup (VGA mode)

| Switch | Position | Why |
|---|---|---|
| S6-1 | OFF | LVC245 colour buffers enabled (B_EN) |
| S6-2 | OFF | MAX7219/WS2812B buffer off. If it is on, the RGB LEDs flicker with VSYNC (GPIO2). |
| S6-3 | ON | Blanks the 7-segment display, which would otherwise show garbage from the shared pins |
| S6-4 | OFF | Buzzer disconnected. G5 (GPIO3) is also the buzzer net, so there is no sound in VGA mode. |

Use the same VGA switch positions that work with the slideshow. Then connect:

- the VGA monitor;
- an Ethernet cable to a router or straight to a laptop;
- a USB keyboard in the **Type-A** port (native USB is in host mode, so the
  Type-C device port is unavailable);
- a FAT-formatted microSD card;
- the USB-UART port for the serial log.

GPIO0 and GPIO45 are strapping pins. Do not hold GPIO0 during reset. The
relay output is driven LOW before anything else starts.

## Build and flash

```bash
cd live_lab_console
idf.py set-target esp32s3
idf.py build
idf.py -p COM5 flash monitor
```

Use ESP-IDF 5.4.x. `components/usb_host_hid` is a vendored copy of
espressif/usb_host_hid 1.2.0, so the build needs no component registry.

Without a build, flash the prebuilt image from `prebuilt/`:
`python -m esptool --chip esp32s3 -p COM5 -b 921600 write_flash @flash_args`.

The image is built with an 8 MB flash header and fits in 4 MB, so it boots
on an N8R8, N8R16 or N16R8 module. The boot log, the OLED and the SYSTEM
panel show the flash and PSRAM sizes **the chip reports**. Use these to
settle the 8 MB / 16 MB question in the draft.

For the five-slot launcher, the app (about 1.4 MB) fits the 3 MiB `advanced`
slot. Flash it with the launcher's `flash_five_apps.py`, as the advanced
dashboard README describes.

## Using it

| Input | Action |
|---|---|
| USB `R` or Space, BLE small button, GPIO0 tap, web button | Toggle the relay (turns off automatically after 30 s, `menuconfig`) |
| USB `S`, BLE big button, GPIO0 hold 1.5 s, web button | **Stress** on/off: continuous SD writes, plus a full recompose and copy of the dashboard every frame |
| USB `X`, web button | **Envelope** test on/off: adds a core-0 PSRAM copy loop that deliberately breaks the budget |
| USB `C`, web button | Clear the VGA counters |
| USB `M`, web button | Write a marker line to the SD log |

Networks:

- Wi-Fi AP: `STEAM-LAB`, password `steamlab`, `http://192.168.4.1/`.
- Ethernet: DHCP. With no DHCP server for 8 s, the board falls back to
  `192.168.50.2/24` (give the laptop `192.168.50.1`, as for the E1 bench).
  The address is shown on VGA and on the OLED.
- `GET /api/status` returns everything as JSON, and `POST /api/cmd?c=relay|stress|envelope|clear|mark`
  runs a command.

### Network load for the stress demo

The board runs a TCP sink on port 5001 on both interfaces. It counts the
bytes and shows Mbit/s per interface. Use the bench tool
(`steam_bench_firmware/host/netload.py`, a copy is in `tools/`):

```bash
python tools/netload.py blast --host 192.168.4.1   --seconds 600   # over the Wi-Fi AP
python tools/netload.py blast --host <ethernet ip> --seconds 600   # over Ethernet
```

### Suggested 3-minute run on stage

1. Idle: all cards green and **LATE 0**. Point out that the bar strip is flat.
2. Open the page on a phone (Wi-Fi) and a laptop (Ethernet). Press the BLE
   clicker and type on the keyboard, and the cards update.
3. Start both `netload.py blast` commands, then press **S**. SD writes run at
   full speed on the bus shared with the W5500. The counter should stay at 0.
4. Press **X**. The counter climbs and the bar strip turns amber or red. The
   picture should recover within a frame instead of staying banded. This
   shows where the envelope ends.
5. Press **X** again, then **C**. The counter returns to 0 and stays there.

## What is on the screen

| Panel | Content |
|---|---|
| Header | Mode, refresh rate, uptime, and a STRESS/ENVELOPE badge |
| Ethernet | Link, IP (DHCP or static), sink Mbit/s and MB |
| Wi-Fi AP | Clients, SSID and IP, sink Mbit/s and MB |
| BLE remote | State, battery, last button |
| USB host | Keyboard state, last key |
| Relay | State, who switched it, time left, GPIO0 presses |
| microSD | Logging, or write rate in stress mode; lines, clock, errors |
| BMP280 | Temperature and pressure, 250 s graph |
| VGA refill health | Late refills, irregular frames, longest refill / budget, smallest margin, late refills per second for the last 60 s |
| Events | Last four events |
| System | Load per core, internal SRAM free/min, PSRAM, flash size, bounce lines, frame count |

The OLED shows the late and irregular counts, the longest refill, CPU load,
heap, PSRAM and both IP addresses, so the counter is readable even when the
monitor is out of view.

The SD log `llc_log.csv` gets one line every 2 s with the uptime,
temperature, pressure, VGA counters, network rates, SD rate, CPU load, free
SRAM, stress level and marker. This is a log of the demo run that you can
plot for the paper.

## First-boot checklist

- The serial log prints `SRAM free / DMA largest / PSRAM free` after each
  start-up stage. If "after USB" shows less than about 20 kB of SRAM free,
  set `LLC_BOUNCE_LINES` to 15 in `menuconfig` (2 × 24 kB instead of 2 × 32 kB).
- The `VGA 800x600 4bpp, 36.00 MHz, 20-line bounce buffers on core 1` line
  confirms that the refills run on core 1.
- If the monitor shows nothing, the timing is the one from the slideshow
  (36 MHz, 1022 × 623, positive sync). Check S6-1 first.
- If a colour looks wrong, check the palette in `dashboard.cpp`. The bit
  order is the board's BBBBBGGGGGGRRRRR.

## Layout

```
main/main.cpp          start-up order, command handling, envelope task, CPU load
main/vga4.*            4bpp VGA driver on core 1 + E5 late-refill statistics
main/gfx.*, font8x16.h drawing primitives, generated 8x16 font
main/dashboard.*       800x600 layout, off-screen panels, history graphs
main/net.*             W5500 + Wi-Fi AP + TCP sink :5001
main/web.*, index.html browser dashboard and JSON API
main/storage.*         microSD mount, CSV log, stress writer
main/ble_remote.*      AB Shutter3 BLE HID host (esp_hid_gap.c from the demos)
main/usb_keyboard.*    USB HID boot keyboard
main/sensors.*         SH1106 OLED + BMP280 on the shared I2C bus
main/io.*              relay with auto-off, GPIO0 button
components/usb_host_hid vendored espressif/usb_host_hid 1.2.0
tools/gen_font.py      regenerates font8x16.h
tools/netload.py       network load generator (from steam_bench_firmware)
prebuilt/              binaries from the compile check (ESP-IDF v5.4.2)
```

The code is built from `01_STEAM_ADVANCED_DASHBOARD_UPDATED` (web server,
BMP280, relay, OLED), `STEAM_BOARD_IDF54_ALL_IN_ONE_DEMO` (BLE remote, USB
host, W5500, SD) and the late-refill instrumentation in
`steam_bench_firmware/E5_E8_E9_vga_bench`.
