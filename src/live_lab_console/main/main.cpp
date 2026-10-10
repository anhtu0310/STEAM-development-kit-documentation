/*
 * STEAM Live Lab Console: every VGA-mode function of the STEAM ESP32-S3 board
 * at once. 800x600 VGA dashboard + W5500 Ethernet + Wi-Fi soft-AP + BLE HID
 * clicker + USB keyboard + microSD log + BMP280 + SH1106 OLED + relay + GPIO0.
 *
 * Hardware mode: VGA. S6-1 OFF (colour buffers on), S6-2 OFF (or WS2812B
 * flickers with VSYNC), S6-3 ON to blank the 7-segment display, S6-4 OFF.
 *
 * Core split (see VGA_STABILITY.md):
 *   core 1  VGA refill + VSYNC interrupts, dashboard renderer (priority 2)
 *   core 0  everything else: Wi-Fi, BLE controller + Bluedroid, lwIP, W5500,
 *           HTTP, USB host, SD, I2C, relay/button, this control task
 */

#include <array>
#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "ble_remote.h"
#include "board.h"
#include "dashboard.h"
#include "io.h"
#include "net.h"
#include "sensors.h"
#include "storage.h"
#include "usb_keyboard.h"
#include "vga4.h"
#include "web.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace {

constexpr char TAG[] = "llc_main";
constexpr size_t HAMMER_BYTES = 256 * 1024;

TaskHandle_t hammer_handle = nullptr;
volatile bool hammer_on = false;

void log_memory(const char *stage) {
    ESP_LOGI(TAG, "%-14s SRAM free %6u (DMA largest %6u)  PSRAM free %7u", stage,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void report(const char *name, bool ok) {
    ESP_LOGI(TAG, "%s: %s", name, ok ? "running" : "NOT available");
    if (!ok) app_event("%s unavailable", name);
}

bool init_shared_spi() {
    // Both chip selects high before anything touches MISO.
    gpio_config_t selects = {};
    selects.pin_bit_mask = (1ULL << board::W5500_CS) | (1ULL << board::SD_CS);
    selects.mode = GPIO_MODE_OUTPUT;
    selects.pull_up_en = GPIO_PULLUP_ENABLE;
    if (gpio_config(&selects) != ESP_OK) return false;
    gpio_set_level(board::W5500_CS, 1);
    gpio_set_level(board::SD_CS, 1);
    gpio_set_pull_mode(board::SPI_MISO, GPIO_PULLUP_ONLY);

    spi_bus_config_t bus = {};
    bus.mosi_io_num = board::SPI_MOSI;
    bus.miso_io_num = board::SPI_MISO;
    bus.sclk_io_num = board::SPI_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 16 * 1024;
    // Called from app_main, so the SPI interrupt is allocated on core 0.
    return spi_bus_initialize(board::SPI_HOST, &bus, SPI_DMA_CH_AUTO) == ESP_OK;
}

// ---------------------------------------------------------------- envelope
// Deliberately outside the operating envelope: core 0 copies 256 kB blocks
// PSRAM->PSRAM without pause, the pattern behind the original flicker (a
// second core streaming PSRAM while the refill ISR reads it). The late-refill
// counter should rise, and the picture should recover at each VSYNC.
void hammer_task(void *) {
    auto *a = static_cast<uint8_t *>(heap_caps_malloc(HAMMER_BYTES, MALLOC_CAP_SPIRAM));
    auto *b = static_cast<uint8_t *>(heap_caps_malloc(HAMMER_BYTES, MALLOC_CAP_SPIRAM));
    if (a == nullptr || b == nullptr) {
        app_event("Envelope: no PSRAM for the hammer");
        task_exit();
    }
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint32_t loops = 0;
        while (hammer_on) {
            std::memcpy((loops & 1) ? a : b, (loops & 1) ? b : a, HAMMER_BYTES);
            if ((++loops & 15) == 0) vTaskDelay(1);   // let core-0 radios breathe
        }
    }
}

void set_stress(StressLevel level, const char *source) {
    storage_set_stress(level != StressLevel::OFF);
    hammer_on = level == StressLevel::ENVELOPE;
    if (hammer_on && hammer_handle != nullptr) xTaskNotifyGive(hammer_handle);
    app_state_update([](AppState &s, void *raw) { s.stress = *static_cast<StressLevel *>(raw); },
                     &level);
    static const char *const names[] = {"OFF", "ON (SD writes + frame-rate redraw)",
                                        "ENVELOPE (PSRAM hammer on core 0)"};
    app_event("Stress %s by %s", names[static_cast<int>(level)], source);
}

void handle(Command command, const char *source) {
    const AppState s = app_state_snapshot();
    switch (command) {
        case Command::RELAY_TOGGLE: io_relay_toggle(source); break;
        case Command::RELAY_ON: io_relay_set(true, source); break;
        case Command::RELAY_OFF: io_relay_set(false, source); break;
        case Command::STRESS_TOGGLE:
            set_stress(s.stress == StressLevel::OFF ? StressLevel::REALISTIC : StressLevel::OFF, source);
            break;
        case Command::ENVELOPE_TOGGLE:
            set_stress(s.stress == StressLevel::ENVELOPE ? StressLevel::OFF : StressLevel::ENVELOPE, source);
            break;
        case Command::CLEAR_COUNTERS:
            vga::clear_stats();
            app_event("VGA counters cleared by %s", source);
            break;
        case Command::MARK:
            storage_mark(source);
            app_event("Marker from %s", source);
            break;
    }
}

// ---------------------------------------------------------------- 1 Hz system
void update_system() {
    static uint32_t last_idle[2] = {};
    static uint32_t last_total = 0;
    const uint32_t total = static_cast<uint32_t>(esp_timer_get_time());
    uint8_t load[2] = {};
    for (int core = 0; core < 2; ++core) {
        const uint32_t idle = ulTaskGetIdleRunTimeCounterForCore(core);
        const uint32_t elapsed = total - last_total;
        if (last_total != 0 && elapsed > 0) {
            const uint32_t idle_delta = idle - last_idle[core];
            const int busy = 100 - static_cast<int>(100ULL * idle_delta / elapsed);
            load[core] = static_cast<uint8_t>(busy < 0 ? 0 : (busy > 100 ? 100 : busy));
        }
        last_idle[core] = idle;
    }
    last_total = total;
    struct Sys { uint8_t load[2]; } sys = {{load[0], load[1]}};
    app_state_update([](AppState &s, void *raw) {
        auto *v = static_cast<Sys *>(raw);
        s.uptime_s = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
        s.cpu_load[0] = v->load[0];
        s.cpu_load[1] = v->load[1];
        s.heap_internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        s.heap_internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        s.heap_dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        s.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }, &sys);
}

}  // namespace

