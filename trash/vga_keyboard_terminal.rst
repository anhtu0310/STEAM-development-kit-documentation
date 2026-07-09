***************************************************************
Project 6 : USB Keyboard Terminal on a VGA Monitor
***************************************************************


Introduction
============

In this project, you will combine two advanced ESP32-S3 peripherals simultaneously: the built-in **USB OTG controller** in host mode (from Project 4) and the **LCD parallel interface** repurposed to generate a **VGA video signal** using bitluni's ESP32-S3 VGA library.

A USB keyboard plugged into the board becomes the input device. Every keystroke is decoded and rendered in real time as a scrolling text terminal on a standard VGA monitor — the same style of terminal interface that computers used for decades before graphical operating systems.

By the end of this tutorial, you will know how to:

* Understand how the ESP32-S3 generates a VGA signal in software using its LCD peripheral.
* Wire a VGA connector to the ESP32-S3 with a simple resistor circuit.
* Configure the bitluni ESP32-S3-VGA library with a custom pin mapping.
* Combine VGA output and USB host keyboard input in one sketch.
* Implement a scrolling text terminal with cursor rendering using the VGA drawing API.
* Manage the interaction between the USB background task and the VGA rendering loop.

Requirements
============

Before starting, make sure you have the following:

* The STEAM development kit or The STEAM standalone microcontroller board
* A standard USB keyboard (USB HID Boot Protocol compatible)
* A USB Type-A to Type-C OTG cable to connect the keyboard
* A VGA monitor with a VGA cable (DE-15 connector)
* 5 × resistors (see wiring section below for values)
* A breadboard and jumper wires
* USB cable (for programming, connected to a separate UART port)
* Arduino IDE with ESP32 toolchain installed (esp32 board package version **3.0 or later**)

.. figure:: ../../img/MCU_board.png
   :align: center
   :width: 400
   :figclass: align-center

   The interfacing description of the microcontroller board

Background: Key Concepts
=========================

How the ESP32-S3 Generates a VGA Signal
-----------------------------------------

A VGA signal consists of five wires: three analogue video channels (**Red**, **Green**, **Blue**) each carrying a voltage between 0 V (no colour) and 0.7 V (full colour), plus a **horizontal sync** (hSync) and a **vertical sync** (vSync) square-wave signal that tells the monitor where each line and each frame begins.

The ESP32-S3 cannot produce analogue voltages directly from its GPIO pins — they are digital outputs at either 0 V or 3.3 V. bitluni's library solves this with two techniques:

* **Parallel output via the LCD peripheral** — the ESP32-S3's built-in LCD/camera interface drives multiple GPIO pins simultaneously in hardware at precise pixel-clock timing, matching the VGA standard. This is far more accurate than toggling pins in software loops.
* **Resistor DAC** — each colour channel uses one GPIO pin per bit, each connected through a resistor of a different value. The resistors form a voltage divider network that produces intermediate analogue voltages at the VGA connector, encoding more than two levels per channel without any external DAC chip.

.. figure:: ../../img/vga_resistor_dac.png
   :align: center
   :width: 400
   :figclass: align-center

   Resistor DAC concept: digital GPIO pins produce an analogue voltage by summing weighted currents

3-Bit vs. 16-Bit Colour
-------------------------

The library supports multiple colour depths, trading image quality for pin count and memory:

.. list-table::
   :header-rows: 1
   :widths: 15 15 15 55

   * - Mode
     - Pins
     - Colours
     - Notes
   * - 3-bit
     - 3
     - 8
     - One pin per channel (R, G, B). Simplest wiring, minimal resistors. Sufficient for a text terminal.
   * - 16-bit
     - 16
     - 65 536
     - Five red + six green + five blue pins. Requires many GPIOs and a full resistor ladder per channel.

This project uses **3-bit colour** — one GPIO pin per colour channel — because it is ideal for a text terminal: the wiring is simple, it requires only three signal resistors, and it uses no PSRAM.

