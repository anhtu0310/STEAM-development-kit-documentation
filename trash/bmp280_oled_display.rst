*************************************************************
Project 5 : BMP280 Sensor Readings on the OLED Display
*************************************************************


Introduction
============

In this project, you will add a BMP280 temperature and pressure sensor to the same I²C bus already used by the on-board SH1106 OLED display. This is one of the most useful properties of the I²C protocol: multiple devices can share just two wires, as long as each has a unique address.

By the end of this tutorial, you will know how to:

* Connect a second I²C device onto an already-occupied I²C bus.
* Understand how I²C addressing allows multiple devices to coexist on shared SDA/SCL lines.
* Read temperature and pressure from a BMP280 sensor.
* Render live sensor readings as formatted text on the SH1106 OLED.
* Diagnose I²C address conflicts and wiring problems.

Requirements
============

Before starting, make sure you have the following:

* The STEAM development kit or The STEAM standalone microcontroller board
* A BMP280 breakout board (Bosch BMP280, I²C mode)
* 4 jumper wires (VCC, GND, SCL, SDA)
* USB cable
* Arduino IDE with ESP32 toolchain installed

The SH1106 OLED display is already integrated on the STEAM development board. Only the BMP280 sensor needs to be wired in.

.. figure:: ../../img/MCU_board.png
   :align: center
   :width: 400
   :figclass: align-center

   The interfacing description of the microcontroller board

**Pin assignments used in this project:**

.. list-table::
   :header-rows: 1
   :widths: 30 20 50

   * - Signal
     - ESP32-S3 GPIO
     - Description
   * - I²C SCL
     - IO48
     - Shared clock line — OLED and BMP280
   * - I²C SDA
     - IO47
     - Shared data line — OLED and BMP280

.. note::
   Both the OLED and the BMP280 connect to the **same two pins** (IO48 and IO47). This is the defining feature of I²C: every device on the bus shares SCL and SDA, and each is distinguished by its own address rather than its own set of wires.

Wiring the BMP280
==================

Connect the BMP280 breakout board to the existing I²C bus alongside the OLED:

.. list-table::
   :header-rows: 1
   :widths: 25 25 50

   * - BMP280 Pin
     - Connects To
     - Notes
   * - VCC
     - 3.3V
     - The BMP280 die operates at 1.71–3.6V; do not connect to 5V on bare modules
   * - GND
     - GND
     - Common ground with the board
   * - SCL
     - IO48
     - Same pin the OLED uses
   * - SDA
     - IO47
     - Same pin the OLED uses
   * - SDO
     - GND *or* leave floating
     - Sets the I²C address — see below

.. warning::
   Some bare BMP280 modules (without an on-board regulator) accept **3.3V only**. Applying 5V can permanently damage the sensor. Check your specific module's silkscreen or datasheet before connecting power.

.. figure:: ../../img/bmp280_oled_wiring.png
   :align: center
   :width: 400
   :figclass: align-center

   BMP280 and SH1106 OLED sharing the same I²C bus

Background: Key Concepts
=========================

How Multiple Devices Share One I²C Bus
-----------------------------------------

I²C (Inter-Integrated Circuit) is a multi-drop bus: every device's SCL pin connects to every other device's SCL pin, and likewise for SDA. There is no per-device wiring — only two shared lines plus power and ground.

When the ESP32-S3 wants to talk to a specific device, it first transmits that device's **7-bit address** on the bus. Every device listens to this address but only the one matching it responds; everyone else stays silent. This is why two devices can coexist on the same two wires: addressing, not separate wiring, is what tells them apart.

.. figure:: ../../img/i2c_bus_topology.png
   :align: center
   :width: 400
   :figclass: align-center

   Multiple I²C devices sharing one set of SCL/SDA lines, distinguished by address

BMP280 I²C Addressing
------------------------

The BMP280 has exactly two possible addresses, selected by the voltage on its **SDO** pin:

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - SDO Pin Connection
     - I²C Address
     - Typical Default
   * - Floating or tied to VCC
     - ``0x77``
     - Most breakout boards default here
   * - Tied to GND
     - ``0x76``
     - Common alternate address

.. tip::
   If you are unsure which address your specific module uses, run the **I²C scanner** sketch in the Experiment section below. It will report the exact address found on the bus, removing any guesswork.

