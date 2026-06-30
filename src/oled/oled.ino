#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// Define custom I2C pins for the OLED
#define SDA_PIN 47
#define SCL_PIN 48

// Define the button pin
#define BUT_PIN 0

// Initialize the u8g2 object for the SH1106 OLED
// 'F' stands for Full Framebuffer (best for ESP32 as it has plenty of RAM)
// HW_I2C tells the library to use hardware-accelerated I2C
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

bool last_but_sts = HIGH;
int display_state = 0; // 0 = Text Screen, 1 = Graphics Screen

void setup() {
    pinMode(BUT_PIN, INPUT);

    // Map the ESP32's hardware I2C to our specific pins
    Wire.begin(SDA_PIN, SCL_PIN);

    // Start the display
    u8g2.begin();
}

void loop() {
    // Read the current state of the active-LOW button
    bool current_but_sts = digitalRead(BUT_PIN);

    // Detect button press (falling edge)
    if (last_but_sts == HIGH && current_but_sts == LOW) {
        // Toggle between state 0 and 1
        display_state = (display_state + 1) % 2;
        delay(50); // Software debounce
    }

    last_but_sts = current_but_sts;

    // Render the OLED Screen
    u8g2.clearBuffer(); // 1. Clear the internal memory

    if (display_state == 0) {
        drawTextScreen();
    } else {
        drawGraphicsScreen();
    }

    u8g2.sendBuffer();  // 2. Push the memory to the physical display
}

// Function to render text
void drawTextScreen() {
    // Set a standard readable font
    u8g2.setFont(u8g2_font_ncenB08_tr);

    // drawStr(X, Y, "Text") - Note: Y is the bottom-left baseline of the text
    u8g2.drawStr(10, 20, "Hello, STEAM!");
    u8g2.drawStr(10, 40, "SH1106 OLED Test");
    u8g2.drawStr(10, 60, "Press the button ->");
}

// Function to render shapes
void drawGraphicsScreen() {
    // Draw a border around the edge of the 128x64 screen
    u8g2.drawFrame(0, 0, 128, 64);

    // Draw a hollow circle in the center (X=64, Y=32, Radius=20)
    u8g2.drawCircle(64, 32, 20, U8G2_DRAW_ALL);

    // Draw a filled square on the left side
    u8g2.drawBox(15, 22, 20, 20);

    // Add a small label
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(95, 36, "Art!");
}