.. note::
   To use 3-bit mode in the ``PinConfig`` struct, pass ``-1`` for the unused bit lanes and a real GPIO number only for the most-significant bit of each channel. The library will produce 8 colours: black, blue, green, cyan, red, magenta, yellow, and white.

The VGA Connector
------------------

The VGA connector (DE-15) carries 15 pins. Only five are relevant for this project:

.. list-table::
   :header-rows: 1
   :widths: 10 20 30 40

   * - Pin
     - Signal
     - Connect To
     - Notes
   * - 1
     - Red
     - ESP32-S3 GPIO (via resistor)
     - Analogue, 0–0.7 V
   * - 2
     - Green
     - ESP32-S3 GPIO (via resistor)
     - Analogue, 0–0.7 V
   * - 3
     - Blue
     - ESP32-S3 GPIO (via resistor)
     - Analogue, 0–0.7 V
   * - 13
     - hSync
     - ESP32-S3 GPIO (direct)
     - 3.3 V TTL compatible
   * - 14
     - vSync
     - ESP32-S3 GPIO (direct)
     - 3.3 V TTL compatible
   * - 5, 6, 7, 8, 10
     - Ground
     - GND
     - Multiple ground pins — connect at least one

Wiring the VGA Connector
=========================

The resistors limit the current from the GPIO pins and scale the 3.3 V output down toward the 0.7 V maximum the VGA standard expects. A 100 Ω resistor per colour channel (in series between the GPIO and the VGA pin) works reliably across most monitors.

.. list-table::
   :header-rows: 1
   :widths: 30 20 20 30

   * - Connection
     - From
     - To
     - Component
   * - Red channel
     - IO4
     - VGA pin 1
     - 100 Ω resistor in series
   * - Green channel
     - IO5
     - VGA pin 2
     - 100 Ω resistor in series
   * - Blue channel
     - IO6
     - VGA pin 3
     - 100 Ω resistor in series
   * - hSync
     - IO7
     - VGA pin 13
     - Direct wire (no resistor needed)
   * - vSync
     - IO8
     - VGA pin 14
     - Direct wire (no resistor needed)
   * - Ground
     - GND
     - VGA pin 5 (or 6/7/8/10)
     - Direct wire

.. figure:: ../../img/vga_wiring.png
   :align: center
   :width: 400
   :figclass: align-center

   VGA wiring for 3-bit colour with resistor series termination

.. warning::
   The VGA input impedance is 75 Ω to ground. With a 100 Ω series resistor and a 3.3 V GPIO, the voltage at the monitor input is approximately 3.3 × 75 / (100 + 75) ≈ **1.4 V**, which exceeds the 0.7 V VGA specification. Most modern monitors tolerate this (they simply saturate at full brightness), but a proper R-2R resistor ladder producing a true 0–0.7 V range is recommended for permanent installations. For this educational project, 100 Ω resistors and direct connection are acceptable and widely used.

Pin Assignments
===============

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Signal
     - ESP32-S3 GPIO
     - Description
   * - VGA Red
     - IO4
     - Red channel — connect via 100 Ω to VGA pin 1
   * - VGA Green
     - IO5
     - Green channel — connect via 100 Ω to VGA pin 2
   * - VGA Blue
     - IO6
     - Blue channel — connect via 100 Ω to VGA pin 3
   * - VGA hSync
     - IO7
     - Horizontal sync — direct to VGA pin 13
   * - VGA vSync
     - IO8
     - Vertical sync — direct to VGA pin 14
   * - USB D+ / D−
     - IO19 / IO20
     - Built-in USB OTG (internal, no manual wiring)

Critical Arduino IDE Settings
==============================

Both USB host mode and VGA output require specific build settings. Verify all of the following before uploading.

