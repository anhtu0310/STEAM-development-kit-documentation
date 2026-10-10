#pragma once

// One mutex-protected snapshot of everything the VGA dashboard, the OLED and
// the web page show. Producers (network, BLE, USB, SD, sensor, relay) write
// small fields; the three views copy the whole struct. Nothing in here is
// touched from an interrupt.

#include <cstddef>
#include <cstdint>

enum class Command : uint8_t {
    RELAY_TOGGLE,
    RELAY_ON,
    RELAY_OFF,
    STRESS_TOGGLE,     // realistic stress: SD writes + max-rate redraw
    ENVELOPE_TOGGLE,   // deliberately breaks the budget: PSRAM hammer on core 0
    CLEAR_COUNTERS,
    MARK,              // writes a marker line to the SD log and event list
};

enum class StressLevel : uint8_t { OFF = 0, REALISTIC = 1, ENVELOPE = 2 };

constexpr int EVENT_COUNT = 9;
constexpr int EVENT_TEXT = 72;

struct AppState {
    uint32_t generation;

    // Ethernet (W5500)
    bool eth_started;
    bool eth_link;
    bool eth_has_ip;
    bool eth_static_fallback;
    char eth_ip[16];
    uint16_t eth_speed_mbps;

    // Wi-Fi soft-AP
    bool wifi_up;
    char wifi_ssid[33];
    char wifi_ip[16];
    uint8_t wifi_stations;

    // Network load (TCP sink on port 5001, both interfaces)
    float eth_rx_mbps;
    float wifi_rx_mbps;
    uint32_t eth_rx_mb;
    uint32_t wifi_rx_mb;
    uint32_t sink_clients;
    uint32_t http_requests;

    // BLE remote
    char ble_state[18];
    int8_t ble_battery;   // -1 = unknown
    char ble_last[16];
    uint32_t ble_presses;

    // USB keyboard
    char usb_state[18];
    char usb_last[16];
    uint32_t usb_keys;

    // microSD
    bool sd_ok;
    uint16_t sd_khz;
    uint32_t sd_log_lines;
    float sd_write_kbps;   // kB/s, stress writer
    uint32_t sd_stress_mb;
    uint32_t sd_errors;
    char sd_card[24];

    // BMP280
    bool bmp_ok;
    int32_t temp_centi_c;
    uint32_t pressure_pa;

    // Relay and button
    bool relay_on;
    uint32_t relay_remaining_ms;
    char relay_source[12];
    uint32_t button_presses;

    // System
    uint32_t uptime_s;
    uint32_t heap_internal_free;
    uint32_t heap_internal_min;
    uint32_t heap_dma_largest;
    uint32_t psram_free;
    uint32_t psram_size;
    uint32_t flash_size;
    uint8_t cpu_load[2];   // percent per core

    StressLevel stress;

    char events[EVENT_COUNT][EVENT_TEXT];   // [0] = newest
};

bool app_state_init();
AppState app_state_snapshot();

// Runs `edit` with the mutex held and bumps the generation. Keep it short.
void app_state_update(void (*edit)(AppState &state, void *context), void *context);

// Prepends a timestamped line to the event list (and the serial log).
void app_event(const char *format, ...) __attribute__((format(printf, 1, 2)));

// Bounded copy that always terminates.
void copy_text(char *target, size_t capacity, const char *source);

// Command queue: every input (keyboard, BLE, GPIO0, web) ends up here.
bool app_command(Command command, const char *source);
// Starts an application task with its stack in PSRAM (internal SRAM is kept
// for the VGA bounce buffers, DMA and the radio stacks). Code and data run
// from PSRAM anyway (XIP), so this costs nothing in behaviour. End such a
// task with task_exit(), never vTaskDelete().
bool spawn(void (*function)(void *), const char *name, uint32_t stack_bytes,
           void *argument, unsigned priority, int core, void **handle = nullptr);
[[noreturn]] void task_exit();

bool app_command_receive(Command *command, char *source, size_t source_capacity,
                         uint32_t wait_ms);
