#include "wifi/wifi_manager.h"

#include <algorithm>
#include <cstring>
#include <map>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system/control_time.h"
#include "lwip/inet.h"

namespace {
constexpr const char *TAG = "CONTROL_WIFI";
constexpr int PENDING_SCAN = 1;
constexpr int PENDING_CONNECTED = 2;
constexpr int PENDING_FAILED = 3;

std::string ip4ToString(const esp_ip4_addr_t &addr) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), IPSTR, IP2STR(&addr));
    return buffer;
}

std::string ip6ToString(const esp_ip6_addr_t &addr) {
    char buffer[40];
    snprintf(buffer, sizeof(buffer), IPV6STR, IPV62STR(addr));
    return buffer;
}

const char *authName(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        case WIFI_AUTH_OWE: return "OWE";
        default: return "UNKNOWN";
    }
}
}

bool WifiManager::initialize() {
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        status_ = "Échec esp_netif";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        status_ = "Échec boucle événements";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        return false;
    }
    stationNetif_ = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!stationNetif_) {
        stationNetif_ = esp_netif_create_default_wifi_sta();
    }
    if (!stationNetif_) {
        status_ = "Échec interface Wi-Fi";
        ESP_LOGE(TAG, "%s", status_.c_str());
        return false;
    }
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&config);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGI(TAG, "Wi-Fi driver déjà initialisé; réutilisation");
        err = ESP_OK;
    }
    if (err != ESP_OK ||
        (err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            &WifiManager::eventHandler, this, &wifiHandler_)) != ESP_OK ||
        (err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            &WifiManager::eventHandler, this, &ipHandler_)) != ESP_OK ||
        (err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
        (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        status_ = "Échec initialisation Wi-Fi";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        return false;
    }
    initialized_ = true;
    status_ = "Prêt";
    ESP_LOGI(TAG, "Wi-Fi station initialisé");
    return true;
}

bool WifiManager::startScan() {
    if (!initialized_) return false;
    connecting_ = false;
    esp_wifi_disconnect();
    wifi_scan_config_t config{};
    config.show_hidden = true;
    config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    const esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan_failed err=%s", esp_err_to_name(err));
        return false;
    }
    status_ = "Scan en cours";
    ESP_LOGI(TAG, "scan_start");
    return true;
}

bool WifiManager::connect(const std::string &ssid, const std::string &password) {
    if (!initialized_ || ssid.empty() || ssid.size() > 32 || password.size() > 63) return false;
    wifi_config_t config{};
    memcpy(config.sta.ssid, ssid.data(), ssid.size());
    memcpy(config.sta.password, password.data(), password.size());
    config.sta.threshold.authmode = password.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Connexion à '%s' impossible à démarrer: %s", ssid.c_str(), esp_err_to_name(err));
        return false;
    }
    selectedSsid_ = ssid;
    connecting_ = true;
    status_ = "Connexion en cours";
    ESP_LOGI(TAG, "connect_start ssid=\"%s\"", ssid.c_str());
    return true;
}

void WifiManager::cancel() {
    connecting_ = false;
    esp_wifi_disconnect();
    connected_ = false;
    status_ = "Annulé";
    ESP_LOGI(TAG, "Opération Wi-Fi annulée");
}

void WifiManager::releaseTransientBuffers() {
    networks_.clear();
    networks_.shrink_to_fit();
    esp_err_t err = esp_wifi_clear_ap_list();
    if (err == ESP_ERR_WIFI_NOT_INIT) err = ESP_OK;
    ESP_LOGI(TAG, "transient_buffers_released scan_ap_list=%s", esp_err_to_name(err));
}

bool WifiManager::isConnected() const {
    if (!connected_) return false;
    esp_netif_ip_info_t ip{};
    return stationNetif_ && esp_netif_get_ip_info(stationNetif_, &ip) == ESP_OK && ip.ip.addr != 0;
}

