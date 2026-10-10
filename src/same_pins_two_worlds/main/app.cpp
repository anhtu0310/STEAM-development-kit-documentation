#include "app.h"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace {

struct QueuedCommand {
    Command command;
    char source[12];
};

portMUX_TYPE view_lock = portMUX_INITIALIZER_UNLOCKED;
View view = {Stage::WORLD_A, 0, true, 100, 0, "-"};
QueueHandle_t commands = nullptr;
volatile uint8_t input_mask = 0;

void copy(char *target, size_t capacity, const char *source) {
    if (capacity == 0) return;
    std::strncpy(target, source != nullptr ? source : "", capacity - 1);
    target[capacity - 1] = '\0';
}

}  // namespace

bool app_init() {
    commands = xQueueCreate(16, sizeof(QueuedCommand));
    return commands != nullptr;
}

View view_get() {
    portENTER_CRITICAL(&view_lock);
    const View copy_of_view = view;
    portEXIT_CRITICAL(&view_lock);
    return copy_of_view;
}

void view_update(void (*edit)(View &, void *), void *context) {
    View working = view_get();
    edit(working, context);
    portENTER_CRITICAL(&view_lock);
    view = working;
    portEXIT_CRITICAL(&view_lock);
}

bool command_send(Command command, const char *source) {
    if (commands == nullptr) return false;
    QueuedCommand item = {};
    item.command = command;
    copy(item.source, sizeof(item.source), source);
    return xQueueSend(commands, &item, pdMS_TO_TICKS(20)) == pdTRUE;
}

bool command_receive(Command *command, char *source, size_t capacity, uint32_t wait_ms) {
    QueuedCommand item = {};
    if (xQueueReceive(commands, &item, pdMS_TO_TICKS(wait_ms)) != pdTRUE) return false;
    *command = item.command;
    copy(source, capacity, item.source);
    return true;
}

const char *stage_name(Stage stage) {
    switch (stage) {
        case Stage::WORLD_A: return "WORLD A (VGA)";
        case Stage::TO_B: return "SWITCHING TO WORLD B";
        case Stage::WORLD_B: return "WORLD B (PERIPHERALS)";
        case Stage::TO_A: return "SWITCHING TO WORLD A";
        case Stage::FAULT_ON: return "FAULT: S6-2 ON IN VGA";
        case Stage::FAULT_OFF: return "FAULT: S6-2 OFF AGAIN";
    }
    return "?";
}

void input_mask_set(uint8_t mask) { input_mask = mask; }
uint8_t input_mask_get() { return input_mask; }
