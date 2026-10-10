#include "io.h"

#include "app_state.h"
#include "board.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr uint32_t MAX_ON_MS = CONFIG_LLC_RELAY_MAX_ON_SECONDS * 1000U;
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t LONG_PRESS_MS = 1500;

portMUX_TYPE relay_lock = portMUX_INITIALIZER_UNLOCKED;
bool relay_on = false;
TickType_t relay_off_at = 0;

void publish(bool on, uint32_t remaining_ms, const char *source) {
    struct Context { bool on; uint32_t remaining; const char *source; } context = {
        on, remaining_ms, source};
    app_state_update([](AppState &s, void *raw) {
        auto *c = static_cast<Context *>(raw);
        s.relay_on = c->on;
        s.relay_remaining_ms = c->remaining;
        if (c->source != nullptr) copy_text(s.relay_source, sizeof(s.relay_source), c->source);
    }, &context);
}

void io_task(void *) {
    int last_raw = 1;
    int stable = 1;
    TickType_t changed_at = 0;
    TickType_t pressed_at = 0;
    bool armed = gpio_get_level(board::BUTTON) == 1;   // ignore a press held from reset
    bool long_sent = false;
    while (true) {
        const TickType_t now = xTaskGetTickCount();
        const int level = gpio_get_level(board::BUTTON);
        if (level != last_raw) {
            last_raw = level;
            changed_at = now;
        }
        if (level != stable && now - changed_at >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
            stable = level;
            if (stable == 1) {
                if (armed && pressed_at != 0 && !long_sent) {
                    app_state_update([](AppState &s, void *) { ++s.button_presses; }, nullptr);
                    app_command(Command::RELAY_TOGGLE, "GPIO0");
                }
                armed = true;
                pressed_at = 0;
            } else {
                pressed_at = now;
                long_sent = false;
            }
        }
        if (armed && stable == 0 && pressed_at != 0 && !long_sent &&
            now - pressed_at >= pdMS_TO_TICKS(LONG_PRESS_MS)) {
            long_sent = true;
            app_state_update([](AppState &s, void *) { ++s.button_presses; }, nullptr);
            app_command(Command::STRESS_TOGGLE, "GPIO0 hold");
        }

        bool expired = false;
        uint32_t remaining = 0;
        portENTER_CRITICAL(&relay_lock);
        if (relay_on) {
            const int32_t left = static_cast<int32_t>(relay_off_at - now);
            if (left <= 0) {
                relay_on = false;
                gpio_set_level(board::RELAY, 0);
                expired = true;
            } else {
                remaining = static_cast<uint32_t>(left) * portTICK_PERIOD_MS;
            }
        }
        const bool on = relay_on;
        portEXIT_CRITICAL(&relay_lock);
        if (expired) {
            publish(false, 0, "AUTO-OFF");
            app_event("Relay automatic off after %u s", CONFIG_LLC_RELAY_MAX_ON_SECONDS);
        } else if (on) {
            publish(true, remaining, nullptr);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

bool io_start() {
    // GPIO45 is a strapping pin: latch LOW before switching it to an output.
    gpio_set_level(board::RELAY, 0);
    gpio_config_t relay = {};
    relay.pin_bit_mask = 1ULL << board::RELAY;
    relay.mode = GPIO_MODE_OUTPUT;
    if (gpio_config(&relay) != ESP_OK) return false;
    gpio_set_level(board::RELAY, 0);

    gpio_config_t button = {};
    button.pin_bit_mask = 1ULL << board::BUTTON;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    if (gpio_config(&button) != ESP_OK) return false;
    return spawn(io_task, "io", 3072, nullptr, 5, 0);
}

void io_relay_set(bool on, const char *source) {
    portENTER_CRITICAL(&relay_lock);
    relay_on = on;
    relay_off_at = xTaskGetTickCount() + pdMS_TO_TICKS(MAX_ON_MS);
    gpio_set_level(board::RELAY, on ? 1 : 0);
    portEXIT_CRITICAL(&relay_lock);
    publish(on, on ? MAX_ON_MS : 0, source);
    app_event("Relay %s (%s)", on ? "ON" : "OFF", source);
}

void io_relay_toggle(const char *source) {
    portENTER_CRITICAL(&relay_lock);
    const bool on = relay_on;
    portEXIT_CRITICAL(&relay_lock);
    io_relay_set(!on, source);
}