Go to **Tools** in the Arduino IDE and set:

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Setting
     - Required Value
     - Why
   * - USB CDC On Boot
     - **Disabled**
     - Frees the USB OTG peripheral for keyboard host mode
   * - USB Mode
     - **Hardware CDC and JTAG**
     - Uses UART0 for programming and Serial Monitor
   * - Upload Mode
     - **UART0 / Hardware CDC**
     - Ensures the sketch uploads over UART, not the OTG port
   * - PSRAM
     - **Disabled** (for 3-bit mode)
     - The 3-bit framebuffer fits in internal RAM; PSRAM can interfere with the LCD peripheral timing at lower resolutions

.. warning::
   If ``USB CDC on Boot`` is **Enabled**, the keyboard will never be detected and the VGA signal may not initialise correctly. This setting must be **Disabled** before uploading.

Required Libraries
==================

Install both libraries through the Arduino Library Manager (**Tools → Manage Libraries**):

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Library
     - Search term / source
   * - ESP32-S3-VGA (bitluni)
     - Search ``ESP32S3VGA`` — install the library by bitluni. If not found in the manager, download it from ``https://github.com/bitluni/ESP32-S3-VGA`` and install via **Sketch → Include Library → Add .ZIP Library**.
   * - EspUsbHost
     - Search ``EspUsbHost`` by Masayuki Tanaka (tanakamasayuki). Install version 2.x.

.. note::
   ``ESP32-S3-VGA`` is an experimental library that may not yet be registered in the Library Manager. If the search returns no results, use the manual ZIP install method described above.

Writing the Program
===================

Create a new sketch in the Arduino IDE and enter the following code:

