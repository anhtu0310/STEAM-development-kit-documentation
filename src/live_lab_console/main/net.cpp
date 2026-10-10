#include "net.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "app_state.h"
#include "board.h"

#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

namespace {

constexpr char TAG[] = "llc_net";
constexpr int W5500_CLOCK_HZ = 20'000'000;
constexpr int SINK_PORT = 5001;
constexpr int DHCP_FALLBACK_S = 8;

esp_eth_handle_t eth = nullptr;
esp_netif_t *eth_netif = nullptr;
esp_netif_t *ap_netif = nullptr;
std::atomic<uint32_t> eth_rx_bytes{0};
std::atomic<uint32_t> wifi_rx_bytes{0};
std::atomic<uint32_t> sink_clients{0};
int64_t link_up_at_us = 0;

void set_eth(bool link, bool has_ip, bool fallback, const char *ip, uint16_t speed) {
    struct Context { bool link, has_ip, fallback; const char *ip; uint16_t speed; } c = {
        link, has_ip, fallback, ip, speed};
    app_state_update([](AppState &s, void *raw) {
        auto *v = static_cast<Context *>(raw);
        s.eth_link = v->link;
        s.eth_has_ip = v->has_ip;
        s.eth_static_fallback = v->fallback;
        copy_text(s.eth_ip, sizeof(s.eth_ip), v->ip);
        s.eth_speed_mbps = v->speed;
    }, &c);
}

void on_eth_event(void *, esp_event_base_t, int32_t id, void *data) {
    esp_eth_handle_t handle = *static_cast<esp_eth_handle_t *>(data);
    if (id == ETHERNET_EVENT_CONNECTED) {
        eth_speed_t speed = ETH_SPEED_10M;
        esp_eth_ioctl(handle, ETH_CMD_G_SPEED, &speed);
        link_up_at_us = esp_timer_get_time();
        set_eth(true, false, false, "-", speed == ETH_SPEED_100M ? 100 : 10);
        app_event("Ethernet link up, %s Mbit/s", speed == ETH_SPEED_100M ? "100" : "10");
    } else if (id == ETHERNET_EVENT_DISCONNECTED) {
        link_up_at_us = 0;
        set_eth(false, false, false, "-", 0);
        esp_netif_dhcpc_start(eth_netif);   // try DHCP again on the next link
        app_event("Ethernet link down");
    }
}

void on_got_ip(void *, esp_event_base_t, int32_t id, void *data) {
    const auto *event = static_cast<ip_event_got_ip_t *>(data);
    char ip[16];
    std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
    if (id == IP_EVENT_ETH_GOT_IP) {
        link_up_at_us = 0;
        const AppState s = app_state_snapshot();
        set_eth(true, true, false, ip, s.eth_speed_mbps);
        app_event("Ethernet DHCP %s  http://%s/", ip, ip);
    }
}

void on_wifi_event(void *, esp_event_base_t, int32_t id, void *data) {
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        const auto *e = static_cast<wifi_event_ap_staconnected_t *>(data);
        app_event("Wi-Fi client " MACSTR " joined", MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        const auto *e = static_cast<wifi_event_ap_stadisconnected_t *>(data);
        app_event("Wi-Fi client " MACSTR " left", MAC2STR(e->mac));
    }
}

bool start_ethernet() {
    spi_device_interface_config_t device = {};
    device.command_bits = 16;
    device.address_bits = 8;
    device.mode = 0;
    device.clock_speed_hz = W5500_CLOCK_HZ;
    device.spics_io_num = board::W5500_CS;
    device.queue_size = 20;
    eth_w5500_config_t w5500 = ETH_W5500_DEFAULT_CONFIG(board::SPI_HOST, &device);
    w5500.int_gpio_num = board::W5500_INT;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    mac_config.flags |= ETH_MAC_FLAG_PIN_TO_CORE;   // installed from core 0
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = board::W5500_RESET;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    if (mac == nullptr || phy == nullptr) return false;
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    if (esp_eth_driver_install(&config, &eth) != ESP_OK) {
        mac->del(mac);
        phy->del(phy);
        return false;
    }
    uint8_t address[6];
    if (esp_read_mac(address, ESP_MAC_ETH) != ESP_OK ||
        esp_eth_ioctl(eth, ETH_CMD_S_MAC_ADDR, address) != ESP_OK) {
        return false;
    }
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
    eth_netif = esp_netif_new(&netif_config);
    if (eth_netif == nullptr ||
        esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth)) != ESP_OK) {
        return false;
    }
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_eth_event, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_got_ip, nullptr);
    return esp_eth_start(eth) == ESP_OK;
}

void use_static_fallback() {
    esp_netif_dhcpc_stop(eth_netif);
    esp_netif_ip_info_t info = {};
    info.ip.addr = esp_ip4addr_aton(CONFIG_LLC_ETH_FALLBACK_IP);
    info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    info.gw.addr = 0;
    if (esp_netif_set_ip_info(eth_netif, &info) == ESP_OK) {
        const AppState s = app_state_snapshot();
        set_eth(true, true, true, CONFIG_LLC_ETH_FALLBACK_IP, s.eth_speed_mbps);
        app_event("No DHCP: Ethernet static %s", CONFIG_LLC_ETH_FALLBACK_IP);
    }
}

