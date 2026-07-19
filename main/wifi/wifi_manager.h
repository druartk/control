#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types_generic.h"

struct WifiNetwork {
    std::string ssid;
    int8_t rssi = -127;
    wifi_auth_mode_t auth = WIFI_AUTH_OPEN;
    bool hidden = false;
};

enum class WifiEventType { NONE, SCAN_COMPLETE, CONNECTED, FAILED };

struct WifiStatusEvent {
    WifiEventType type = WifiEventType::NONE;
    std::string reason;
};

struct WifiConnectionInfo {
    std::string ssid;
    std::string ipv4;
    std::string ipv6LinkLocal;
    std::string gateway;
    std::string netmask;
    std::string dns;
    std::string mac;
    int8_t rssi = 0;
    bool hasIpv4 = false;
    bool hasIpv6LinkLocal = false;
};

class WifiManager {
public:
    bool initialize();
    bool startScan();
    bool connect(const std::string &ssid, const std::string &password);
    void cancel();
    void releaseTransientBuffers();
    bool isConnected() const;
    bool hasIpv6LinkLocal() const;
    esp_err_t waitMatterNetworkReady(uint32_t timeout_ms);
    WifiStatusEvent poll();
    const std::vector<WifiNetwork> &networks() const { return networks_; }
    WifiConnectionInfo connectionInfo() const;
    void logNetworkDiagnostics() const;
    const char *statusText() const { return status_.c_str(); }

private:
    static void eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data);
    void collectScanResults();
    std::string disconnectReason(uint8_t reason) const;

    esp_netif_t *stationNetif_ = nullptr;
    esp_event_handler_instance_t wifiHandler_ = nullptr;
    esp_event_handler_instance_t ipHandler_ = nullptr;
    std::atomic<int> pending_{0};
    std::atomic<uint8_t> failureReason_{0};
    std::vector<WifiNetwork> networks_;
    std::string selectedSsid_;
    std::string status_ = "Non initialisé";
    bool initialized_ = false;
    bool connecting_ = false;
    bool connected_ = false;
};
