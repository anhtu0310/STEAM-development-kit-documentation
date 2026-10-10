#include "input.h"

#include <cstdio>

#include "app.h"
#include "board.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char TAG[] = "sptw_input";
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t LONG_PRESS_MS = 1500;

struct Button {
    gpio_num_t pin;
    uint8_t mask;          // InputMask bit that makes the pin readable
    Command command;
    const char *name;
    bool armed;            // seen released since it became readable
    int stable;            // debounced level, 1 = released
    int last_raw;
    TickType_t changed_at;
    TickType_t pressed_at;
    bool long_sent;
};

Button buttons[] = {
    {board::BUTTON, INPUT_BOOT, Command::NEXT, "BOOT"},
    {board::INPUTS[0], INPUT_GPIO4, Command::NEXT, "GPIO4"},
    {board::INPUTS[1], INPUT_GPIO5_7, Command::TUNE, "GPIO5"},
    {board::INPUTS[2], INPUT_GPIO5_7, Command::PIN_NEXT, "GPIO6"},
    {board::INPUTS[3], INPUT_GPIO5_7, Command::BRIGHTNESS, "GPIO7"},
};

void button_task(void *) {
    uint8_t previous_mask = 0;
    while (true) {
        const uint8_t mask = input_mask_get();
        const TickType_t now = xTaskGetTickCount();
        for (Button &button : buttons) {
            if ((mask & button.mask) == 0) continue;
            if ((previous_mask & button.mask) == 0) {
                // Newly readable: a press held across the pin change must be
                // released before it counts.
                const int level = gpio_get_level(button.pin);
                button.stable = level;
                button.last_raw = level;
                button.armed = level == 1;
                button.pressed_at = 0;
                button.long_sent = false;
                continue;
            }
            const int level = gpio_get_level(button.pin);
            if (level != button.last_raw) {
                button.last_raw = level;
                button.changed_at = now;
            }
            if (level == button.stable || now - button.changed_at < pdMS_TO_TICKS(DEBOUNCE_MS)) {
                if (button.pin == board::BUTTON && button.armed && button.stable == 0 &&
                    !button.long_sent && now - button.pressed_at >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
                    button.long_sent = true;
                    command_send(Command::FAULT, "BOOT hold");
                }
                continue;
            }
            button.stable = level;
            if (level == 0) {
                button.pressed_at = now;
                button.long_sent = false;
                if (button.armed && button.pin != board::BUTTON) {
                    command_send(button.command, button.name);
                }
            } else {
                // BOOT acts on release, so a long hold can mean "fault demo".
                if (button.armed && button.pin == board::BUTTON && !button.long_sent) {
                    command_send(button.command, button.name);
                }
                button.armed = true;
            }
        }
        previous_mask = mask;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void print_help() {
    std::printf(
        "\nSame Pins, Two Worlds - serial keys\n"
        "  Enter/Space  next step (confirm a switch change)\n"
        "  b / Esc      cancel a pending switch step\n"
        "  f            fault demo (S6-2 left on during VGA)\n"
        "  n            moving noise band on/off\n"
        "  c            clear VGA refill counters\n"
        "  p            next spotlight pin\n"
        "  t / l        tune / 7-segment brightness (World B)\n\n");
}

void console_task(void *) {
    uint8_t character = 0;
    while (true) {
        if (uart_read_bytes(UART_NUM_0, &character, 1, portMAX_DELAY) != 1) continue;
        switch (character) {
            case '\r': case '\n': case ' ': command_send(Command::NEXT, "SERIAL"); break;
            case 'b': case 'B': case 27: command_send(Command::BACK, "SERIAL"); break;
            case 'f': case 'F': command_send(Command::FAULT, "SERIAL"); break;
            case 'n': case 'N': command_send(Command::NOISE, "SERIAL"); break;
            case 'c': case 'C': command_send(Command::CLEAR, "SERIAL"); break;
            case 'p': case 'P': command_send(Command::PIN_NEXT, "SERIAL"); break;
            case 't': case 'T': command_send(Command::TUNE, "SERIAL"); break;
            case 'l': case 'L': command_send(Command::BRIGHTNESS, "SERIAL"); break;
            case 'h': case 'H': case '?': print_help(); break;
            default: break;
        }
    }
}

}  // namespace

bool input_start() {
    bool ok = xTaskCreatePinnedToCore(button_task, "buttons", 3072, nullptr, 5, nullptr, 0) == pdPASS;
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0) == ESP_OK) {
        ok &= xTaskCreatePinnedToCore(console_task, "console", 3072, nullptr, 4, nullptr, 0) == pdPASS;
        print_help();
    } else {
        ESP_LOGW(TAG, "Serial console input unavailable");
    }
    return ok;
}