.. code-block:: cpp

   #include "ESP32S3VGA.h"
   #include <GfxWrapper.h>           // Bundled with ESP32-S3-VGA — Adafruit GFX API
   #include <Fonts/Font5x7Fixed.h>   // Built-in fixed-width font from the library
   #include "EspUsbHost.h"

   // ── VGA pin configuration (3-bit colour: one pin per channel) ─────────────
   // PinConfig argument order:
   //   r0,r1,r2,r3,r4, g0,g1,g2,g3,g4,g5, b0,b1,b2,b3,b4, hSync, vSync
   // For 3-bit mode, only the MSB slot (index 4 for R/B, index 5 for G)
   // gets a real pin; all others are -1.
   //                          R               G              B        h   v
   const PinConfig VGA_PINS(-1,-1,-1,-1, 4, -1,-1,-1,-1,-1, 5, -1,-1,-1,-1, 6, 7, 8);

   // ── VGA resolution ────────────────────────────────────────────────────────
   const Mode VGA_MODE = Mode::MODE_320x240x60;   // 320×240 at 60 Hz, 4:3

   // ── VGA driver instance ───────────────────────────────────────────────────
   VGA vga;
   GfxWrapper<VGA> gfx(vga, VGA_MODE.hRes, VGA_MODE.vRes);

   // ── Colour constants (3-bit palette) ──────────────────────────────────────
   #define COL_BLACK   vga.RGB(  0,   0,   0)
   #define COL_WHITE   vga.RGB(255, 255, 255)
   #define COL_GREEN   vga.RGB(  0, 255,   0)
   #define COL_CYAN    vga.RGB(  0, 255, 255)
   #define COL_YELLOW  vga.RGB(255, 255,   0)

   // ── Terminal layout ────────────────────────────────────────────────────────
   // Font5x7Fixed: character cell is 6×8 pixels (6 wide, 8 tall with spacing)
   #define CHAR_W       6
   #define CHAR_H       8
   #define HEADER_H    10     // Pixel height of the header bar
   #define TERM_COLS   (VGA_MODE.hRes / CHAR_W)               // 53 columns
   #define TERM_ROWS   ((VGA_MODE.vRes - HEADER_H) / CHAR_H)  // 28 rows

   // ── Shared state (USB task → main loop) ───────────────────────────────────
   static SemaphoreHandle_t bufMutex;

   static String   lines[TERM_ROWS];    // Ring buffer of text lines
   static int      curLine  = 0;        // Index of the current (active) line
   static bool     dirty    = false;    // True when a redraw is needed

   // ── USB host ───────────────────────────────────────────────────────────────
   EspUsbHost usb;

   // ── Helper: advance the terminal to the next line ─────────────────────────
   static void newLine() {
       curLine = (curLine + 1) % TERM_ROWS;
       lines[curLine] = "";              // Clear the newly current line
   }

   // ── USB keyboard callback ─────────────────────────────────────────────────
   // Runs in the EspUsbHost FreeRTOS task — keep it short and fast.
   void onKeyboardEvent(const EspUsbHostKeyboardEvent &event) {
       if (!event.pressed) return;

       xSemaphoreTake(bufMutex, portMAX_DELAY);

       if (event.keycode == 0x28 || event.keycode == 0x58) {
           // Enter / Numpad Enter
           newLine();
       } else if (event.keycode == 0x2A) {
           // Backspace
           if (lines[curLine].length() > 0) {
               lines[curLine].remove(lines[curLine].length() - 1);
           } else if (curLine != 0) {
               lines[curLine] = "";
               curLine = (curLine + TERM_ROWS - 1) % TERM_ROWS;
           }
       } else if (event.keycode == 0x29) {
           // Escape — clear all lines
           for (int i = 0; i < TERM_ROWS; i++) lines[i] = "";
           curLine = 0;
       } else if (event.ascii >= 0x20 && event.ascii <= 0x7E) {
           // Printable character — auto-wrap if line is full
           if ((int)lines[curLine].length() >= TERM_COLS) newLine();
           lines[curLine] += (char)event.ascii;
       }

       dirty = true;
       xSemaphoreGive(bufMutex);
   }

   // ── Draw the header bar ───────────────────────────────────────────────────
   void drawHeader() {
       gfx.fillRect(0, 0, VGA_MODE.hRes, HEADER_H, COL_GREEN);
       gfx.setTextColor(COL_BLACK);
       gfx.setCursor(2, 1);
       gfx.print("VGA Keyboard Terminal");

       // Show terminal dimensions in the top-right corner
       char info[16];
       snprintf(info, sizeof(info), "%dx%d", TERM_COLS, TERM_ROWS);
       gfx.setCursor(VGA_MODE.hRes - (strlen(info) * CHAR_W) - 2, 1);
       gfx.print(info);
   }

   // ── Redraw the entire terminal ─────────────────────────────────────────────
   void redrawTerminal() {
       // Clear the text area
       gfx.fillRect(0, HEADER_H, VGA_MODE.hRes, VGA_MODE.vRes - HEADER_H, COL_BLACK);

       // Render lines top-to-bottom, oldest first
       int startLine = (curLine + 1) % TERM_ROWS;

       for (int row = 0; row < TERM_ROWS; row++) {
           int idx = (startLine + row) % TERM_ROWS;
           int y   = HEADER_H + row * CHAR_H;

           if (idx == curLine) {
               // Active line: draw in white with a blinking underscore cursor
               gfx.setTextColor(COL_WHITE);
               String lineWithCursor = lines[curLine] + "_";
               gfx.setCursor(0, y);
               gfx.print(lineWithCursor);
           } else {
               // Inactive lines in cyan for readability
               gfx.setTextColor(COL_CYAN);
               gfx.setCursor(0, y);
               gfx.print(lines[idx]);
           }
       }

       vga.show();   // Push the completed frame to the display
   }

   // ── Setup ──────────────────────────────────────────────────────────────────
   void setup() {
       Serial.begin(115200);

       // Initialise VGA
       if (!vga.init(VGA_PINS, VGA_MODE, 8, 2)) {
           // init() failed — likely a pin conflict or PSRAM issue
           Serial.println(F("VGA init failed! Check pin config and PSRAM setting."));
           while (true) { delay(1000); }
       }
       vga.start();

       // Set font for the GfxWrapper text API
       gfx.setFont(&Font5x7Fixed);
       gfx.setTextWrap(false);    // We handle wrapping manually

       // Draw the initial screen
       gfx.fillScreen(COL_BLACK);
       drawHeader();
       gfx.setTextColor(COL_YELLOW);
       gfx.setCursor(4, HEADER_H + CHAR_H * 2);
       gfx.print("Plug in USB keyboard...");
       vga.show();

       // Initialise shared state
       for (int i = 0; i < TERM_ROWS; i++) lines[i] = "";
       bufMutex = xSemaphoreCreateMutex();

       // Start USB host
       usb.onKeyboard(onKeyboardEvent);
       if (!usb.begin()) {
           gfx.fillScreen(COL_BLACK);
           drawHeader();
           gfx.setTextColor(COL_WHITE);
           gfx.setCursor(4, HEADER_H + CHAR_H * 2);
           gfx.print("USB Host failed!");
           gfx.setCursor(4, HEADER_H + CHAR_H * 3);
           gfx.print("Check CDC setting.");
           vga.show();
           while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
       }
   }

   // ── Loop ───────────────────────────────────────────────────────────────────
   // The USB host task runs in the background.
   // This loop only redraws the terminal when new keyboard data has arrived.
   void loop() {
       if (dirty) {
           xSemaphoreTake(bufMutex, portMAX_DELAY);
           dirty = false;
           drawHeader();
           redrawTerminal();
           xSemaphoreGive(bufMutex);
       }
       vTaskDelay(pdMS_TO_TICKS(30));   // ~33 fps ceiling
   }

