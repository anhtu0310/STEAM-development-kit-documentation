#include <Wire.h>
#include <U8g2lib.h>
#include "EspUsbHost.h"

// ── Pin definitions ────────────────────────────────────────────────────────
#define OLED_SCL 48
#define OLED_SDA 47

// ── Display: SH1106 128×64, full frame buffer, hardware I²C ───────────────
// Constructor argument order: rotation, reset pin, SCL pin, SDA pin
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,          // No rotation
    U8X8_PIN_NONE,    // No hardware reset pin
    OLED_SCL,         // IO48
    OLED_SDA          // IO47
);

// ── USB host ───────────────────────────────────────────────────────────────
EspUsbHost usb;

// ── Shared state (written by USB task, read by main loop) ──────────────────
// Protected by a FreeRTOS mutex to avoid race conditions.
static SemaphoreHandle_t bufMutex;

#define MAX_LINE_CHARS 21        // 128px / ~6px per char at font size 6×10
#define MAX_LINES       5        // 5 usable text rows on a 64px display

static String lines[MAX_LINES];  // Ring buffer of text lines
static int    currentLine = 0;   // Which line is being typed into
static bool   displayDirty = false; // True when the display needs refresh

// ── Helper: advance to the next line (like pressing Enter) ────────────────
static void newLine() {
    currentLine = (currentLine + 1) % MAX_LINES;
    lines[currentLine] = "";   // Clear the new current line
}

// ── USB keyboard callback ─────────────────────────────────────────────────
// Called from the EspUsbHost FreeRTOS task on every key event.
// Keep this function fast — do not call display functions here.
void onKeyboardEvent(const EspUsbHostKeyboardEvent &event) {
    if (!event.pressed) return;   // Ignore key-release events

    xSemaphoreTake(bufMutex, portMAX_DELAY);

    if (event.keycode == 0x28 || event.keycode == 0x58) {
        // Enter / Numpad Enter → move to the next line
        newLine();
    } else if (event.keycode == 0x2A) {
        // Backspace → remove last character from current line
        if (lines[currentLine].length() > 0) {
            lines[currentLine].remove(lines[currentLine].length() - 1);
        } else if (currentLine != 0) {
            // Line is empty — go back to the previous line
            lines[currentLine] = "";
            currentLine = (currentLine + MAX_LINES - 1) % MAX_LINES;
        }
    } else if (event.ascii >= 0x20 && event.ascii <= 0x7E) {
        // Printable ASCII character
        if ((int)lines[currentLine].length() >= MAX_LINE_CHARS) {
            // Current line is full — auto-wrap to the next
            newLine();
        }
        lines[currentLine] += (char)event.ascii;
    }
    // Non-printable keycodes (function keys, arrows, etc.) are silently ignored.

    displayDirty = true;
    xSemaphoreGive(bufMutex);
}

// ── Helper: render all lines to the display ───────────────────────────────
void refreshDisplay() {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tr);  // 6px wide, 10px tall — fits 21 chars

    // Draw a header bar
    display.setDrawColor(1);
    display.drawBox(0, 0, 128, 12);
    display.setDrawColor(0);
    display.drawStr(2, 10, "USB Keyboard Terminal");
    display.setDrawColor(1);

    // Draw text lines, starting from the oldest visible line.
    // The ring buffer holds MAX_LINES entries; we display them top to bottom.
    int startLine = (currentLine + 1) % MAX_LINES;  // Oldest line
    for (int i = 0; i < MAX_LINES; i++) {
        int idx = (startLine + i) % MAX_LINES;
        int y   = 24 + i * 10;           // 12px header + 2px gap + 10px per row

        if (i == (MAX_LINES - 1)) {
            // Highlight the active (current) line with a cursor character
            String cursorLine = lines[currentLine] + "_";
            display.drawStr(0, y, cursorLine.c_str());
        } else {
            display.drawStr(0, y, lines[idx].c_str());
        }
    }

    display.sendBuffer();
}

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
    // Initialise I²C display
    display.begin();
    display.setFont(u8g2_font_6x10_tr);
    display.clearBuffer();
    display.drawStr(10, 30, "Plug in keyboard...");
    display.sendBuffer();

    // Initialise shared state
    for (int i = 0; i < MAX_LINES; i++) lines[i] = "";
    bufMutex = xSemaphoreCreateMutex();

    // Register the keyboard callback and start the USB host stack
    usb.onKeyboard(onKeyboardEvent);
    if (!usb.begin()) {
        // If begin() fails, show an error and halt
        display.clearBuffer();
        display.drawStr(0, 20, "USB Host failed!");
        display.drawStr(0, 34, "Check CDC setting");
        display.sendBuffer();
        while (true) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }
}

// ── Loop ───────────────────────────────────────────────────────────────────
// The USB host task runs in the background. This loop only updates the
// display when the keyboard callback has flagged new data.
void loop() {
    if (displayDirty) {
        xSemaphoreTake(bufMutex, portMAX_DELAY);
        displayDirty = false;
        refreshDisplay();
        xSemaphoreGive(bufMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(30));  // ~33 fps ceiling; yields to other tasks
}