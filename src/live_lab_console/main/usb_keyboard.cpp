#include "usb_keyboard.h"

#include <cstdio>
#include <cstring>

#include "app_state.h"

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/usb_host.h"

namespace {

constexpr char TAG[] = "llc_usb";

struct DeviceEvent {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t event;
};

QueueHandle_t device_events = nullptr;

void set_state(const char *state) {
    app_state_update([](AppState &s, void *raw) {
        copy_text(s.usb_state, sizeof(s.usb_state), static_cast<const char *>(raw));
    }, const_cast<char *>(state));
}

char key_name(uint8_t key) {
    if (key >= HID_KEY_A && key <= HID_KEY_Z) return static_cast<char>('A' + key - HID_KEY_A);
    if (key >= HID_KEY_1 && key <= HID_KEY_9) return static_cast<char>('1' + key - HID_KEY_1);
    if (key == HID_KEY_0) return '0';
    return 0;
}

void on_key(uint8_t key) {
    char label[16];
    const char name = key_name(key);
    if (key == HID_KEY_SPACE) std::snprintf(label, sizeof(label), "SPACE");
    else if (key == HID_KEY_ENTER) std::snprintf(label, sizeof(label), "ENTER");
    else if (name != 0) std::snprintf(label, sizeof(label), "%c", name);
    else std::snprintf(label, sizeof(label), "0x%02X", key);
    app_state_update([](AppState &s, void *raw) {
        copy_text(s.usb_last, sizeof(s.usb_last), static_cast<const char *>(raw));
        ++s.usb_keys;
    }, label);

    switch (key) {
        case HID_KEY_R:
        case HID_KEY_SPACE: app_command(Command::RELAY_TOGGLE, "USB"); break;
        case HID_KEY_S: app_command(Command::STRESS_TOGGLE, "USB"); break;
        case HID_KEY_X: app_command(Command::ENVELOPE_TOGGLE, "USB"); break;
        case HID_KEY_C: app_command(Command::CLEAR_COUNTERS, "USB"); break;
        case HID_KEY_M: app_command(Command::MARK, "USB"); break;
        default: break;
    }
}

void handle_report(const uint8_t *data, size_t length) {
    if (length < sizeof(hid_keyboard_input_report_boot_t)) return;
    const auto *report = reinterpret_cast<const hid_keyboard_input_report_boot_t *>(data);
    static uint8_t previous[HID_KEYBOARD_KEY_MAX] = {};
    for (int index = 0; index < HID_KEYBOARD_KEY_MAX; ++index) {
        const uint8_t key = report->key[index];
        if (key <= HID_KEY_ERROR_UNDEFINED) continue;
        bool held = false;
        for (uint8_t old : previous) held |= old == key;
        if (!held) on_key(key);
    }
    std::memcpy(previous, report->key, sizeof(previous));
}

void interface_callback(hid_host_device_handle_t handle, hid_host_interface_event_t event, void *) {
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
            uint8_t data[64];
            size_t length = 0;
            if (hid_host_device_get_raw_input_report_data(handle, data, sizeof(data), &length) == ESP_OK) {
                handle_report(data, length);
            }
            break;
        }
        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            set_state("NO KEYBOARD");
            app_event("USB keyboard unplugged");
            hid_host_device_close(handle);
            break;
        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            set_state("TRANSFER ERR");
            break;
        default:
            break;
    }
}

void device_callback(hid_host_device_handle_t handle, hid_host_driver_event_t event, void *) {
    const DeviceEvent message = {handle, event};
    xQueueSend(device_events, &message, 0);
}

void usb_task(void *) {
    usb_host_config_t host = {};
    host.skip_phy_setup = false;
    host.intr_flags = ESP_INTR_FLAG_LOWMED;   // allocated here, on core 0
    if (usb_host_install(&host) != ESP_OK) {
        set_state("HOST ERROR");
        task_exit();
    }
    hid_host_driver_config_t hid = {};
    hid.create_background_task = true;
    hid.task_priority = 4;
    hid.stack_size = 4096;
    hid.core_id = 0;
    hid.callback = device_callback;
    if (hid_host_install(&hid) != ESP_OK) {
        set_state("HID ERROR");
        task_exit();
    }
    set_state("NO KEYBOARD");
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &flags);
        DeviceEvent message;
        while (xQueueReceive(device_events, &message, 0) == pdTRUE) {
            if (message.event != HID_HOST_DRIVER_EVENT_CONNECTED) continue;
            hid_host_dev_params_t params = {};
            if (hid_host_device_get_params(message.handle, &params) != ESP_OK ||
                params.proto != HID_PROTOCOL_KEYBOARD) {
                continue;   // mice and other HID interfaces are ignored
            }
            hid_host_device_config_t device = {};
            device.callback = interface_callback;
            if (hid_host_device_open(message.handle, &device) != ESP_OK) {
                set_state("OPEN ERROR");
                continue;
            }
            if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                hid_class_request_set_protocol(message.handle, HID_REPORT_PROTOCOL_BOOT);
                hid_class_request_set_idle(message.handle, 0, 0);
            }
            if (hid_host_device_start(message.handle) == ESP_OK) {
                set_state("KEYBOARD");
                app_event("USB keyboard connected");
            } else {
                set_state("START ERROR");
            }
        }
    }
}

}  // namespace

bool usb_keyboard_start() {
    device_events = xQueueCreate(8, sizeof(DeviceEvent));
    return device_events != nullptr && spawn(usb_task, "usb_host", 6144, nullptr, 3, 0);
}