Uploading the Program
^^^^^^^^^^^^^^^^^^^^^

1. **Verify the IDE settings first.** Confirm ``USB CDC on Boot`` is **Disabled** and ``PSRAM`` is **Disabled** (for 3-bit mode) before doing anything else.
2. Wire the VGA connector and resistors as described in the *Wiring* section above. The sketch does not depend on the VGA hardware for programming.
3. Connect the board to your computer using the USB cable (the UART programming port).
4. Select **ESP32S3 Dev Module** from **Tools → Board → esp32**.
5. Select the correct serial port from **Tools → Port**.
6. Click the **Upload** button and wait for completion.
7. Disconnect the programming cable and power the board via a USB power supply.
8. Connect the VGA cable from the board to the monitor.
9. Plug the USB keyboard into the board's USB OTG port.
10. Power on the monitor and switch it to the correct VGA input if needed.

.. figure:: ../../img/vga_keyboard_terminal.png
   :align: center
   :figclass: align-center

   The keyboard terminal running on a VGA monitor

Expected Result
^^^^^^^^^^^^^^^

After powering the board with both the monitor and keyboard connected:

* The monitor shows a green header bar reading **"VGA Keyboard Terminal"** and the terminal dimensions in the top-right corner.
* The message **"Plug in USB keyboard…"** appears in yellow while waiting for the keyboard.
* Once the keyboard is detected, the cursor (an underscore ``_``) appears on the first text line.
* Pressing any printable key displays the character immediately on screen.
* Pressing **Enter** moves the cursor to the next line.
* Pressing **Backspace** removes the last character.
* Pressing **Escape** clears all lines and returns the cursor to the top.
* When all rows are filled, older lines scroll off the top as new lines are added.
* The active line is rendered in **white**; previous lines appear in **cyan** for easy visual distinction.

How the Code Works
^^^^^^^^^^^^^^^^^^

**VGA Initialisation**

The ``PinConfig`` struct defines every bit lane of every colour channel plus the two sync signals. The argument order is: ``r0–r4, g0–g5, b0–b4, hSync, vSync`` (a total of 17 values). In 3-bit mode, each channel uses only its MSB slot — for Red that is position 4 (the 5th value), for Green position 10 (6 bits of green), for Blue position 14 (the 15th value):

