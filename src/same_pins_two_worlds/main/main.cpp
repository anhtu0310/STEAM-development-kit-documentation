/*
 * STEAM "Same Pins, Two Worlds": the live mode-switch demo (showcase 2).
 *
 * The 14 VGA pins that have a second job (Table 2 of the JST draft) are
 * handed between two worlds while the board keeps running:
 *
 *   World A  VGA 800x600: the RGB LCD peripheral drives all 18 VGA pins.
 *   World B  7-segment (74HC595), MAX7219 matrix, WS2812B, buzzer, LED on
 *            GPIO8 and buttons on GPIO4..7 drive the same pins.
 *
 * The RGB panel is never stopped. Switching worlds only re-routes the pins in
 * the GPIO matrix, so World A comes back instantly. Firmware cannot read S6,
 * so every switch change is a step on the OLED that the presenter confirms.
 * The fault demo runs VGA while S6-2 is (deliberately) on: the MAX7219 and
 * WS2812B then receive the VGA blue bits and VSYNC.
 *
 * Core split as in the Live Lab Console (VGA_STABILITY.md):
 *   core 1  VGA refill + VSYNC interrupts, scene renderer (priority 2)
 *   core 0  this control task, World B drivers, buttons, serial, USB host, OLED
 */

#include <cstdio>
#include <cstring>

#include "app.h"
#include "board.h"
#include "input.h"
#include "oled.h"
#include "peripherals.h"
#include "pins.h"
#include "scene.h"
#include "usb_keyboard.h"
#include "vga4.h"

#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char TAG[] = "sptw_main";

Stage stage = Stage::WORLD_A;

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

void set_stage(Stage next, bool switched) {
    struct Context { Stage stage; bool switched; } context = {next, switched};
    view_update([](View &view, void *raw) {
        auto *c = static_cast<Context *>(raw);
        view.stage = c->stage;
        if (c->switched) ++view.switches;
    }, &context);
    stage = next;
    ESP_LOGI(TAG, "Stage: %s", stage_name(next));
}

// ------------------------------------------------------------ pin ownership
void to_world_a_pins() {
    input_mask_set(0);
    vga::attach_pins(board::SHARED, board::SHARED_COUNT);
    gpio_reset_pin(board::BUTTON);   // GPIO0 back to an input for BOOT
    input_mask_set(INPUT_BOOT);
}

void park_pins() {
    input_mask_set(0);
    periph::release();   // outputs off (if World B was active), then park
    input_mask_set(INPUT_BOOT | INPUT_GPIO4);
}

void to_world_b_pins() {
    input_mask_set(0);
    periph::claim();
    input_mask_set(INPUT_GPIO4 | INPUT_GPIO5_7);
}

// ------------------------------------------------------------ the walkthrough
void handle(Command command, const char *source) {
    view_update([](View &view, void *raw) {
        std::snprintf(view.last_input, sizeof(view.last_input), "%s", static_cast<const char *>(raw));
    }, const_cast<char *>(source));

    switch (command) {
        case Command::NOISE:
            view_update([](View &view, void *) { view.noise = !view.noise; }, nullptr);
            return;
        case Command::CLEAR:
            vga::clear_stats();
            return;
        case Command::PIN_NEXT:
            view_update([](View &view, void *) { view.spotlight = (view.spotlight + 1) % pins::SPOTLIGHT_COUNT; }, nullptr);
            return;
        case Command::TUNE:
            if (stage == Stage::WORLD_B) periph::tune_start();
            return;
        case Command::BRIGHTNESS:
            if (stage == Stage::WORLD_B) {
                const uint8_t percent = periph::brightness_next();
                view_update([](View &view, void *raw) { view.brightness = *static_cast<uint8_t *>(raw); },
                            const_cast<uint8_t *>(&percent));
            }
            return;
        default:
            break;
    }

    switch (stage) {
        case Stage::WORLD_A:
            if (command == Command::NEXT) {
                park_pins();
                set_stage(Stage::TO_B, false);
            } else if (command == Command::FAULT) {
                set_stage(Stage::FAULT_ON, false);
            }
            break;
        case Stage::TO_B:
            if (command == Command::NEXT) {
                to_world_b_pins();
                set_stage(Stage::WORLD_B, true);
            } else if (command == Command::BACK) {
                to_world_a_pins();
                set_stage(Stage::WORLD_A, false);
            }
            break;
        case Stage::WORLD_B:
            if (command == Command::NEXT) {
                park_pins();
                set_stage(Stage::TO_A, false);
            }
            break;
        case Stage::TO_A:
            if (command == Command::NEXT) {
                to_world_a_pins();
                set_stage(Stage::WORLD_A, true);
            } else if (command == Command::BACK) {
                to_world_b_pins();
                set_stage(Stage::WORLD_B, false);
            }
            break;
        case Stage::FAULT_ON:
            if (command == Command::NEXT || command == Command::FAULT) set_stage(Stage::FAULT_OFF, false);
            else if (command == Command::BACK) set_stage(Stage::WORLD_A, false);
            break;
        case Stage::FAULT_OFF:
            if (command == Command::NEXT || command == Command::BACK) set_stage(Stage::WORLD_A, false);
            break;
    }
}

