#pragma once

// What each VGA pin does in each world: the content of Table 2 of the JST
// draft, used by the VGA table, the OLED, the 7-segment display and the
// scrolling text on the LED matrix.

namespace pins {

struct Role {
    int gpio;
    const char *vga;        // World A signal
    const char *world_b;    // World B function ("-" = VGA only)
    const char *path;       // what connects the World B function
    bool shared;
};

inline constexpr Role ROLES[18] = {
    {4, "R0", "Button GPIO4 (next)", "no buffer", true},
    {5, "R1", "Button GPIO5 (tune)", "no buffer", true},
    {6, "R2", "Button GPIO6 (pin)", "no buffer", true},
    {7, "R3", "Button GPIO7 (light)", "no buffer", true},
    {8, "R4", "LED (heartbeat)", "no buffer", true},
    {38, "G0", "-", "VGA only", false},
    {39, "G1", "-", "VGA only", false},
    {40, "G2", "-", "VGA only", false},
    {41, "G3", "7-seg OE (PWM)", "S6-3", true},
    {42, "G4", "7-seg clock", "no buffer", true},
    {3, "G5", "Buzzer", "S6-4", true},
    {15, "B0", "7-seg reset", "no buffer", true},
    {16, "B1", "7-seg data", "no buffer", true},
    {17, "B2", "MAX7219 CS", "HCT244 S6-2", true},
    {18, "B3", "MAX7219 CLK", "HCT244 S6-2", true},
    {21, "B4", "MAX7219 DIN", "HCT244 S6-2", true},
    {1, "HSYNC", "-", "direct", false},
    {2, "VSYNC", "WS2812B DIN", "direct+HCT244", true},
};

// Indices into ROLES of the 14 shared pins, in the spotlight order.
inline constexpr int SPOTLIGHT[14] = {13, 14, 15, 17, 10, 8, 9, 11, 12, 4, 0, 1, 2, 3};
inline constexpr int SPOTLIGHT_COUNT = 14;

inline const Role &spotlight(int index) {
    return ROLES[SPOTLIGHT[((index % SPOTLIGHT_COUNT) + SPOTLIGHT_COUNT) % SPOTLIGHT_COUNT]];
}

}  // namespace pins