extern "C" void app_main() {
    ESP_LOGI(TAG, "STEAM Live Lab Console starting");
    if (!app_state_init()) {
        ESP_LOGE(TAG, "Out of memory for the state mutex");
        return;
    }
    // Relay output LOW before anything else (GPIO45 is a strapping pin).
    report("Relay + GPIO0 button", io_start());

    // NVS (PHY calibration, BLE bonding keys). Flash writes no longer stall
    // VGA (XIP from PSRAM), but the first-boot erase is still done here.
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);

    uint32_t flash_size = 0;
    esp_flash_get_size(nullptr, &flash_size);
    const uint32_t psram_size = static_cast<uint32_t>(esp_psram_get_size());
    app_state_update([](AppState &s, void *raw) {
        const auto *v = static_cast<uint32_t *>(raw);
        s.flash_size = v[0];
        s.psram_size = v[1];
    }, std::array<uint32_t, 2>{flash_size, psram_size}.data());
    app_event("Chip reports %lu MB flash, %lu MB PSRAM",
              static_cast<unsigned long>(flash_size >> 20), static_cast<unsigned long>(psram_size >> 20));
    log_memory("boot");

    // VGA first, so its internal DMA bounce buffers exist before the radio
    // stacks take internal SRAM.
    const bool vga_ok = vga::start();
    report("VGA 800x600", vga_ok);
    if (vga_ok) report("Dashboard renderer", dashboard_start());
    log_memory("after VGA");

    const bool spi_ok = init_shared_spi();
    report("Shared SPI bus", spi_ok);
    if (spi_ok) report("microSD log", storage_start());
    report("I2C OLED + BMP280", sensors_start());
    report("Ethernet + Wi-Fi AP", net_start());
    report("HTTP dashboard", web_start());
    log_memory("after network");
    report("BLE remote", ble_remote_start());
    log_memory("after BLE");
    report("USB keyboard host", usb_keyboard_start());
    log_memory("after USB");

    spawn(hammer_task, "hammer", 3072, nullptr, 1, 0, reinterpret_cast<void **>(&hammer_handle));
    app_event("All services started; S = stress, X = envelope");

    int64_t next_system = 0;
    while (true) {
        Command command;
        char source[12];
        if (app_command_receive(&command, source, sizeof(source), 100)) handle(command, source);
        const int64_t now = esp_timer_get_time();
        if (now >= next_system) {
            update_system();
            next_system = now + 1000000;
        }
    }
}
