# Why the VGA picture flickers and loses sync in the demo projects

This note covers the cause of the flicker and the design this project uses
to avoid it. It is based on reading the demo sources in `STEAM_THE_END.zip`
and the ESP-IDF v5.4.2 RGB LCD driver
(`components/esp_lcd/rgb/esp_lcd_panel_rgb.c`). Nothing in it has been
measured on the board yet. The `E5_E8_E9_vga_bench` firmware and this
console's live counters are how to confirm it.

## How the picture is produced

All the demos use the RGB LCD peripheral in bounce-buffer mode. The DMA sends
one internal-SRAM buffer of L lines (L = 20 in the slideshow) while an
interrupt refills the other one from the PSRAM frame buffer. At 36 MHz and
1022 pixels per line, each refill has to finish within

    T_bb = L x 28.4 us = 568 us   (L = 20)

The DMA raises an end-of-frame (EOF) interrupt for each buffer. The driver
picks the buffer to refill from a running EOF count (`bb_eof_count % 2`).

## What goes wrong, in order of impact

1. **The refill interrupt shares core 0 with the radios.** The panel is
   created from `app_main`, so both LCD interrupts (the GDMA EOF and VSYNC)
   are allocated on core 0. The BLE controller, Bluedroid, the Wi-Fi task and
   lwIP all run there too (`CONFIG_BT_CTRL_PINNED_TO_CORE_0`,
   `ESP_WIFI_TASK_PINNED_TO_CORE_0`, lwIP with no affinity). The radio
   controllers mask interrupts in long critical sections, and BLE scanning
   (the all-in-one demo rescans every few seconds) is the worst case. If a
   refill is held off for more than T_bb, the DMA sends a stale buffer, which
   shows as a torn 20-line band. If two EOFs merge into one interrupt, the
   count drops by one.

2. **`CONFIG_LCD_RGB_RESTART_IN_VSYNC=y` makes one lost EOF permanent.** All
   the demos enable this option. With it set, the driver restarts the DMA at
   every VSYNC but **never resets `bb_eof_count`**. The counter is reset only
   in the `#else` branch of `lcd_rgb_panel_try_restart_transmission`, and
   v5.4.3 and v5.5.1 have the same code. After a single lost EOF the parity
   is wrong for good. The driver then refills the buffer the DMA is sending,
   and every band shows the wrong lines. This stays until another EOF is
   lost. A forced restart also lands inside the visible picture whenever the
   VSYNC interrupt itself is late (cause 1), which shifts that frame.
   With the option **off**, ESP-IDF 5.4 still restarts at VSYNC when a frame
   had fewer EOFs than expected, and it does reset the counter. A missed
   refill then costs one frame and the picture recovers.

3. **Flash writes freeze the refill.** `CONFIG_LCD_RGB_ISR_IRAM_SAFE` is off
   in every demo. Each NVS write disables the cache and the interrupts for
   the length of the write: Wi-Fi PHY calibration, Wi-Fi config storage and
   BLE bonding keys are the common ones, and a sector erase takes tens of
   milliseconds. Even an IRAM-safe interrupt could not read a PSRAM frame
   buffer while the cache is off.

4. **Too much PSRAM traffic.** 800 x 600 RGB565 at 36 MHz is 72 MB/s of
   PSRAM reads, out of a bus (octal, 80 MHz) shared with code fetch. The
   advanced dashboard also redraws the whole 614 kB frame into PSRAM five
   times a second from the other core. The ESP-IDF documentation names this
   exact pattern (one core copying PSRAM while the other runs the EOF
   interrupt) as the cause of screen shift. The slideshow avoids it only by
   blanking the screen during SD loads.

## What this project does instead

| Cause | Countermeasure | Where |
|---|---|---|
| 1 | The panel is created from a task pinned to **core 1**, so both LCD interrupts run there. Wi-Fi, BLE controller, Bluedroid, lwIP, esp_timer, the HTTP server, W5500, USB host, SD and I²C are all pinned to core 0. The only other code on core 1 is the dashboard renderer at priority 2. | `vga4.cpp: start()`, `sdkconfig.defaults` (core split) |
| 2 | `CONFIG_LCD_RGB_RESTART_IN_VSYNC` is **off**. The built-in EOF-deficit restart recovers within one frame. | `sdkconfig.defaults` |
| 3 | `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`: code and read-only data run from PSRAM, so flash writes no longer disable the cache or stall the other core (`SPI_FLASH_CACHE_NO_DISABLE` in `spi_flash_os_func_app.c`). `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`, the refill callback is in IRAM and its tables are in DRAM. Wi-Fi uses RAM storage, so it makes no NVS writes at run time. | `sdkconfig.defaults`, `net.cpp` |
| 4 | The frame buffer is **4 bits per pixel** (16-colour palette, 240 kB). The interrupt expands each byte to two RGB565 pixels through a 1 kB table in DRAM, so VGA reads **18 MB/s instead of 72 MB/s**. At the end of each refill it starts a cache preload of the next chunk. The renderer draws each panel off screen and copies only the panels that changed, and it runs on the same core as the interrupt, so its PSRAM traffic never overlaps a refill. | `vga4.cpp`, `gfx.cpp`, `dashboard.cpp` |

An expected cost per refill at L = 20: 8 kB read from PSRAM plus 32 kB
written to SRAM, roughly 100 to 150 us of a 568 us budget. The live "ISR max"
and "min slack" numbers on screen show the real figure.

Two side effects:

- Moving Wi-Fi and FreeRTOS code out of IRAM (to free internal SRAM)
  means that code is fetched from PSRAM through the cache on core 0. That
  adds a small amount of core-0 PSRAM traffic.
- 16 colours is a limit for photographs, but it is plenty for a dashboard.
  The slideshow still needs RGB565, and it can use the same core split and
  restart setting.

## How to show it live

- The **VGA REFILL HEALTH** panel (also on the OLED and at `/api/status`)
  uses the E5 bench definitions: late refills (finished after their
  deadline), irregular frames (EOF count not 600 / L, which triggers the
  VSYNC restart), the longest refill time against the T_bb budget, and the
  smallest margin. The bar strip shows late refills per second over the last
  60 s.
- **S (stress)** adds continuous SD writes on the shared SPI bus and a full
  recompose and copy of the dashboard every frame. Run `netload.py blast`
  from a laptop over Ethernet and from a phone or a second laptop over the
  AP at the same time. The expected result is LATE 0.
- **X (envelope)** adds a core-0 task that copies 256 kB PSRAM blocks in a
  tight loop, which reproduces cause 4 on purpose. The counter is expected to
  rise. Because restart-every-frame is off, the picture should recover by the
  next frame instead of staying banded. This is the "where the envelope ends"
  slide for the paper.

## Applying this to the other demos

For the slideshow, the all-in-one demo and the advanced dashboard, the
smallest change that keeps their code is:

1. Turn `CONFIG_LCD_RGB_RESTART_IN_VSYNC` off.
2. Create the RGB panel from a task pinned to core 1, and keep the
   SD/HTTP/render tasks off core 1.
3. Add `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` and `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y`.
4. Advanced dashboard only: stop full-frame redraws, and redraw only the
   cards that changed.

`E5_E8_E9_vga_bench` already has restart-on and restart-off builds.
Comparing E5 rows 12 to 17 between the two builds tests cause 2 directly.