bool WifiManager::hasIpv6LinkLocal() const {
    if (!stationNetif_) return false;
    esp_ip6_addr_t ip6{};
    if (esp_netif_get_ip6_linklocal(stationNetif_, &ip6) != ESP_OK) return false;
    return esp_netif_ip6_get_addr_type(&ip6) == ESP_IP6_ADDR_IS_LINK_LOCAL;
}

esp_err_t WifiManager::waitMatterNetworkReady(uint32_t timeout_ms) {
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
    bool createIp6Requested = false;
    ESP_LOGI(TAG, "matter_network_preflight_start timeout_ms=%u", static_cast<unsigned>(timeout_ms));
    while (esp_timer_get_time() < deadline) {
        if (!isConnected()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!createIp6Requested && !hasIpv6LinkLocal() && stationNetif_) {
            const esp_err_t ip6Err = esp_netif_create_ip6_linklocal(stationNetif_);
            ESP_LOGI(TAG, "ipv6_linklocal_create result=%s", esp_err_to_name(ip6Err));
            createIp6Requested = true;
        }
        if (hasIpv6LinkLocal()) {
            const WifiConnectionInfo info = connectionInfo();
            ESP_LOGI(TAG, "matter_network_ready ipv4=%s ipv6_ll=%s",
                     info.ipv4.empty() ? "unknown" : info.ipv4.c_str(),
                     info.ipv6LinkLocal.empty() ? "unknown" : info.ipv6LinkLocal.c_str());
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    const WifiConnectionInfo info = connectionInfo();
    ESP_LOGW(TAG, "matter_network_not_ready ipv4=%s ipv6_ll=%s connected=%s",
             info.ipv4.empty() ? "unknown" : info.ipv4.c_str(),
             info.ipv6LinkLocal.empty() ? "none" : info.ipv6LinkLocal.c_str(),
             isConnected() ? "yes" : "no");
    return isConnected() ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
}

void WifiManager::eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    auto *self = static_cast<WifiManager *>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        self->pending_ = PENDING_SCAN;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED && self->connecting_) {
        auto *event = static_cast<wifi_event_sta_disconnected_t *>(data);
        self->failureReason_ = event->reason;
        self->connecting_ = false;
        self->connected_ = false;
        self->pending_ = PENDING_FAILED;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        self->connecting_ = false;
        self->connected_ = true;
        if (self->stationNetif_) {
            const esp_err_t ip6Err = esp_netif_create_ip6_linklocal(self->stationNetif_);
            ESP_LOGI(TAG, "ipv6_linklocal_create_on_got_ip result=%s", esp_err_to_name(ip6Err));
        }
        (void)control_time_start_sntp_once();
        self->pending_ = PENDING_CONNECTED;
    }
}

void WifiManager::collectScanResults() {
    uint16_t count = 0;
    if (esp_wifi_scan_get_ap_num(&count) != ESP_OK) return;
    std::vector<wifi_ap_record_t> records(count);
    if (count && esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) return;

    std::map<std::string, WifiNetwork> unique;
    int hiddenIndex = 1;
    for (const auto &record : records) {
        std::string ssid(reinterpret_cast<const char *>(record.ssid));
        const bool hidden = ssid.empty();
        if (hidden) ssid = "<Réseau masqué " + std::to_string(hiddenIndex++) + ">";
        WifiNetwork network{ssid, record.rssi, record.authmode, hidden};
        auto found = unique.find(ssid);
        if (found == unique.end() || record.rssi > found->second.rssi) unique[ssid] = network;
    }
    networks_.clear();
    for (auto &item : unique) networks_.push_back(std::move(item.second));
    std::sort(networks_.begin(), networks_.end(), [](const auto &a, const auto &b) {
        return a.rssi > b.rssi;
    });
    ESP_LOGI(TAG, "%u points d'accès, %u entrées affichées après déduplication",
             count, static_cast<unsigned>(networks_.size()));
    for (const auto &network : networks_) {
        ESP_LOGI(TAG, "Scan: SSID='%s' RSSI=%d sécurité=%s",
                 network.ssid.c_str(), network.rssi,
                 authName(network.auth));
    }
}

