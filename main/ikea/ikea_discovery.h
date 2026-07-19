#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi/wifi_manager.h"

enum class IkeaDiscoveryClassification {
    NONE,
    IKEA_FOUND_CONFIDENT,
    KNOWN_IP_REACHABLE_UNCONFIRMED,
    MATTER_DEVICE_FOUND_UNCONFIRMED,
    MATTER_BRIDGE_FOUND_UNCONFIRMED = MATTER_DEVICE_FOUND_UNCONFIRMED,
    LOCAL_MDNS_FOUND_UNCONFIRMED,
    NOT_FOUND,
    DISCOVERY_ERROR,
};

struct IkeaDiscoveryResult {
    IkeaDiscoveryClassification classification = IkeaDiscoveryClassification::NONE;
    std::string hostname;
    std::string ip;
    std::string serviceType;
    std::string protocol;
    std::string instanceName;
    uint16_t port = 0;
    std::string summary;
    size_t serviceCount = 0;
};

enum class IkeaDiscoveryEventType { NONE, COMPLETE };

struct IkeaDiscoveryEvent {
    IkeaDiscoveryEventType type = IkeaDiscoveryEventType::NONE;
    IkeaDiscoveryResult result;
};

class IkeaDiscovery {
public:
    bool initializeMdns(const WifiConnectionInfo &wifi);
    bool start(const WifiConnectionInfo &wifi);
    void cancel();
    IkeaDiscoveryEvent poll();
    const IkeaDiscoveryResult &result() const { return result_; }
    bool probeKnownIp(const std::string &ip, IkeaDiscoveryResult &out);

private:
    static void taskEntry(void *arg);
    void run();
    void logWifiDiagnostics() const;
    bool browseServices(IkeaDiscoveryResult &out, size_t &serviceCount, size_t &matterServiceCount);
    bool resolveHostname(const char *hostname, IkeaDiscoveryResult &out);
    void classifyCandidate(IkeaDiscoveryResult &candidate);

    TaskHandle_t task_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> complete_{false};
    std::atomic<bool> mdnsInitialized_{false};
    WifiConnectionInfo wifi_;
    IkeaDiscoveryResult result_;
};