bool start_wifi_ap() {
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);   // no NVS writes at run time
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr);
    ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == nullptr || esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) return false;

    wifi_config_t config = {};
    copy_text(reinterpret_cast<char *>(config.ap.ssid), sizeof(config.ap.ssid),
              CONFIG_LLC_WIFI_AP_SSID);
    copy_text(reinterpret_cast<char *>(config.ap.password), sizeof(config.ap.password),
              CONFIG_LLC_WIFI_AP_PASSWORD);
    config.ap.ssid_len = std::strlen(CONFIG_LLC_WIFI_AP_SSID);
    config.ap.channel = CONFIG_LLC_WIFI_AP_CHANNEL;
    config.ap.max_connection = 4;
    config.ap.authmode = std::strlen(CONFIG_LLC_WIFI_AP_PASSWORD) >= 8
        ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if (esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK) return false;
    if (esp_wifi_start() != ESP_OK) return false;

    esp_netif_ip_info_t ip = {};
    char address[16] = "192.168.4.1";
    if (esp_netif_get_ip_info(ap_netif, &ip) == ESP_OK) {
        std::snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
    }
    app_state_update([](AppState &s, void *raw) {
        s.wifi_up = true;
        copy_text(s.wifi_ssid, sizeof(s.wifi_ssid), CONFIG_LLC_WIFI_AP_SSID);
        copy_text(s.wifi_ip, sizeof(s.wifi_ip), static_cast<const char *>(raw));
    }, address);
    app_event("Wi-Fi AP %s  http://%s/", CONFIG_LLC_WIFI_AP_SSID, address);
    return true;
}

// ------------------------------------------------------------ TCP sink :5001
void sink_client_task(void *argument) {
    const int client = static_cast<int>(reinterpret_cast<intptr_t>(argument));
    sockaddr_in local = {};
    socklen_t length = sizeof(local);
    getsockname(client, reinterpret_cast<sockaddr *>(&local), &length);
    esp_netif_ip_info_t ap_ip = {};
    esp_netif_get_ip_info(ap_netif, &ap_ip);
    const bool over_wifi = ap_netif != nullptr && local.sin_addr.s_addr == ap_ip.ip.addr;
    std::atomic<uint32_t> &counter = over_wifi ? wifi_rx_bytes : eth_rx_bytes;
    app_event("Load sink: client on %s", over_wifi ? "Wi-Fi" : "Ethernet");
    ++sink_clients;
    static uint8_t buffers[2][2048];   // internal: lwIP copies into it
    uint8_t *buffer = buffers[over_wifi ? 1 : 0];
    while (true) {
        const int received = recv(client, buffer, sizeof(buffers[0]), 0);
        if (received <= 0) break;
        counter += static_cast<uint32_t>(received);
    }
    --sink_clients;
    close(client);
    app_event("Load sink: %s client closed", over_wifi ? "Wi-Fi" : "Ethernet");
    task_exit();
}

void sink_task(void *) {
    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(SINK_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(server, 2) != 0) {
        ESP_LOGE(TAG, "TCP sink: bind/listen failed");
        task_exit();
    }
    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) continue;
        if (sink_clients >= 2 ||
            !spawn(sink_client_task, "sink_rx", 3072,
                   reinterpret_cast<void *>(static_cast<intptr_t>(client)), 4, 0)) {
            close(client);
        }
    }
}

// ------------------------------------------------------------ 1 Hz monitor
void monitor_task(void *) {
    uint32_t last_eth = 0;
    uint32_t last_wifi = 0;
    uint64_t total_eth = 0;
    uint64_t total_wifi = 0;
    int64_t last_us = esp_timer_get_time();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const int64_t now = esp_timer_get_time();
        const float seconds = (now - last_us) / 1e6f;
        last_us = now;
        const uint32_t e = eth_rx_bytes.load();
        const uint32_t w = wifi_rx_bytes.load();
        const uint32_t de = e - last_eth;
        const uint32_t dw = w - last_wifi;
        last_eth = e;
        last_wifi = w;
        total_eth += de;
        total_wifi += dw;

        wifi_sta_list_t stations = {};
        const uint8_t count = esp_wifi_ap_get_sta_list(&stations) == ESP_OK ? stations.num : 0;

        struct Rates { float e, w; uint32_t emb, wmb, clients; uint8_t sta; } r = {
            de * 8 / 1e6f / seconds, dw * 8 / 1e6f / seconds,
            static_cast<uint32_t>(total_eth >> 20), static_cast<uint32_t>(total_wifi >> 20),
            sink_clients.load(), count};
        app_state_update([](AppState &s, void *raw) {
            auto *v = static_cast<Rates *>(raw);
            s.eth_rx_mbps = v->e;
            s.wifi_rx_mbps = v->w;
            s.eth_rx_mb = v->emb;
            s.wifi_rx_mb = v->wmb;
            s.sink_clients = v->clients;
            s.wifi_stations = v->sta;
        }, &r);

        if (link_up_at_us != 0 && now - link_up_at_us > DHCP_FALLBACK_S * 1000000LL) {
            link_up_at_us = 0;
            use_static_fallback();
        }
    }
}

}  // namespace

bool net_start() {
    if (esp_netif_init() != ESP_OK) return false;
    esp_err_t error = esp_event_loop_create_default();
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;
    error = gpio_install_isr_service(0);   // W5500 INT; allocated on core 0
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return false;

    const bool eth_ok = start_ethernet();
    app_state_update([](AppState &s, void *raw) { s.eth_started = *static_cast<bool *>(raw); },
                     const_cast<bool *>(&eth_ok));
    if (!eth_ok) app_event("W5500 not responding");
    const bool wifi_ok = start_wifi_ap();
    if (!wifi_ok) app_event("Wi-Fi AP failed to start");

    spawn(sink_task, "sink", 3072, nullptr, 4, 0);
    spawn(monitor_task, "net_mon", 3072, nullptr, 3, 0);
    return eth_ok || wifi_ok;
}