// ------------------------------------------------------------ OLED
void update_oled(const View &view, const vga::Stats &stats, uint8_t inputs) {
    char a[22], b[22], c[22], d[22];
    const pins::Role &spot = pins::spotlight(view.spotlight);
    const char *lines[8] = {};
    switch (view.stage) {
        case Stage::WORLD_A:
            std::snprintf(a, sizeof(a), "LATE %lu IRR %lu", static_cast<unsigned long>(stats.late),
                          static_cast<unsigned long>(stats.irregular_frames));
            lines[0] = "WORLD A: VGA 800X600";
            lines[1] = "S6-1 OFF VGA BUFFERS";
            lines[2] = "S6-2 OFF MATRIX+RGB";
            lines[3] = "S6-3 OFF  S6-4 OFF";
            lines[4] = "NO SOUND IN VGA MODE";
            lines[5] = "BOOT/ENTER: WORLD B";
            lines[6] = "HOLD BOOT/F: FAULT";
            lines[7] = a;
            break;
        case Stage::TO_B:
            lines[0] = "GO TO WORLD B";
            lines[1] = "PINS RELEASED. SET:";
            lines[2] = "S6-1 ON  VGA HI-Z";
            lines[3] = "S6-2 ON  MATRIX+RGB";
            lines[4] = "S6-3 ON  7SEG OE";
            lines[5] = "S6-4 ON  BUZZER";
            lines[6] = "THEN GPIO4/BOOT/ENTR";
            lines[7] = "ESC/B: STAY IN VGA";
            break;
        case Stage::WORLD_B:
            std::snprintf(a, sizeof(a), "GPIO%d = %s", spot.gpio, spot.world_b);
            std::snprintf(b, sizeof(b), "IN VGA IT IS %s", spot.vga);
            std::snprintf(c, sizeof(c), "IN 4-7: %c%c%c%c  LT %u%%",
                          (inputs & 1) ? '4' : '-', (inputs & 2) ? '5' : '-',
                          (inputs & 4) ? '6' : '-', (inputs & 8) ? '7' : '-', view.brightness);
            lines[0] = "WORLD B: PERIPHERALS";
            lines[1] = a;
            lines[2] = b;
            lines[3] = c;
            lines[4] = "GPIO5 TUNE  6 PIN";
            lines[5] = "GPIO7 7SEG LIGHT";
            lines[6] = "GPIO4/ENTER: TO VGA";
            lines[7] = "VGA HI-Z, NO VSYNC";
            break;
        case Stage::TO_A:
            lines[0] = "BACK TO WORLD A";
            lines[1] = "OUTPUTS OFF. SET:";
            lines[2] = "S6-1 OFF VGA BUFFERS";
            lines[3] = "S6-2 OFF MATRIX+RGB";
            lines[4] = "S6-3 OFF  S6-4 OFF";
            lines[6] = "THEN GPIO4/BOOT/ENTR";
            lines[7] = "ESC/B: STAY IN B";
            break;
        case Stage::FAULT_ON:
            std::snprintf(d, sizeof(d), "LATE %lu IRR %lu", static_cast<unsigned long>(stats.late),
                          static_cast<unsigned long>(stats.irregular_frames));
            lines[0] = "FAULT DEMO, VGA ON";
            lines[1] = "NOW FLIP S6-2 ON";
            lines[2] = "MATRIX GETS VGA BLUE";
            lines[3] = "RGB LEDS GET VSYNC";
            lines[4] = "SCREEN: WHAT IT GETS";
            lines[5] = "N: NOISE ON/OFF";
            lines[6] = "BOOT/ENTER: NEXT";
            lines[7] = d;
            break;
        case Stage::FAULT_OFF:
            lines[0] = "FAULT DEMO: FIX IT";
            lines[1] = "FLIP S6-2 OFF";
            lines[2] = "THE NOISE STOPS.";
            lines[3] = "MATRIX KEEPS ITS";
            lines[4] = "LAST GARBAGE UNTIL";
            lines[5] = "WORLD B RESETS IT";
            lines[7] = "BOOT/ENTER: DONE";
            break;
    }
    oled::show(lines);
}

void report(const char *name, bool ok) {
    ESP_LOGI(TAG, "%-12s %s", name, ok ? "running" : "NOT available");
}

}  // namespace

extern "C" void app_main() {
    // GPIO45 is a strapping pin and drives the relay: hold it LOW.
    gpio_set_level(board::RELAY, 0);
    gpio_config_t relay = {};
    relay.pin_bit_mask = 1ULL << board::RELAY;
    relay.mode = GPIO_MODE_OUTPUT;
    gpio_config(&relay);
    gpio_set_level(board::RELAY, 0);

    uint32_t flash_bytes = 0;
    esp_flash_get_size(nullptr, &flash_bytes);
    ESP_LOGI(TAG, "Same Pins, Two Worlds. Flash %lu MB, PSRAM %u MB (as the chip reports)",
             static_cast<unsigned long>(flash_bytes >> 20),
             static_cast<unsigned>(esp_psram_get_size() >> 20));

    if (!app_init()) {
        ESP_LOGE(TAG, "Out of memory");
        return;
    }
    report("OLED", oled::start());
    report("LEDC timers", periph::init_once());
    gpio_reset_pin(board::BUTTON);
    const bool vga_ok = vga::start();
    report("VGA", vga_ok);
    if (vga_ok) report("Scene", scene::start());
    input_mask_set(INPUT_BOOT);
    report("Buttons", input_start());
    report("USB host", usb_keyboard_start());

    uint32_t next_oled = 0;
    while (true) {
        Command command;
        char source[12];
        if (command_receive(&command, source, sizeof(source), 20)) handle(command, source);
        const View view = view_get();
        const uint32_t now = now_ms();
        if (stage == Stage::WORLD_B) periph::tick(now, view);
        if (static_cast<int32_t>(now - next_oled) >= 0) {
            const uint8_t inputs = stage == Stage::WORLD_B ? periph::read_inputs() : 0;
            update_oled(view, vga::stats(), inputs);
            next_oled = now + 200;
        }
    }
}
