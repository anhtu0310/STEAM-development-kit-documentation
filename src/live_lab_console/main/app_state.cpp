#include "app_state.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

namespace {

constexpr char TAG[] = "llc_event";

struct QueuedCommand {
    Command command;
    char source[12];
};

SemaphoreHandle_t lock = nullptr;
QueueHandle_t commands = nullptr;
AppState state = {};

}  // namespace

void copy_text(char *target, size_t capacity, const char *source) {
    if (target == nullptr || capacity == 0) return;
    if (source == nullptr) source = "";
    std::strncpy(target, source, capacity - 1);
    target[capacity - 1] = '\0';
}

bool app_state_init() {
    lock = xSemaphoreCreateMutex();
    commands = xQueueCreate(16, sizeof(QueuedCommand));
    if (lock == nullptr || commands == nullptr) return false;
    copy_text(state.ble_state, sizeof(state.ble_state), "STARTING");
    copy_text(state.usb_state, sizeof(state.usb_state), "STARTING");
    copy_text(state.relay_source, sizeof(state.relay_source), "BOOT");
    copy_text(state.eth_ip, sizeof(state.eth_ip), "-");
    copy_text(state.wifi_ip, sizeof(state.wifi_ip), "-");
    copy_text(state.ble_last, sizeof(state.ble_last), "-");
    copy_text(state.usb_last, sizeof(state.usb_last), "-");
    copy_text(state.sd_card, sizeof(state.sd_card), "-");
    state.ble_battery = -1;
    return true;
}

AppState app_state_snapshot() {
    AppState copy;
    xSemaphoreTake(lock, portMAX_DELAY);
    copy = state;
    xSemaphoreGive(lock);
    return copy;
}

void app_state_update(void (*edit)(AppState &, void *), void *context) {
    xSemaphoreTake(lock, portMAX_DELAY);
    edit(state, context);
    ++state.generation;
    xSemaphoreGive(lock);
}

void app_event(const char *format, ...) {
    char text[EVENT_TEXT - 10] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ESP_LOGI(TAG, "%s", text);

    const uint32_t seconds = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    char line[EVENT_TEXT] = {};
    std::snprintf(line, sizeof(line), "%02lu:%02lu:%02lu %s",
                  static_cast<unsigned long>(seconds / 3600),
                  static_cast<unsigned long>((seconds / 60) % 60),
                  static_cast<unsigned long>(seconds % 60), text);
    xSemaphoreTake(lock, portMAX_DELAY);
    std::memmove(state.events[1], state.events[0],
                 sizeof(state.events[0]) * (EVENT_COUNT - 1));
    copy_text(state.events[0], sizeof(state.events[0]), line);
    ++state.generation;
    xSemaphoreGive(lock);
}

bool spawn(void (*function)(void *), const char *name, uint32_t stack_bytes,
           void *argument, unsigned priority, int core, void **handle) {
    TaskHandle_t created = nullptr;
    const BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        function, name, stack_bytes, argument, priority, &created, core,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Cannot start task %s", name);
        return false;
    }
    if (handle != nullptr) *handle = created;
    return true;
}

void task_exit() {
    vTaskDeleteWithCaps(nullptr);
    while (true) vTaskDelay(portMAX_DELAY);
}

bool app_command(Command command, const char *source) {
    if (commands == nullptr) return false;
    QueuedCommand item = {};
    item.command = command;
    copy_text(item.source, sizeof(item.source), source);
    return xQueueSend(commands, &item, pdMS_TO_TICKS(20)) == pdTRUE;
}

bool app_command_receive(Command *command, char *source, size_t capacity,
                         uint32_t wait_ms) {
    QueuedCommand item = {};
    if (xQueueReceive(commands, &item, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        return false;
    }
    *command = item.command;
    copy_text(source, capacity, item.source);
    return true;
}
