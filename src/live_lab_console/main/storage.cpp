#include "storage.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "app_state.h"
#include "board.h"
#include "vga4.h"

#include "driver/sdspi_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

namespace {

constexpr char TAG[] = "llc_sd";
constexpr char MOUNT[] = "/sdcard";
constexpr char LOG_PATH[] = "/sdcard/llc_log.csv";
constexpr char STRESS_PATH[] = "/sdcard/llc_stress.bin";
constexpr size_t STRESS_CHUNK = 8 * 1024;   // internal DMA buffer
constexpr size_t STRESS_FILE = 8 * 1024 * 1024;

sdmmc_card_t *card = nullptr;
SemaphoreHandle_t file_lock = nullptr;   // the log and the stress loop share FATFS
volatile bool stress_on = false;
TaskHandle_t stress_task_handle = nullptr;
char pending_mark[24] = {};

void count_error() {
    app_state_update([](AppState &s, void *) { ++s.sd_errors; }, nullptr);
}

bool mount() {
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = board::SPI_HOST;
    slot.gpio_cs = board::SD_CS;
    esp_vfs_fat_sdmmc_mount_config_t options = {};
    options.format_if_mount_failed = false;
    options.max_files = 4;
    options.allocation_unit_size = 16 * 1024;
    static constexpr int FREQUENCIES_KHZ[] = {SDMMC_FREQ_DEFAULT, 10000};
    for (const int khz : FREQUENCIES_KHZ) {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = board::SPI_HOST;
        host.max_freq_khz = khz;
        if (esp_vfs_fat_sdspi_mount(MOUNT, &host, &slot, &options, &card) == ESP_OK) {
            return true;
        }
        // A failed mount leaves CS as an input: keep both slaves deselected.
        gpio_set_direction(board::SD_CS, GPIO_MODE_OUTPUT);
        gpio_set_level(board::SD_CS, 1);
        gpio_set_level(board::W5500_CS, 1);
    }
    return false;
}

void write_log_line(FILE *file, uint32_t &lines) {
    const AppState s = app_state_snapshot();
    const vga::Stats v = vga::stats();
    char mark[24];
    copy_text(mark, sizeof(mark), pending_mark);
    pending_mark[0] = '\0';
    std::fprintf(file, "%lu,%.2f,%.2f,%lu,%lu,%.0f,%.1f,%.1f,%.0f,%u,%u,%lu,%d,%s\n",
                 static_cast<unsigned long>(s.uptime_s), s.temp_centi_c / 100.0,
                 s.pressure_pa / 100.0, static_cast<unsigned long>(v.late),
                 static_cast<unsigned long>(v.irregular_frames), v.max_exec_us,
                 s.eth_rx_mbps, s.wifi_rx_mbps, s.sd_write_kbps, s.cpu_load[0],
                 s.cpu_load[1], static_cast<unsigned long>(s.heap_internal_free),
                 static_cast<int>(s.stress), mark);
    ++lines;
}

void log_task(void *) {
    uint32_t lines = 0;
    xSemaphoreTake(file_lock, portMAX_DELAY);
    FILE *file = std::fopen(LOG_PATH, "a");
    if (file != nullptr) {
        std::fprintf(file, "# STEAM Live Lab Console boot\n"
                           "uptime_s,temp_c,pressure_hpa,vga_late,vga_irregular,isr_max_us,"
                           "eth_mbps,wifi_mbps,sd_kBps,cpu0,cpu1,sram_free,stress,mark\n");
    }
    xSemaphoreGive(file_lock);
    if (file == nullptr) {
        app_event("SD: cannot open %s (errno %d)", LOG_PATH, errno);
        count_error();
        task_exit();
    }
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        xSemaphoreTake(file_lock, portMAX_DELAY);
        write_log_line(file, lines);
        bool ok = true;
        if (lines % 5 == 0) ok = std::fflush(file) == 0 && fsync(fileno(file)) == 0;
        xSemaphoreGive(file_lock);
        if (!ok) count_error();
        app_state_update([](AppState &s, void *raw) {
            s.sd_log_lines = *static_cast<uint32_t *>(raw);
        }, &lines);
    }
}

