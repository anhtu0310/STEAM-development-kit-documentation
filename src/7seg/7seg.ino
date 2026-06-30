/**
 * 4-Digit 7-Segment Display via 4× 74HC595 Shift Registers
 *
 * Hardware:
 *   - 4× Common-Anode 7-segment single-digit displays
 *   - 4× 74HC595 shift registers (daisy-chained)
 *   - Segment mapping: Q0=A, Q1=B, Q2=C, Q3=D, Q4=E, Q5=F, Q6=G, Q7=DP
 *   - Common Anode: segment ON when shift register output is LOW
 *
 * Wiring:
 *   ESP32-S3 Pin 16  → DS   (SER / Data)     of first 74HC595
 *   ESP32-S3 Pin 42  → SH_CP (SRCLK / Clock) of all 74HC595s (shared)
 *   ESP32-S3 Pin 0 → ST_CP (RCLK / Latch)  of all 74HC595s (shared)
 *
 *   Daisy-chain: Q7S of 74HC595 #1 → DS of #2 → DS of #3 → DS of #4
 *   Digit order:  595 #1 = digit 1 (leftmost), #4 = digit 4 (rightmost)
 *
 * Note: Each digit's common anode must be connected to VCC (5V).
 *       Add current-limiting resistors (~220Ω) on each segment line.
 */

// ── Pin definitions ──────────────────────────────────────────────────────────
const int DATA_PIN  = 16;   // DS   (pin 14 of 74HC595)
const int CLOCK_PIN = 42;   // SH_CP (pin 11 of 74HC595)
const int LATCH_PIN = 0;  // ST_CP (pin 12 of 74HC595)

// ── Segment encoding (Common Anode → active LOW, so bits are inverted) ───────
//
//  Bit position:  7   6   5   4   3   2   1   0
//  Segment:       DP  G   F   E   D   C   B   A
//
//  For common anode, 0 = segment ON, 1 = segment OFF.
//
//       AAA
//      F   B
//      F   B
//       GGG
//      E   C
//      E   C
//       DDD  DP

// Lookup table for digits 0–9 (index 10 = blank, index 11 = dash '-')
// Stored as active-HIGH patterns; we invert when sending to the shift register.
//                          PGFEDCBA
const uint8_t DIGITS[] = {
  0b00111111,  // 0  — A B C D E F
  0b00000110,  // 1  — B C
  0b01011011,  // 2  — A B D E G
  0b01001111,  // 3  — A B C D G
  0b01100110,  // 4  — B C F G
  0b01101101,  // 5  — A C D F G
  0b01111101,  // 6  — A C D E F G
  0b00000111,  // 7  — A B C
  0b01111111,  // 8  — A B C D E F G
  0b01101111,  // 9  — A B C D F G
  0b00000000,  // 10 — blank (all off)
  0b01000000,  // 11 — dash  (G only)
};

const int BLANK = 10;
const int DASH  = 11;

// ── Core shift-register function ─────────────────────────────────────────────

/**
 * Send one byte to the shift register chain.
 * MSB first. Clock is generated manually (no SPI) for simplicity.
 */
void shiftOutByte(uint8_t value) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(CLOCK_PIN, LOW);
    digitalWrite(DATA_PIN, (value >> i) & 0x01);
    digitalWrite(CLOCK_PIN, HIGH);
  }
}

/**
 * Display a 4-digit number on the displays.
 *
 * @param d1  Leftmost digit  (digit index: 0–9, BLANK, or DASH)
 * @param d2  Second digit
 * @param d3  Third digit
 * @param d4  Rightmost digit
 * @param dp  Bitmask for decimal points: bit3=d1, bit2=d2, bit1=d3, bit0=d4
 */
void displayDigits(int d1, int d2, int d3, int d4, uint8_t dp = 0) {
  // For common-anode, invert the segment pattern (0 = ON).
  // The 74HC595s are daisy-chained, so the LAST byte clocked in
  // ends up in the FIRST 595 (leftmost digit). Send right-to-left.
  uint8_t patterns[4];
  patterns[0] = ~DIGITS[d4];  // leftmost
  patterns[1] = ~DIGITS[d3];
  patterns[2] = ~DIGITS[d2];
  patterns[3] = ~DIGITS[d1];  // rightmost

  // Apply decimal points (bit = 0 → DP ON for common anode)
  for (int i = 0; i < 4; i++) {
    bool dpOn = (dp >> (3 - i)) & 0x01;
    if (dpOn) {
      patterns[i] &= ~(1 << 7);  // clear bit 7 (DP) to turn ON
    } else {
      patterns[i] |=  (1 << 7);  // set   bit 7 (DP) to turn OFF
    }
  }

  // Latch LOW → shift data → Latch HIGH to update outputs
  digitalWrite(LATCH_PIN, LOW);

  // Clock in from rightmost digit to leftmost (daisy-chain order)
  for (int i = 3; i >= 0; i--) {
    shiftOutByte(patterns[i]);
  }

  digitalWrite(LATCH_PIN, HIGH);
}

/**
 * Display a signed integer (-999 to 9999).
 * Numbers out of range show "----".
 *
 * @param number  Value to display
 * @param dp      Decimal point bitmask (bit3=d1 … bit0=d4)
 */
void displayNumber(int number, uint8_t dp = 0) {
  if (number > 9999 || number < -999) {
    // Out of range → show dashes
    displayDigits(DASH, DASH, DASH, DASH);
    return;
  }

  bool negative = (number < 0);
  if (negative) number = -number;

  int d4 =  number % 10;
  int d3 = (number / 10)   % 10;
  int d2 = (number / 100)  % 10;
  int d1 = (number / 1000) % 10;

  // Suppress leading zeros
  if (number < 1000) {
    d1 = negative ? DASH : BLANK;  // leading sign or blank
    if (number < 100)  d2 = BLANK;
    if (number < 10)   d3 = BLANK;
  }

  displayDigits(d1, d2, d3, d4, dp);
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setup() {
  pinMode(DATA_PIN,  OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  // Show "0" at startup
  displayNumber(0);
}

void loop() {
  // ── Example 1: Count 0 → 9999, one step per 50 ms ──
  for (int i = 0; i <= 9999; i++) {
    displayNumber(i);
    delay(10);
  }

  // ── Example 2: Show a fixed value with a decimal point ──
  // Displays "12.34" (decimal point between digit 2 and 3 → bit1 set)
  displayNumber(1234, 0b0010);
  delay(2000);

  // ── Example 3: Show a negative number ──
  displayNumber(-42);
  delay(2000);

  // ── Example 4: Show individual digit patterns directly ──
  displayDigits(1, 2, 3, 4);   // shows  1 2 3 4
  delay(2000);
}