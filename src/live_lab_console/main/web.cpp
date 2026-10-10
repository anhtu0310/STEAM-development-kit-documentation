#include "web.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_state.h"
#include "vga4.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"

namespace {

constexpr char TAG[] = "llc_web";
httpd_handle_t server = nullptr;

extern "C" const uint8_t index_html_start[] asm("_binary_index_html_start");
extern "C" const uint8_t index_html_end[] asm("_binary_index_html_end");

void count_request() {
    app_state_update([](AppState &s, void *) { ++s.http_requests; }, nullptr);
}

esp_err_t root_handler(httpd_req_t *request) {
    count_request();
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, reinterpret_cast<const char *>(index_html_start),
                           index_html_end - index_html_start - 1);   // EMBED_TXTFILES adds a NUL
}

const char *stress_name(StressLevel level) {
    switch (level) {
        case StressLevel::REALISTIC: return "stress";
        case StressLevel::ENVELOPE: return "envelope";
        default: return "off";
    }
}

esp_err_t status_handler(httpd_req_t *request) {
    count_request();
    const AppState s = app_state_snapshot();
    const vga::Stats v = vga::stats();
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return httpd_resp_send_500(request);

    cJSON *o = cJSON_AddObjectToObject(root, "vga");
    cJSON_AddNumberToObject(o, "late", v.late);
    cJSON_AddNumberToObject(o, "irregular_frames", v.irregular_frames);
    cJSON_AddNumberToObject(o, "refills", v.refills);
    cJSON_AddNumberToObject(o, "frames", v.vsyncs);
    cJSON_AddNumberToObject(o, "max_exec_us", v.max_exec_us);
    cJSON_AddNumberToObject(o, "budget_us", v.budget_us);
    cJSON_AddNumberToObject(o, "min_slack_us", v.min_slack_us);
    cJSON_AddNumberToObject(o, "max_late_us", v.max_late_us);
    cJSON_AddNumberToObject(o, "bounce_lines", v.bounce_lines);
    cJSON_AddNumberToObject(o, "refresh_hz", v.refresh_hz);

    o = cJSON_AddObjectToObject(root, "eth");
    cJSON_AddBoolToObject(o, "link", s.eth_link);
    cJSON_AddStringToObject(o, "ip", s.eth_ip);
    cJSON_AddBoolToObject(o, "static", s.eth_static_fallback);
    cJSON_AddNumberToObject(o, "rx_mbps", s.eth_rx_mbps);
    cJSON_AddNumberToObject(o, "rx_mb", s.eth_rx_mb);

    o = cJSON_AddObjectToObject(root, "wifi");
    cJSON_AddBoolToObject(o, "up", s.wifi_up);
    cJSON_AddStringToObject(o, "ssid", s.wifi_ssid);
    cJSON_AddStringToObject(o, "ip", s.wifi_ip);
    cJSON_AddNumberToObject(o, "stations", s.wifi_stations);
    cJSON_AddNumberToObject(o, "rx_mbps", s.wifi_rx_mbps);
    cJSON_AddNumberToObject(o, "rx_mb", s.wifi_rx_mb);

    o = cJSON_AddObjectToObject(root, "ble");
    cJSON_AddStringToObject(o, "state", s.ble_state);
    cJSON_AddNumberToObject(o, "battery", s.ble_battery);
    cJSON_AddStringToObject(o, "last", s.ble_last);

    o = cJSON_AddObjectToObject(root, "usb");
    cJSON_AddStringToObject(o, "state", s.usb_state);
    cJSON_AddStringToObject(o, "last", s.usb_last);

    o = cJSON_AddObjectToObject(root, "sd");
    cJSON_AddBoolToObject(o, "ok", s.sd_ok);
    cJSON_AddNumberToObject(o, "khz", s.sd_khz);
    cJSON_AddNumberToObject(o, "log_lines", s.sd_log_lines);
    cJSON_AddNumberToObject(o, "write_kbps", s.sd_write_kbps);
    cJSON_AddNumberToObject(o, "stress_mb", s.sd_stress_mb);
    cJSON_AddNumberToObject(o, "errors", s.sd_errors);

    o = cJSON_AddObjectToObject(root, "bmp280");
    cJSON_AddBoolToObject(o, "ok", s.bmp_ok);
    cJSON_AddNumberToObject(o, "temperature_c", s.temp_centi_c / 100.0);
    cJSON_AddNumberToObject(o, "pressure_hpa", s.pressure_pa / 100.0);

    o = cJSON_AddObjectToObject(root, "relay");
    cJSON_AddBoolToObject(o, "on", s.relay_on);
    cJSON_AddNumberToObject(o, "remaining_ms", s.relay_remaining_ms);
    cJSON_AddStringToObject(o, "source", s.relay_source);

    o = cJSON_AddObjectToObject(root, "system");
    cJSON_AddNumberToObject(o, "uptime_s", s.uptime_s);
    cJSON_AddNumberToObject(o, "cpu0", s.cpu_load[0]);
    cJSON_AddNumberToObject(o, "cpu1", s.cpu_load[1]);
    cJSON_AddNumberToObject(o, "sram_free", s.heap_internal_free);
    cJSON_AddNumberToObject(o, "sram_min", s.heap_internal_min);
    cJSON_AddNumberToObject(o, "psram_free", s.psram_free);
    cJSON_AddNumberToObject(o, "psram_size", s.psram_size);
    cJSON_AddNumberToObject(o, "flash_size", s.flash_size);
    cJSON_AddStringToObject(o, "stress", stress_name(s.stress));
    cJSON_AddNumberToObject(o, "http_requests", s.http_requests);

    cJSON *events = cJSON_AddArrayToObject(root, "events");
    for (const auto &line : s.events) {
        if (line[0] != '\0') cJSON_AddItemToArray(events, cJSON_CreateString(line));
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body == nullptr) return httpd_resp_send_500(request);
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_sendstr(request, body);
    std::free(body);
    return result;
}

esp_err_t command_handler(httpd_req_t *request) {
    count_request();
    char query[48] = {};
    char value[16] = {};
    if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "c", value, sizeof(value)) != ESP_OK) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "missing ?c=");
    }
    Command command;
    if (std::strcmp(value, "relay") == 0) command = Command::RELAY_TOGGLE;
    else if (std::strcmp(value, "relay_on") == 0) command = Command::RELAY_ON;
    else if (std::strcmp(value, "relay_off") == 0) command = Command::RELAY_OFF;
    else if (std::strcmp(value, "stress") == 0) command = Command::STRESS_TOGGLE;
    else if (std::strcmp(value, "envelope") == 0) command = Command::ENVELOPE_TOGGLE;
    else if (std::strcmp(value, "clear") == 0) command = Command::CLEAR_COUNTERS;
    else if (std::strcmp(value, "mark") == 0) command = Command::MARK;
    else return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "unknown command");
    const bool queued = app_command(command, "WEB");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, queued ? "{\"accepted\":true}" : "{\"accepted\":false}");
}

bool add(const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
    httpd_uri_t endpoint = {};
    endpoint.uri = uri;
    endpoint.method = method;
    endpoint.handler = handler;
    return httpd_register_uri_handler(server, &endpoint) == ESP_OK;
}

}  // namespace

bool web_start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.core_id = 0;   // keep core 1 for VGA
    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    config.stack_size = 6144;
    config.max_open_sockets = 6;
    config.lru_purge_enable = true;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server failed to start");
        return false;
    }
    return add("/", HTTP_GET, root_handler) &&
           add("/api/status", HTTP_GET, status_handler) &&
           add("/api/cmd", HTTP_POST, command_handler);
}
