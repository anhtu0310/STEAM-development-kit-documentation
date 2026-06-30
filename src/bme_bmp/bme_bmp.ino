#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

// ── I²C pin definitions (shared by OLED and BMP280) ──────────────────────
#define I2C_SCL 48
#define I2C_SDA 47

// ── Device I²C addresses ──────────────────────────────────────────────────
#define OLED_ADDRESS 0x3C
#define bmp_ADDRESS  0x77   // Change to 0x77 if your board uses that address

// ── Sea-level pressure reference for altitude calculation ─────────────────
#define SEALEVEL_HPA 1013.25

// ── Temperature self-heating correction (°C to subtract) ─────────────────
// The BMP280 sits near other components that generate heat.
// Measure the true ambient temperature with a reference thermometer,
// subtract the BMP280 reading, and enter the difference here.
#define TEMP_OFFSET 0.0    // e.g. set to 3.0 if BMP280 reads 3°C too high

// ── Display: SH1106 128×64, full buffer, hardware I²C ────────────────────
// When passing a Wire pointer explicitly, use the SW constructor with
// U8X8_PIN_NONE for all pins; pin configuration comes from Wire.begin().
// U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
//     U8G2_R0,        // No rotation
//     U8X8_PIN_NONE,  // No hardware reset pin
//     I2C_SCL,        // SCL — IO48
//     I2C_SDA         // SDA — IO47
// );
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// ── BMP280 sensor object ──────────────────────────────────────────────────
Adafruit_BMP280 bmp;

// ── Update interval ───────────────────────────────────────────────────────
#define UPDATE_INTERVAL_MS 1000
unsigned long lastUpdate = 0;

// ── Helper: draw the sensor dashboard ────────────────────────────────────
void drawDashboard(float tempC, float pressure, float altitude) {
    char buf[24];

    display.clearBuffer();

    // ── Header bar ────────────────────────────────────────────────────────
    display.setDrawColor(1);
    display.drawBox(0, 0, 128, 13);
    display.setDrawColor(0);
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(14, 10, "Environment Monitor");
    display.setDrawColor(1);

    // ── Temperature ───────────────────────────────────────────────────────
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 25, "Temp:");
    display.setFont(u8g2_font_profont17_tr);  // Slightly larger for the value
    snprintf(buf, sizeof(buf), "%.1f C", tempC);
    display.drawStr(40, 25, buf);

    // ── Pressure ──────────────────────────────────────────────────────────
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 39, "Pres:");
    display.setFont(u8g2_font_profont17_tr);
    snprintf(buf, sizeof(buf), "%.0f hPa", pressure);
    display.drawStr(40, 39, buf);

    // ── Altitude (small, bottom-right) ───────────────────────────────────
    display.setFont(u8g2_font_5x7_tr);
    snprintf(buf, sizeof(buf), "Alt: %.0fm", altitude);
    display.drawStr(72, 49, buf);

    display.sendBuffer();
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Configure the I²C bus with custom pins BEFORE initialising any device.
    // Both U8g2 and the BMP280 library will use this configured bus.
    Wire.begin(I2C_SDA, I2C_SCL);

    // Initialise the OLED display
    display.begin();

    // Show a startup message while the BMP280 initialises
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(10, 28, "STEAM Dev Board");
    display.drawStr(10, 42, "Finding BMP280...");
    display.sendBuffer();
    bmp = Adafruit_BMP280(&Wire);
    // Initialise the BMP280, passing the configured Wire instance
    if (!bmp.begin()) {
        // Sensor not found — show error and halt
        display.clearBuffer();
        display.setFont(u8g2_font_6x10_tr);
        display.drawStr(0, 20, "BMP280 not found!");
        display.drawStr(0, 34, "Check address:");
        display.drawStr(0, 48, "Try 0x76 or 0x77");
        display.sendBuffer();
        Serial.println("ERROR: BMP280 not found. Check I2C address and wiring.");
        while (true) { delay(1000); }
    }

    Serial.println("BMP280 initialised successfully.");
    Serial.print("Sensor chip ID: 0x");
    Serial.println(bmp.sensorID(), HEX);   // 0x60 = BMP280, 0x56-0x58 = BMP280

    delay(500);  // Brief pause so the startup message is visible
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    if (now - lastUpdate >= UPDATE_INTERVAL_MS) {
        lastUpdate = now;

        // Read sensor values
        float tempC    = bmp.readTemperature() - TEMP_OFFSET;
        float pressure = bmp.readPressure() / 100.0F;   // Pa → hPa
        float altitude = bmp.readAltitude(SEALEVEL_HPA);

        // Update display
        drawDashboard(tempC, pressure, altitude);

        // Echo to Serial Monitor for debugging
        Serial.printf("Temp: %.1f°C  Pres: %.1f hPa  Alt: %.0fm\n",
                      tempC, pressure, altitude);
    }
}