.. code-block:: cpp

   //                          R               G              B        h   v
   const PinConfig VGA_PINS(-1,-1,-1,-1, 4, -1,-1,-1,-1,-1, 5, -1,-1,-1,-1, 6, 7, 8);

``vga.init(pins, mode, colourDepth, frameBufferCount)`` allocates the framebuffer and configures the LCD peripheral for the requested resolution and colour depth. Using ``frameBufferCount = 2`` enables double buffering: while one buffer is being displayed, the CPU draws into the other. Calling ``vga.show()`` at the end of each frame swaps them, producing a tear-free image.

**The GfxWrapper**

``GfxWrapper<VGA>`` wraps the VGA driver with the Adafruit GFX API, giving access to familiar functions like ``setCursor()``, ``print()``, ``setTextColor()``, and ``fillRect()``. This avoids working with raw pixel coordinates for every character.

**The Terminal Ring Buffer**

All text lines are stored in a fixed-size ``String`` array indexed modulo ``TERM_ROWS``. When ``newLine()`` is called, the index advances by one (wrapping around), and the newly current line is cleared — overwriting the oldest stored line. This gives the appearance of scrolling without any memory copying.

.. code-block:: cpp

   static void newLine() {
       curLine = (curLine + 1) % TERM_ROWS;
       lines[curLine] = "";
   }

``redrawTerminal()`` starts rendering from the line *after* the current one (the oldest), proceeding top to bottom, so the newest line always appears at the bottom of the screen.

**Frame Submission with ``vga.show()``**

Unlike U8g2 (which sends the entire buffer to an external display chip over I²C), the VGA driver holds the framebuffer in internal RAM and streams it to the monitor continuously using DMA. ``vga.show()`` signals the driver to swap the back buffer into the active display buffer. This call must be made **after** all drawing for a frame is complete.

**Keyboard Callback and Mutex**

The EspUsbHost callback runs in a FreeRTOS task on a separate core. The main ``loop()`` also accesses the ``lines[]`` array. A FreeRTOS mutex (the same pattern as Project 4) prevents concurrent access:

.. code-block:: cpp

   xSemaphoreTake(bufMutex, portMAX_DELAY);
   // ... modify shared state ...
   xSemaphoreGive(bufMutex);

The ``dirty`` flag avoids calling ``vga.show()`` (which takes time) on every loop iteration — the display is only redrawn when the keyboard callback has written new data.

VGA vs. OLED — Key Differences
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Property
     - OLED (Project 4)
     - VGA (this project)
   * - Display interface
     - I²C (2 wires)
     - Parallel RGB + sync (5 wires minimum)
   * - Resolution
     - 128×64 pixels
     - 320×240 (or up to 800×600)
   * - Framebuffer location
     - Inside display chip
     - ESP32-S3 internal RAM
   * - Frame update
     - ``sendBuffer()`` via I²C burst
     - ``vga.show()`` swaps DMA buffer
   * - External components
     - None (I²C built-in)
     - 3–16 resistors for DAC
   * - Colour depth
     - Monochrome
     - 8 colours (3-bit) to 65 536 (16-bit)
   * - Typical use
     - Status display, HMI
     - Gaming, terminal, video output

Experiment
^^^^^^^^^^

Try the following modifications to explore the capabilities of VGA output further.

* **Change the resolution** to a higher preset for a larger terminal:

  .. code-block:: cpp

     const Mode VGA_MODE = Mode::MODE_400x300x60;  // More text rows and columns

  Remember to recalculate ``TERM_COLS`` and ``TERM_ROWS`` if you change the mode. Also check whether ``PSRAM`` needs to be enabled in the IDE for higher resolutions.

* **Add colour-coded text** by cycling through the 3-bit palette depending on which line is being drawn:

  .. code-block:: cpp

     // Inside the redrawTerminal loop:
     uint16_t colours[] = { COL_CYAN, COL_WHITE, COL_YELLOW, COL_GREEN };
     gfx.setTextColor(colours[row % 4]);

