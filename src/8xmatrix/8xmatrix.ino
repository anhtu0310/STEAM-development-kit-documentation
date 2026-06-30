#include <LedControl.h>

// Define the MAX7219 Matrix Pins (Update based on your board's schematic)
#define DIN_PIN 21
#define CS_PIN  17
#define CLK_PIN 18
#define BUT_PIN 5

// Initialize LedControl: LedControl(DIN, CLK, CS, Number of Displays)
LedControl lc = LedControl(DIN_PIN, CLK_PIN, CS_PIN, 1);

// Track button states
bool last_but_sts = HIGH;
int display_state = 0; // 0 = Happy, 1 = Neutral, 2 = Sad

// Define 8x8 Custom Graphics using Byte Arrays (1 = LED ON, 0 = LED OFF)
byte happyFace[8] = {
    B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10100101,
    B10011001,
    B01000010,
    B00111100
};

byte neutralFace[8] = {
    B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10111101,
    B10000001,
    B01000010,
    B00111100
};

byte sadFace[8] = {
    B00111100,
    B01000010,
    B10100101,
    B10000001,
    B10011001,
    B10100101,
    B01000010,
    B00111100
};

void setup() {
    // Wake up the MAX7219 from power-saving/shutdown mode
    lc.shutdown(0, false);

    // Set brightness intensity (0 is dimmest, 15 is brightest)
    lc.setIntensity(0, 4);

    // Clear the display screen
    lc.clearDisplay(0);

    // Configure the button pin
    pinMode(BUT_PIN, INPUT_PULLUP);

    // Show the initial happy face
    drawFace(happyFace);
}

void loop() {
    // Read the current state of the active-LOW button
    bool current_but_sts = digitalRead(BUT_PIN);

    // Detect button press (falling edge: HIGH to LOW transition)
    if (last_but_sts == HIGH && current_but_sts == LOW) {

        // Move to the next face state (0 -> 1 -> 2 -> 0)
        display_state = (display_state + 1) % 3;

        // Update display based on the new state
        if (display_state == 0) {
            drawFace(happyFace);
        } else if (display_state == 1) {
            drawFace(neutralFace);
        } else if (display_state == 2) {
            drawFace(sadFace);
        }

        delay(50); // Small debounce delay
    }

    // Save the state for the next loop iteration
    last_but_sts = current_but_sts;
}

// Helper function to render a byte array to the matrix
void drawFace(byte face[]) {
    for (int row = 0; row < 8; row++) {
        lc.setRow(0, row, face[row]);
    }
}