WifiStatusEvent WifiManager::poll() {
    WifiStatusEvent event;
    const int pending = pending_.exchange(0);
    if (pending == PENDING_SCAN) {
        collectScanResults();
        status_ = "Scan terminé";
        ESP_LOGI(TAG, "scan_done count=%u", static_cast<unsigned>(networks_.size()));
        event.type = WifiEventType::SCAN_COMPLETE;
    } else if (pending == PENDING_CONNECTED) {
        status_ = "Connecté";
        esp_netif_ip_info_t ip{};
        if (stationNetif_ && esp_netif_get_ip_info(stationNetif_, &ip) == ESP_OK) {
            ESP_LOGI(TAG, "got_ip ip=" IPSTR, IP2STR(&ip.ip));
        } else {
            ESP_LOGI(TAG, "got_ip ip=unknown");
        }
        event.type = WifiEventType::CONNECTED;
    } else if (pending == PENDING_FAILED) {
        status_ = "Connexion échouée";
        event.type = WifiEventType::FAILED;
        event.reason = disconnectReason(failureReason_);
        ESP_LOGW(TAG, "connect_failed reason=%s", event.reason.c_str());
    }
    return event;
}

WifiConnectionInfo WifiManager::connectionInfo() const {
    WifiConnectionInfo info;
    info.ssid = selectedSsid_;
    esp_netif_ip_info_t ip{};
    if (stationNetif_ && esp_netif_get_ip_info(stationNetif_, &ip) == ESP_OK) {
        info.ipv4 = ip4ToString(ip.ip);
        info.gateway = ip4ToString(ip.gw);
        info.netmask = ip4ToString(ip.netmask);
        info.hasIpv4 = ip.ip.addr != 0;
    }
    esp_ip6_addr_t ip6{};
    if (stationNetif_ && esp_netif_get_ip6_linklocal(stationNetif_, &ip6) == ESP_OK &&
        esp_netif_ip6_get_addr_type(&ip6) == ESP_IP6_ADDR_IS_LINK_LOCAL) {
        info.ipv6LinkLocal = ip6ToString(ip6);
        info.hasIpv6LinkLocal = true;
    }
    esp_netif_dns_info_t dns{};
    if (stationNetif_ && esp_netif_get_dns_info(stationNetif_, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4) {
        info.dns = ip4ToString(dns.ip.u_addr.ip4);
    }
    wifi_ap_record_t ap{};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) info.rssi = ap.rssi;
    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        char buffer[18];
        snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        info.mac = buffer;
    }
    return info;
}

void WifiManager::logNetworkDiagnostics() const {
    const WifiConnectionInfo info = connectionInfo();
    ESP_LOGI(TAG, "network_diag ssid=\"%s\" ip=%s ipv6_ll=%s gw=%s netmask=%s dns=%s rssi=%d mac=%s",
             info.ssid.c_str(),
             info.ipv4.empty() ? "unknown" : info.ipv4.c_str(),
             info.ipv6LinkLocal.empty() ? "none" : info.ipv6LinkLocal.c_str(),
             info.gateway.empty() ? "unknown" : info.gateway.c_str(),
             info.netmask.empty() ? "unknown" : info.netmask.c_str(),
             info.dns.empty() ? "unknown" : info.dns.c_str(),
             info.rssi,
             info.mac.empty() ? "unknown" : info.mac.c_str());
}

std::string WifiManager::disconnectReason(uint8_t reason) const {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL: return "Authentification refusée";
        case WIFI_REASON_NO_AP_FOUND: return "Réseau introuvable";
        case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Délai d'authentification dépassé";
        case WIFI_REASON_BEACON_TIMEOUT: return "Point d'accès hors de portée";
        case WIFI_REASON_ASSOC_FAIL: return "Association refusée";
        default: return "Erreur Wi-Fi (code " + std::to_string(reason) + ")";
    }
}