The SH1106 OLED on this board uses I²C address ``0x3C`` (or sometimes ``0x3D``), which does not overlap with either BMP280 address — so no conflict exists. As a rule, any two devices on the same bus simply need *different* addresses, regardless of what those addresses are.

What the BMP280 Measures
---------------------------

The BMP280 is a digital barometric pressure and temperature sensor. It does **not** measure humidity — for that, the related BME280 sensor is needed. From its two raw measurements, a third value — approximate altitude — can be derived by comparing the measured pressure to a known sea-level reference pressure.

Required Libraries
==================

Install the following from the Arduino Library Manager (**Tools → Manage Libraries**):

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Library
     - Search term in Library Manager
   * - Adafruit BMP280 Library
     - ``Adafruit BMP280``
   * - Adafruit Unified Sensor
     - ``Adafruit Unified Sensor`` (required dependency of the BMP280 library)
   * - U8g2
     - ``U8g2`` by oliver (olikraus)

.. note::
   The Arduino Library Manager will usually offer to install **Adafruit Unified Sensor** automatically as a dependency when you install **Adafruit BMP280**. Accept this prompt — the BMP280 library will not compile without it.

Writing the Program
===================

Create a new sketch in the Arduino IDE and enter the following code:

.. code-block:: cpp

   #include <Wire.h>
   #include <Adafruit_Sensor.h>
   #include <Adafruit_BMP280.h>
   #include <U8g2lib.h>

   // ── Pin definitions ────────────────────────────────────────────────────────
   #define I2C_SCL 48
   #define I2C_SDA 47

   // ── BMP280 I2C addresses to try, in order ──────────────────────────────────
   #define BMP280_ADDR_PRIMARY   0x76
   #define BMP280_ADDR_SECONDARY 0x77

   // ── Sea-level reference pressure for altitude calculation (hPa) ────────────
   // Adjust to your local weather report for more accurate altitude readings.
   #define SEALEVEL_HPA 1013.25

   // ── Display: SH1106 128×64, full frame buffer, hardware I²C ───────────────
   U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
       U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);

   // ── Sensor ─────────────────────────────────────────────────────────────────
   Adafruit_BMP280 bmp;   // Uses the default Wire (I2C) interface

   // ── Globals ────────────────────────────────────────────────────────────────
   bool sensorFound = false;

   // ── Helper: try both possible BMP280 addresses ─────────────────────────────
   bool initBmp280() {
       if (bmp.begin(BMP280_ADDR_PRIMARY)) {
           Serial.println(F("BMP280 found at 0x76"));
           return true;
       }
       if (bmp.begin(BMP280_ADDR_SECONDARY)) {
           Serial.println(F("BMP280 found at 0x77"));
           return true;
       }
       return false;
   }

   // ── Helper: draw an error screen ────────────────────────────────────────────
   void showError(const char* line1, const char* line2) {
       display.clearBuffer();
       display.setFont(u8g2_font_6x10_tr);
       display.drawStr(0, 24, line1);
       display.drawStr(0, 38, line2);
       display.sendBuffer();
   }

   // ── Setup ──────────────────────────────────────────────────────────────────
   void setup() {
       Serial.begin(115200);

       // The OLED's U8g2 instance already initialises the I2C bus on
       // IO48/IO47 internally when display.begin() is called below.
       // Adafruit_BMP280's I2C constructor uses the same global Wire object,
       // so once the bus is initialised, both devices share it automatically.
       display.begin();
       display.setFont(u8g2_font_6x10_tr);
       display.clearBuffer();
       display.drawStr(10, 30, "Starting sensor...");
       display.sendBuffer();

       sensorFound = initBmp280();

       if (!sensorFound) {
           Serial.println(F("BMP280 not found. Check wiring/address."));
           showError("BMP280 not found!", "Check wiring/SDO pin");
           return;   // Continue to loop(); error screen stays until power cycle
       }

       // Recommended sampling settings from the Bosch datasheet for
       // general-purpose weather monitoring.
       bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,   // Temperature oversampling
                        Adafruit_BMP280::SAMPLING_X16,  // Pressure oversampling
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
   }

   // ── Loop ───────────────────────────────────────────────────────────────────
   void loop() {
       if (!sensorFound) {
           // Sensor was not detected at boot; nothing to update.
           delay(1000);
           return;
       }

       float temperature = bmp.readTemperature();        // °C
       float pressure    = bmp.readPressure() / 100.0F;   // Pa → hPa
       float altitude    = bmp.readAltitude(SEALEVEL_HPA); // m

       // ── Render to OLED ──
       display.clearBuffer();

       // Header bar
       display.setDrawColor(1);
       display.drawBox(0, 0, 128, 12);
       display.setDrawColor(0);
       display.drawStr(2, 10, "BMP280 Sensor");
       display.setDrawColor(1);

       // Temperature
       char buf[24];
       display.setFont(u8g2_font_7x14_tr);
       snprintf(buf, sizeof(buf), "Temp: %.1f C", temperature);
       display.drawStr(0, 28, buf);

       // Pressure
       snprintf(buf, sizeof(buf), "Pres: %.1f hPa", pressure);
       display.drawStr(0, 44, buf);

       // Altitude
       snprintf(buf, sizeof(buf), "Alt:  %.0f m", altitude);
       display.drawStr(0, 60, buf);

       display.sendBuffer();

       // Also print to Serial for logging/debugging
       Serial.printf("T=%.2f C  P=%.2f hPa  Alt=%.1f m\n",
                      temperature, pressure, altitude);

       delay(1000);   // Update once per second
   }