// Rewrites an 8 MB file in 8 kB writes with an fsync every 256 kB. Its SPI
// transactions interleave with the W5500's on the shared bus.
void stress_task(void *) {
    auto *buffer = static_cast<uint8_t *>(heap_caps_malloc(
        STRESS_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (buffer == nullptr) {
        app_event("SD stress: no DMA buffer");
        task_exit();
    }
    for (size_t index = 0; index < STRESS_CHUNK; ++index) buffer[index] = static_cast<uint8_t>(index * 31 + 7);
    uint64_t total = 0;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // wait for stress ON
        app_event("SD stress writer started");
        int file = -1;
        size_t offset = 0;
        uint32_t window_bytes = 0;
        int64_t window_start = esp_timer_get_time();
        while (stress_on) {
            xSemaphoreTake(file_lock, portMAX_DELAY);
            if (file < 0) file = ::open(STRESS_PATH, O_WRONLY | O_CREAT, 0644);
            bool ok = file >= 0;
            if (ok) {
                if (offset >= STRESS_FILE) {
                    offset = 0;
                    ok = lseek(file, 0, SEEK_SET) == 0;
                }
                ok = ok && ::write(file, buffer, STRESS_CHUNK) == static_cast<ssize_t>(STRESS_CHUNK);
                offset += STRESS_CHUNK;
                if (ok && offset % (256 * 1024) == 0) ok = fsync(file) == 0;
            }
            xSemaphoreGive(file_lock);
            if (!ok) {
                count_error();
                if (file >= 0) ::close(file);
                file = -1;
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            total += STRESS_CHUNK;
            window_bytes += STRESS_CHUNK;
            const int64_t now = esp_timer_get_time();
            if (now - window_start >= 1000000) {
                struct R { float kbps; uint32_t mb; } r = {
                    window_bytes / 1024.0f / ((now - window_start) / 1e6f),
                    static_cast<uint32_t>(total >> 20)};
                app_state_update([](AppState &s, void *raw) {
                    auto *v = static_cast<R *>(raw);
                    s.sd_write_kbps = v->kbps;
                    s.sd_stress_mb = v->mb;
                }, &r);
                window_bytes = 0;
                window_start = now;
            }
            taskYIELD();
        }
        if (file >= 0) {
            fsync(file);
            ::close(file);
        }
        app_state_update([](AppState &s, void *) { s.sd_write_kbps = 0; }, nullptr);
        app_event("SD stress writer stopped, %lu MB total", static_cast<unsigned long>(total >> 20));
    }
}

}  // namespace

bool storage_start() {
    file_lock = xSemaphoreCreateMutex();
    if (file_lock == nullptr) return false;
    if (!mount()) {
        app_event("microSD not found (FAT card in the slot?)");
        return false;
    }
    struct Info { uint16_t khz; char name[24]; } info = {};
    info.khz = static_cast<uint16_t>(card->real_freq_khz);
    char cid[9] = {};
    copy_text(cid, sizeof(cid), card->cid.name);
    std::snprintf(info.name, sizeof(info.name), "%s %luMB", cid,
                  static_cast<unsigned long>((static_cast<uint64_t>(card->csd.capacity) *
                                              card->csd.sector_size) >> 20) % 10000000UL);
    app_state_update([](AppState &s, void *raw) {
        auto *v = static_cast<Info *>(raw);
        s.sd_ok = true;
        s.sd_khz = v->khz;
        copy_text(s.sd_card, sizeof(s.sd_card), v->name);
    }, &info);
    app_event("microSD %s at %u kHz", info.name, info.khz);
    spawn(log_task, "sd_log", 4096, nullptr, 2, 0);
    spawn(stress_task, "sd_stress", 4096, nullptr, 1, 0,
          reinterpret_cast<void **>(&stress_task_handle));
    return true;
}

void storage_set_stress(bool on) {
    stress_on = on;
    if (on && stress_task_handle != nullptr) xTaskNotifyGive(stress_task_handle);
}

void storage_mark(const char *source) {
    copy_text(pending_mark, sizeof(pending_mark), source);
}