* **Display a live clock** in the header alongside the terminal title:

  .. code-block:: cpp

     // Add to drawHeader():
     unsigned long secs = millis() / 1000;
     char clock[12];
     snprintf(clock, sizeof(clock), "%02lu:%02lu:%02lu",
              secs / 3600, (secs % 3600) / 60, secs % 60);
     gfx.setCursor(VGA_MODE.hRes / 2 - 20, 1);
     gfx.print(clock);

  Call ``drawHeader()`` in the ``loop()`` unconditionally (not only when ``dirty``) to keep the clock ticking even between keystrokes.

* **Display the raw HID keycode** for non-printable keys so you can inspect what special keys send:

  .. code-block:: cpp

     } else if (event.keycode != 0) {
         char kc[6];
         snprintf(kc, sizeof(kc), "[%02X]", event.keycode);
         lines[curLine] += kc;
     }

* **Draw a full-screen test pattern** on startup by filling rectangles of different colours before entering the keyboard loop — a good first test to confirm the VGA connection and resistor values are correct.

Troubleshooting
===============

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Symptom
     - What to check
   * - Monitor shows "No Signal"
     - Verify hSync (IO7) and vSync (IO8) wiring to VGA pins 13 and 14. Confirm ground is connected to at least one VGA ground pin. Check that the monitor supports 320×240 at 60 Hz.
   * - Image is visible but colours are wrong or missing
     - Confirm R, G, B resistors are correctly placed. Check that each colour pin is wired to its correct VGA connector pin (1=R, 2=G, 3=B).
   * - Image is present but very dim
     - Increase the GPIO drive strength in code: ``gpio_set_drive_capability((gpio_num_t)4, GPIO_DRIVE_CAP_3);`` for each VGA pin. Alternatively, reduce the series resistor value (try 68 Ω).
   * - ``vga.init()`` fails (Serial prints error)
     - Confirm PSRAM is **Disabled** in the IDE for 3-bit mode. Check for pin conflicts with other peripherals.
   * - Keyboard not detected (cursor does not appear)
     - Confirm ``USB CDC on Boot`` is **Disabled**. Re-upload the sketch after changing the setting.
   * - Screen tears or flickers during redraws
     - This occurs when single-buffered mode is used. Confirm ``frameBufferCount = 2`` is passed to ``vga.init()``. Alternatively, reduce the resolution.
   * - Characters appear garbled or at wrong positions
     - Confirm ``gfx.setTextWrap(false)`` is set. Recalculate ``TERM_COLS`` and ``TERM_ROWS`` to match the actual font metrics.

Summary
=======

In this tutorial, you combined the ESP32-S3's USB OTG host controller with its LCD peripheral to build a complete text terminal: a USB keyboard as input and a VGA monitor as output.

Key concepts covered include:

* How the ESP32-S3 generates a VGA signal using its LCD peripheral and a resistor DAC, without any external display driver chip.
* Configuring bitluni's **ESP32-S3-VGA** library with a custom ``PinConfig`` for 3-bit colour operation.
* Using ``vga.init()``, ``vga.start()``, and ``vga.show()`` to initialise and update a double-buffered VGA framebuffer.
* Wrapping the VGA driver with **GfxWrapper** to access the Adafruit GFX text and drawing API.
* Implementing a scrolling ring-buffer terminal in VGA, applying the same pattern used in the OLED terminal lesson.
* Combining USB host keyboard input (EspUsbHost, FreeRTOS callback, mutex) with a VGA rendering loop in the same sketch.
* The mandatory IDE settings: **USB CDC on Boot → Disabled**, **PSRAM → Disabled** (for 3-bit mode).

This project represents the most capable terminal in this series: a full-colour, freely resolvable display driven entirely by the ESP32-S3 — no display module, no HDMI adapter, and no operating system required.
