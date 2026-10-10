#include "ble_remote.h"

#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "esp_hid_gap.h"

#include "esp_gattc_api.h"
#include "esp_hidh.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr char TAG[] = "llc_ble";
// Consumer-control reports seen from the AB Shutter3 (STEAM_VGA_IDF54_RAW16_OLED).
constexpr uint8_t REPORT_BIG = 0x10;
constexpr uint8_t REPORT_SMALL = 0x20;
constexpr uint16_t CONSUMER_REPORT_ID = 3;
constexpr uint32_t SCAN_SECONDS = 5;
constexpr uint8_t KNOWN_BDA[6] = {0xff, 0xff, 0x10, 0xe5, 0x1c, 0x38};
constexpr EventBits_t CONNECTED = BIT0;
constexpr EventBits_t RESCAN = BIT1;

EventGroupHandle_t events = nullptr;
uint8_t last_report = 0;

void set_state(const char *state, int battery = -2) {
    struct C { const char *state; int battery; } c = {state, battery};
    app_state_update([](AppState &s, void *raw) {
        auto *v = static_cast<C *>(raw);
        copy_text(s.ble_state, sizeof(s.ble_state), v->state);
        if (v->battery != -2) s.ble_battery = static_cast<int8_t>(v->battery);
    }, &c);
}

bool matches(const esp_hid_scan_result_t *r) {
    if (r == nullptr || r->transport != ESP_HID_TRANSPORT_BLE) return false;
    if (std::memcmp(r->bda, KNOWN_BDA, sizeof(KNOWN_BDA)) == 0) return true;
    return r->name != nullptr && std::strcmp(r->name, CONFIG_LLC_BLE_REMOTE_NAME) == 0;
}

void press(const char *name, Command command) {
    app_state_update([](AppState &s, void *raw) {
        copy_text(s.ble_last, sizeof(s.ble_last), static_cast<const char *>(raw));
        ++s.ble_presses;
    }, const_cast<char *>(name));
    app_command(command, "BLE");
}

void hid_callback(void *, esp_event_base_t, int32_t id, void *data) {
    auto *param = static_cast<esp_hidh_event_data_t *>(data);
    switch (static_cast<esp_hidh_event_t>(id)) {
        case ESP_HIDH_OPEN_EVENT:
            if (param->open.status == ESP_OK) {
                last_report = 0;
                xEventGroupClearBits(events, RESCAN);
                xEventGroupSetBits(events, CONNECTED);
                set_state("CONNECTED");
                app_event("BLE remote connected");
            } else {
                xEventGroupSetBits(events, RESCAN);
                set_state("OPEN ERROR");
            }
            break;
        case ESP_HIDH_INPUT_EVENT: {
            if (param->input.usage != ESP_HID_USAGE_CCONTROL ||
                param->input.report_id != CONSUMER_REPORT_ID ||
                param->input.length < 1 || param->input.data == nullptr) {
                break;
            }
            const uint8_t report = param->input.data[0];
            if (report == last_report) break;   // act on new presses only
            last_report = report;
            if (report == REPORT_BIG) press("BIG", Command::STRESS_TOGGLE);
            else if (report == REPORT_SMALL) press("SMALL", Command::RELAY_TOGGLE);
            break;
        }
        case ESP_HIDH_CLOSE_EVENT:
            last_report = 0;
            xEventGroupClearBits(events, CONNECTED);
            xEventGroupSetBits(events, RESCAN);
            set_state("DISCONNECTED");
            app_event("BLE remote disconnected");
            if (param->close.dev != nullptr) esp_hidh_dev_free(param->close.dev);
            break;
        case ESP_HIDH_BATTERY_EVENT: {
            char text[18];
            std::snprintf(text, sizeof(text), "CONNECTED");
            set_state(text, param->battery.level);
            break;
        }
        default:
            break;
    }
}

void scan_task(void *) {
    while (true) {
        if (xEventGroupGetBits(events) & CONNECTED) {
            xEventGroupWaitBits(events, RESCAN, pdTRUE, pdFALSE, portMAX_DELAY);
            continue;
        }
        xEventGroupClearBits(events, RESCAN);
        set_state("SCANNING");
        size_t count = 0;
        esp_hid_scan_result_t *results = nullptr;
        if (esp_hid_scan(SCAN_SECONDS, &count, &results) != ESP_OK) {
            set_state("SCAN ERROR");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        esp_hid_scan_result_t *selected = nullptr;
        for (esp_hid_scan_result_t *r = results; r != nullptr; r = r->next) {
            if (matches(r)) {
                selected = r;
                break;
            }
        }
        if (selected == nullptr) {
            esp_hid_scan_results_free(results);
            set_state("NOT FOUND");
            vTaskDelay(pdMS_TO_TICKS(5000));   // halves the scan duty cycle
            continue;
        }
        set_state("CONNECTING");
        esp_hidh_dev_t *device = esp_hidh_dev_open(selected->bda, selected->transport,
                                                   selected->ble.addr_type);
        esp_hid_scan_results_free(results);
        if (device == nullptr) {
            set_state("OPEN ERROR");
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        const EventBits_t bits = xEventGroupWaitBits(events, CONNECTED | RESCAN, pdFALSE,
                                                     pdFALSE, pdMS_TO_TICKS(15000));
        if ((bits & CONNECTED) == 0) {
            set_state("TIMEOUT");
            if (esp_hidh_dev_exists(device)) esp_hidh_dev_close(device);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
}

}  // namespace

bool ble_remote_start() {
    events = xEventGroupCreate();
    if (events == nullptr) return false;
    // NVS is already initialised by app_main (bonding keys live there).
    if (esp_hid_gap_init(HIDH_BLE_MODE) != ESP_OK ||
        esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler) != ESP_OK) {
        set_state("BT INIT ERROR");
        return false;
    }
    esp_hidh_config_t config = {};
    config.callback = hid_callback;
    config.event_stack_size = 4096;
    if (esp_hidh_init(&config) != ESP_OK) {
        set_state("HID INIT ERROR");
        return false;
    }
    return spawn(scan_task, "ble_scan", 6144, nullptr, 2, 0);
}