Uploading the Program
^^^^^^^^^^^^^^^^^^^^^

1. Connect the board to your computer using the USB cable.
2. Open the Arduino IDE.
3. Select **ESP32S3 Dev Module** from **Tools → Board → esp32**.
4. Select the correct serial port from **Tools → Port**.
5. Click the **Upload** button and wait for completion.
6. Open the **Serial Monitor** (Tools → Serial Monitor, baud 115200) to see logged readings alongside the OLED display.

.. figure:: ../../img/bmp280_oled_display.png
   :align: center
   :figclass: align-center

   Live BMP280 readings shown on the SH1106 OLED

Expected Result
^^^^^^^^^^^^^^^

After uploading the program:

* The OLED briefly shows **"Starting sensor…"**, then switches to live readings.
* The display shows a header bar labelled **"BMP280 Sensor"**, followed by three lines: **Temp**, **Pres** (pressure), and **Alt** (altitude).
* Values update once per second.
* Breathing on the sensor, or gently warming it with your fingers, causes the temperature reading to rise within a few seconds.
* If you wired the BMP280 incorrectly or it is not detected, the OLED shows **"BMP280 not found!"** instead of readings.

How the Code Works
^^^^^^^^^^^^^^^^^^

**Sharing the I²C Bus**

The key insight in this lesson is that **no special code is needed to "share" the bus** — I²C is inherently a shared bus by design. The U8g2 display object initialises the I²C peripheral (``Wire``) on IO48/IO47 when ``display.begin()`` runs. The ``Adafruit_BMP280 bmp;`` object, when constructed without arguments, uses that same global ``Wire`` instance. As long as ``display.begin()`` runs before ``bmp.begin()``, both devices communicate correctly over the same two wires.

.. code-block:: cpp

   display.begin();      // Initialises I2C on IO48 (SCL) / IO47 (SDA)
   sensorFound = initBmp280();  // Re-uses the same I2C bus

**Trying Both Possible Addresses**

Because BMP280 modules ship with different SDO wiring, the code attempts both standard addresses in turn:

.. code-block:: cpp

   bool initBmp280() {
       if (bmp.begin(BMP280_ADDR_PRIMARY)) return true;    // Try 0x76
       if (bmp.begin(BMP280_ADDR_SECONDARY)) return true;  // Try 0x77
       return false;
   }

This makes the sketch work regardless of which address your specific module defaults to, without requiring you to first identify it manually.

**Sensor Sampling Configuration**

``bmp.setSampling()`` configures the internal oversampling and filtering of the BMP280's ADC. Higher oversampling (e.g., ``SAMPLING_X16`` for pressure) produces smoother, less noisy readings at the cost of slightly slower conversion — a worthwhile trade-off for a display that updates once per second.

**Reading and Displaying Values**

Each loop iteration calls three read functions, each of which triggers a fresh conversion on the sensor:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Function
     - Returns
   * - ``bmp.readTemperature()``
     - Temperature in degrees Celsius
   * - ``bmp.readPressure()``
     - Pressure in **Pascals** — divided by 100 in the code to convert to hPa
   * - ``bmp.readAltitude(SEALEVEL_HPA)``
     - Estimated altitude in metres, calculated from the pressure difference against the supplied sea-level reference

