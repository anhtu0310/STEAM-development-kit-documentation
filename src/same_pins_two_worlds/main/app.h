#pragma once

// Shared state of the demo: the walkthrough stage, what the screens show, and
// one command queue that every input (BOOT, GPIO4..7, USB keyboard, serial
// console) feeds. The control task in main.cpp is the only consumer.

#include <cstddef>
#include <cstdint>

// The walkthrough. Firmware cannot read S6, so every switch change is a stage
// that waits for the presenter to confirm it.
enum class Stage : uint8_t {
    WORLD_A,     // VGA owns the 14 shared pins
    TO_B,        // shared pins parked (inputs); presenter sets S6 for World B
    WORLD_B,     // 7-segment, MAX7219, WS2812B, buzzer, LED and buttons own them
    TO_A,        // outputs off, pins parked; presenter sets S6 for VGA
    FAULT_ON,    // VGA running; presenter turns S6-2 on by mistake
    FAULT_OFF,   // presenter turns S6-2 off again
};

enum class Command : uint8_t {
    NEXT,         // confirm / advance the walkthrough
    BACK,         // cancel a pending switch step
    FAULT,        // start the "S6-2 left on" fault demo (World A only)
    NOISE,        // moving noise band on/off
    CLEAR,        // clear the VGA refill counters
    PIN_NEXT,     // next spotlight pin
    TUNE,         // play a tune (World B)
    BRIGHTNESS,   // next 7-segment brightness (World B)
};

struct View {
    Stage stage;
    int spotlight;          // index into pins::SHARED_ROLES
    bool noise;             // moving noise band in the VGA picture
    uint8_t brightness;     // 7-segment brightness, percent
    uint32_t switches;      // completed world switches
    char last_input[20];    // last input seen, for the screens
};

bool app_init();
View view_get();
void view_update(void (*edit)(View &view, void *context), void *context);

bool command_send(Command command, const char *source);
bool command_receive(Command *command, char *source, size_t capacity, uint32_t wait_ms);

const char *stage_name(Stage stage);

// Which pins the input task may read right now. A pin that is an output (or
// owned by the LCD) must never be sampled as a button.
enum InputMask : uint8_t {
    INPUT_BOOT = 1,          // GPIO0 is an input (World A, switch steps)
    INPUT_GPIO4 = 2,         // GPIO4 is an input (switch steps, World B)
    INPUT_GPIO5_7 = 4,       // GPIO5..7 are inputs (World B)
};
void input_mask_set(uint8_t mask);
uint8_t input_mask_get();