The values are formatted into fixed strings with ``snprintf()`` and drawn onto the OLED with ``drawStr()``, then sent to the display in one batch via ``sendBuffer()`` — the same full-frame-buffer pattern used in the USB-keyboard lesson.

Experiment
^^^^^^^^^^

Try the following modifications to deepen your understanding.

* **Run an I²C bus scanner** to see both devices' addresses directly. This is invaluable for diagnosing wiring issues:

  .. code-block:: cpp

     #include <Wire.h>

     void setup() {
         Serial.begin(115200);
         Wire.begin(47, 48);   // SDA, SCL
         Serial.println("Scanning I2C bus...");
         for (byte addr = 1; addr < 127; addr++) {
             Wire.beginTransmission(addr);
             if (Wire.endTransmission() == 0) {
                 Serial.printf("Found device at 0x%02X\n", addr);
             }
         }
     }

     void loop() {}

  Run this sketch alone (without the OLED/BMP280 sketch) to confirm both ``0x3C`` (OLED) and ``0x76`` or ``0x77`` (BMP280) appear in the results.

* **Add a graphing history** of temperature over the last 60 seconds, drawn as a simple line graph using ``display.drawLine()`` across the bottom third of the screen.

* **Switch to Fahrenheit** for the temperature display:

  .. code-block:: cpp

     float tempF = temperature * 9.0 / 5.0 + 32.0;
     snprintf(buf, sizeof(buf), "Temp: %.1f F", tempF);

* **Add a high/low tracker** that remembers the minimum and maximum temperature seen since power-on, and display them as a third line:

  .. code-block:: cpp

     static float minTemp = 1000, maxTemp = -1000;
     if (temperature < minTemp) minTemp = temperature;
     if (temperature > maxTemp) maxTemp = temperature;

* **Calibrate altitude** to your actual elevation. Look up your local sea-level-adjusted pressure (available from most weather websites) and update ``SEALEVEL_HPA`` for a more accurate altitude reading at your specific location and weather conditions.

Troubleshooting
===============

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Symptom
     - What to check
   * - OLED shows "BMP280 not found!"
     - Confirm VCC, GND, SCL, SDA wiring. Run the I²C scanner sketch above to verify the sensor responds at all.
   * - I²C scanner finds the OLED but not the BMP280
     - Check that BMP280 VCC is receiving power (measure with a multimeter). Confirm SDO is tied to GND or VCC, not floating on a breadboard with a poor connection.
   * - I²C scanner finds neither device
     - Verify SCL=IO48 and SDA=IO47 are correctly wired. Confirm common ground between all devices.
   * - Readings are stuck at a constant, unrealistic value
     - The sensor may not be properly calibrated at startup. Power-cycle the board and allow a few seconds for the first reading.
   * - Temperature readings seem several degrees too high
     - This is common when the sensor is mounted close to the ESP32-S3 or voltage regulator, which generate heat. Mount the sensor away from heat-generating components, or apply a calibration offset in software.
   * - Display flickers or sensor readings occasionally glitch
     - Long I²C wires or breadboard connections can introduce noise on a shared bus. Keep wires short, and ensure both devices share a solid common ground.

Summary
=======

In this tutorial, you connected a BMP280 sensor onto the same I²C bus as the SH1106 OLED display, using nothing more than two shared wires (SCL and SDA) for both devices. The sensor's address (``0x76`` or ``0x77``) keeps its traffic distinct from the OLED's address (``0x3C``), so both communicate independently over the same physical bus.

Key concepts covered include:

* How I²C's address-based protocol allows multiple devices to share one set of SCL/SDA wires.
* Wiring a second I²C peripheral onto a bus already in use.
* Installing and using the **Adafruit BMP280** library alongside its **Adafruit Unified Sensor** dependency.
* Trying multiple possible I²C addresses in software to handle hardware variation.
* Reading temperature, pressure, and derived altitude from the BMP280.
* Rendering live, updating sensor data on the OLED using the same frame-buffer pattern from earlier lessons.
* Using an I²C bus scanner sketch to diagnose wiring and addressing problems.

This project demonstrates the core advantage of I²C over single-purpose wiring: as your project grows to include more sensors, you can keep adding devices to the same two wires, provided each one has — or can be configured to use — a unique address.
