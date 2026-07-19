#include "ikea/ikea_discovery.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include "esp_log.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"
#include "sdkconfig.h"

#if CONFIG_CONTROL_IKEA_MDNS_DISCOVERY
#include "mdns.h"
#endif

namespace {
constexpr const char *TAG = "CONTROL_IKEA";
constexpr uint32_t DISCOVERY_TASK_STACK = 6144;
constexpr UBaseType_t DISCOVERY_TASK_PRIORITY = 4;
constexpr uint32_t MDNS_BROWSE_TIMEOUT_MS = 3500;
constexpr size_t MDNS_MAX_RESULTS = 8;
constexpr uint32_t KNOWN_IP_PING_TIMEOUT_MS = 1000;
constexpr uint32_t KNOWN_IP_PING_WAIT_MS = 1600;

#ifndef CONFIG_CONTROL_IKEA_KNOWN_IP
#define CONFIG_CONTROL_IKEA_KNOWN_IP ""
#endif

const std::array<const char *, 6> HOSTNAMES{{
    "dirigera.local",
    "hubv3.local",
    "ikea-dirigera.local",
    "dirigera",
    "hubv3",
    "ikea-dirigera",
}};

struct ServiceQuery {
    const char *type;
    const char *proto;
};

const std::array<ServiceQuery, 6> SERVICE_TYPES{{
    {"_matter", "_tcp"},
    {"_matterc", "_udp"},
    {"_matterd", "_udp"},
    {"_http", "_tcp"},
    {"_homekit", "_tcp"},
    {"_hap", "_tcp"},
}};

std::string lowerCopy(const std::string &value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower;
}

bool containsIkeaIdentity(const std::string &value) {
    const std::string lower = lowerCopy(value);
    return lower.find("ikea") != std::string::npos ||
           lower.find("dirigera") != std::string::npos ||
           lower.find("hubv3") != std::string::npos;
}

bool isMatterService(const std::string &serviceType) {
    return serviceType == "_matter._tcp" ||
           serviceType == "_matterc._udp" ||
           serviceType == "_matterd._udp";
}

std::string serviceName(const ServiceQuery &query) {
    return std::string(query.type) + "." + query.proto;
}

std::string ip4ToString(const esp_ip4_addr_t &addr) {
    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &addr, ip, sizeof(ip));
    return ip;
}

#if CONFIG_CONTROL_IKEA_MDNS_DISCOVERY
std::string firstIpv4(mdns_result_t *result) {
    for (mdns_ip_addr_t *addr = result ? result->addr : nullptr; addr != nullptr; addr = addr->next) {
        if (addr->addr.type == ESP_IPADDR_TYPE_V4) {
            return ip4ToString(addr->addr.u_addr.ip4);
        }
    }
    return {};
}
#endif

const char *classificationName(IkeaDiscoveryClassification classification) {
    switch (classification) {
        case IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT: return "IKEA_FOUND_CONFIDENT";
        case IkeaDiscoveryClassification::KNOWN_IP_REACHABLE_UNCONFIRMED: return "KNOWN_IP_REACHABLE_UNCONFIRMED";
        case IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED: return "MATTER_DEVICE_FOUND_UNCONFIRMED";
        case IkeaDiscoveryClassification::LOCAL_MDNS_FOUND_UNCONFIRMED: return "LOCAL_MDNS_FOUND_UNCONFIRMED";
        case IkeaDiscoveryClassification::NOT_FOUND: return "NOT_FOUND";
        case IkeaDiscoveryClassification::DISCOVERY_ERROR: return "DISCOVERY_ERROR";
        default: return "NONE";
    }
}

struct PingProbeContext {
    SemaphoreHandle_t done = nullptr;
    bool reachable = false;
};

void onPingSuccess(esp_ping_handle_t, void *args) {
    auto *context = static_cast<PingProbeContext *>(args);
    if (context) context->reachable = true;
}

void onPingEnd(esp_ping_handle_t, void *args) {
    auto *context = static_cast<PingProbeContext *>(args);
    if (context && context->done) xSemaphoreGive(context->done);
}
}

bool IkeaDiscovery::initializeMdns(const WifiConnectionInfo &wifi) {
#if CONFIG_CONTROL_IKEA_MDNS_DISCOVERY
    if (!wifi.hasIpv4) {
        ESP_LOGE(TAG, "mdns_init failed err=no_ipv4");
        return false;
    }
    if (mdnsInitialized_.load()) return true;
    ESP_LOGI(TAG, "mdns_init start");
    esp_err_t err = mdns_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "mdns_init failed err=%s", esp_err_to_name(err));
        return false;
    }
    err = mdns_hostname_set("control-t5");
    if (err == ESP_OK) err = mdns_instance_name_set("Control T5");
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed err=%s", esp_err_to_name(err));
        return false;
    }
    mdnsInitialized_ = true;
    ESP_LOGI(TAG, "mdns_init ok hostname=control-t5");
    return true;
#else
    ESP_LOGW(TAG, "mdns_init skipped reason=CONFIG_CONTROL_IKEA_MDNS_DISCOVERY_disabled");
    (void)wifi;
    return false;
#endif
}

bool IkeaDiscovery::start(const WifiConnectionInfo &wifi) {
#if CONFIG_CONTROL_IKEA_DISCOVERY
    if (running_.load()) return true;
    cancelRequested_ = false;
    complete_ = false;
    wifi_ = wifi;
    result_ = {};
    ESP_LOGI(TAG, "discovery_start");
    if (!wifi_.hasIpv4) {
        result_.classification = IkeaDiscoveryClassification::DISCOVERY_ERROR;
        result_.summary = "Wi-Fi connecté sans adresse IPv4 valide.";
        complete_ = true;
        ESP_LOGE(TAG, "result=DISCOVERY_ERROR err=no_ipv4");
        return false;
    }
    const BaseType_t ok = xTaskCreate(
        &IkeaDiscovery::taskEntry,
        "control_ikea_discovery",
        DISCOVERY_TASK_STACK,
        this,
        DISCOVERY_TASK_PRIORITY,
        &task_);
    if (ok != pdPASS) {
        result_.classification = IkeaDiscoveryClassification::DISCOVERY_ERROR;
        result_.summary = "Impossible de démarrer la tâche de découverte.";
        complete_ = true;
        task_ = nullptr;
        ESP_LOGE(TAG, "result=DISCOVERY_ERROR err=task_create_failed");
        return false;
    }
    running_ = true;
    return true;
#else
    (void)wifi;
    result_.classification = IkeaDiscoveryClassification::DISCOVERY_ERROR;
    result_.summary = "Découverte IKEA désactivée dans la configuration.";
    complete_ = true;
    ESP_LOGE(TAG, "result=DISCOVERY_ERROR err=disabled");
    return false;
#endif
}

void IkeaDiscovery::cancel() {
    cancelRequested_ = true;
}

IkeaDiscoveryEvent IkeaDiscovery::poll() {
    IkeaDiscoveryEvent event;
    if (complete_.exchange(false)) {
        event.type = IkeaDiscoveryEventType::COMPLETE;
        event.result = result_;
    }
    return event;
}

bool IkeaDiscovery::probeKnownIp(const std::string &ip, IkeaDiscoveryResult &out) {
    out = {};
    out.ip = ip;
    out.serviceType = "known_ip_ping";
    out.summary = "IP connue testée par ping; identité IKEA non confirmée.";

    if (ip.empty()) return false;

    ip_addr_t target{};
    if (!ipaddr_aton(ip.c_str(), &target)) {
        ESP_LOGW(TAG, "known_ip_probe ip=%s reachable=no err=invalid_ip", ip.c_str());
        return false;
    }

    PingProbeContext context;
    context.done = xSemaphoreCreateBinary();
    if (context.done == nullptr) {
        ESP_LOGE(TAG, "known_ip_probe ip=%s reachable=no err=no_semaphore", ip.c_str());
        return false;
    }

    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.target_addr = target;
    config.count = 1;
    config.interval_ms = 100;
    config.timeout_ms = KNOWN_IP_PING_TIMEOUT_MS;
    config.data_size = 32;
    config.task_stack_size = 3072;

    esp_ping_callbacks_t callbacks{};
    callbacks.cb_args = &context;
    callbacks.on_ping_success = onPingSuccess;
    callbacks.on_ping_end = onPingEnd;

    esp_ping_handle_t handle = nullptr;
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &handle);
    if (err == ESP_OK) err = esp_ping_start(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "known_ip_probe ip=%s reachable=no err=%s", ip.c_str(), esp_err_to_name(err));
        if (handle) esp_ping_delete_session(handle);
        vSemaphoreDelete(context.done);
        return false;
    }

    if (xSemaphoreTake(context.done, pdMS_TO_TICKS(KNOWN_IP_PING_WAIT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "known_ip_probe ip=%s reachable=no err=timeout", ip.c_str());
        esp_ping_stop(handle);
        esp_ping_delete_session(handle);
        vSemaphoreDelete(context.done);
        return false;
    }

    esp_ping_stop(handle);
    esp_ping_delete_session(handle);
    vSemaphoreDelete(context.done);

    ESP_LOGI(TAG, "known_ip_probe ip=%s reachable=%s", ip.c_str(), context.reachable ? "yes" : "no");
    if (!context.reachable) return false;

    out.classification = IkeaDiscoveryClassification::KNOWN_IP_REACHABLE_UNCONFIRMED;
    out.hostname = ip;
    out.instanceName = "known-ip";
    out.summary = "IP connue joignable; identité IKEA non confirmée; aucun service Matter annoncé.";
    return true;
}

void IkeaDiscovery::taskEntry(void *arg) {
    auto *self = static_cast<IkeaDiscovery *>(arg);
    self->run();
    self->running_ = false;
    self->task_ = nullptr;
    self->complete_ = true;
    vTaskDelete(nullptr);
}

void IkeaDiscovery::logWifiDiagnostics() const {
#if CONFIG_CONTROL_IKEA_IP_DIAGNOSTICS
    ESP_LOGI(TAG, "wifi_diag ip=%s gw=%s netmask=%s dns=%s rssi=%d ssid=\"%s\" mac=%s",
             wifi_.ipv4.empty() ? "unknown" : wifi_.ipv4.c_str(),
             wifi_.gateway.empty() ? "unknown" : wifi_.gateway.c_str(),
             wifi_.netmask.empty() ? "unknown" : wifi_.netmask.c_str(),
             wifi_.dns.empty() ? "unknown" : wifi_.dns.c_str(),
             wifi_.rssi,
             wifi_.ssid.c_str(),
             wifi_.mac.empty() ? "unknown" : wifi_.mac.c_str());
#endif
}

void IkeaDiscovery::run() {
    logWifiDiagnostics();

    IkeaDiscoveryResult knownIpResult;
    bool knownIpReachable = false;
    const char *knownIp = CONFIG_CONTROL_IKEA_KNOWN_IP;
    if (knownIp != nullptr && knownIp[0] != '\0') {
        knownIpReachable = probeKnownIp(knownIp, knownIpResult);
    }

#if CONFIG_CONTROL_IKEA_MDNS_DISCOVERY
    if (!initializeMdns(wifi_)) {
        result_.classification = IkeaDiscoveryClassification::DISCOVERY_ERROR;
        result_.summary = "Initialisation mDNS impossible.";
        ESP_LOGE(TAG, "result=DISCOVERY_ERROR err=mdns_init_failed");
        return;
    }
#endif

    size_t serviceCount = 0;
    size_t matterServiceCount = 0;
    IkeaDiscoveryResult candidate;
    if (browseServices(candidate, serviceCount, matterServiceCount)) {
        result_ = candidate;
        result_.serviceCount = serviceCount;
        ESP_LOGI(TAG, "result=%s host=%s ip=%s service=%s",
                 classificationName(result_.classification), result_.hostname.c_str(),
                 result_.ip.c_str(), result_.serviceType.c_str());
        return;
    }
    ESP_LOGI(TAG, "mdns_matter_services_found count=%u", static_cast<unsigned>(matterServiceCount));

#if CONFIG_CONTROL_IKEA_HOSTNAME_FALLBACK
    for (const char *hostname : HOSTNAMES) {
        if (cancelRequested_.load()) {
            result_.classification = IkeaDiscoveryClassification::DISCOVERY_ERROR;
            result_.summary = "Découverte annulée.";
            ESP_LOGW(TAG, "result=DISCOVERY_ERROR err=cancelled");
            return;
        }
        IkeaDiscoveryResult resolved;
        if (resolveHostname(hostname, resolved)) {
            classifyCandidate(resolved);
            result_ = resolved;
            result_.serviceCount = serviceCount;
            ESP_LOGI(TAG, "result=%s host=%s ip=%s",
                     classificationName(result_.classification), result_.hostname.c_str(),
                     result_.ip.c_str());
            return;
        }
    }
#endif

    if (knownIpReachable) {
        result_ = knownIpResult;
        result_.serviceCount = serviceCount;
        ESP_LOGI(TAG, "result=KNOWN_IP_REACHABLE_UNCONFIRMED ip=%s", result_.ip.c_str());
        ESP_LOGI(TAG, "next_step=enter_matter_pairing_code_from_ikea_app");
        return;
    }

    if (serviceCount > 0) {
        result_.classification = IkeaDiscoveryClassification::LOCAL_MDNS_FOUND_UNCONFIRMED;
        result_.serviceCount = serviceCount;
        result_.summary = "Services mDNS locaux détectés, mais aucun identifiant IKEA/DIRIGERA ou Matter clair.";
        ESP_LOGI(TAG, "result=LOCAL_MDNS_FOUND_UNCONFIRMED count=%u", static_cast<unsigned>(serviceCount));
        return;
    }

    result_.classification = IkeaDiscoveryClassification::NOT_FOUND;
    result_.summary = "Aucune centrale IKEA/DIRIGERA détectée depuis l'ESP32.";
    ESP_LOGI(TAG, "result=NOT_FOUND");
}

bool IkeaDiscovery::browseServices(IkeaDiscoveryResult &out, size_t &serviceCount, size_t &matterServiceCount) {
#if CONFIG_CONTROL_IKEA_MDNS_DISCOVERY
    for (const auto &query : SERVICE_TYPES) {
        if (cancelRequested_.load()) return false;
        const std::string fullType = serviceName(query);
        ESP_LOGI(TAG, "mdns_browse type=%s", fullType.c_str());
        mdns_result_t *results = nullptr;
        const esp_err_t err = mdns_query_ptr(query.type, query.proto, MDNS_BROWSE_TIMEOUT_MS,
                                             MDNS_MAX_RESULTS, &results);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mdns_browse_failed type=%s err=%s", fullType.c_str(), esp_err_to_name(err));
            continue;
        }
        for (mdns_result_t *item = results; item != nullptr; item = item->next) {
            ++serviceCount;
            IkeaDiscoveryResult candidate;
            candidate.serviceType = fullType;
            if (isMatterService(candidate.serviceType)) ++matterServiceCount;
            candidate.protocol = query.proto;
            candidate.instanceName = item->instance_name ? item->instance_name : "";
            candidate.hostname = item->hostname ? item->hostname : "";
            candidate.port = item->port;
            candidate.ip = firstIpv4(item);
            ESP_LOGI(TAG, "service_found type=%s instance=\"%s\" host=\"%s\" ip=%s port=%u",
                     candidate.serviceType.c_str(), candidate.instanceName.c_str(),
                     candidate.hostname.c_str(), candidate.ip.empty() ? "unknown" : candidate.ip.c_str(),
                     candidate.port);
            bool identityFromTxt = false;
            for (size_t i = 0; i < item->txt_count; ++i) {
                const char *key = item->txt[i].key ? item->txt[i].key : "";
                const char *value = item->txt[i].value ? item->txt[i].value : "";
                ESP_LOGI(TAG, "txt key=\"%s\" value=\"%s\"", key, value);
                identityFromTxt = identityFromTxt || containsIkeaIdentity(key) || containsIkeaIdentity(value);
            }
            classifyCandidate(candidate);
            if (identityFromTxt) {
                candidate.classification = IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT;
                candidate.summary = "Identité IKEA/DIRIGERA détectée dans les TXT mDNS.";
            }
            if (candidate.classification == IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT ||
                candidate.classification == IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED) {
                out = candidate;
                mdns_query_results_free(results);
                return true;
            }
            if (out.classification == IkeaDiscoveryClassification::NONE) out = candidate;
        }
        if (results) mdns_query_results_free(results);
    }
    return false;
#else
    for (const auto &query : SERVICE_TYPES) {
        ESP_LOGI(TAG, "service_browse_unavailable type=%s reason=mdns_disabled", serviceName(query).c_str());
    }
    (void)out;
    (void)serviceCount;
    (void)matterServiceCount;
    return false;
#endif
}

bool IkeaDiscovery::resolveHostname(const char *hostname, IkeaDiscoveryResult &out) {
    ESP_LOGI(TAG, "hostname_query host=%s", hostname);
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    const int rc = getaddrinfo(hostname, nullptr, &hints, &res);
    if (rc != 0 || res == nullptr) {
        ESP_LOGI(TAG, "hostname_not_found host=%s rc=%d", hostname, rc);
        if (res) freeaddrinfo(res);
        return false;
    }

    char ip[INET_ADDRSTRLEN]{};
    const auto *addr = reinterpret_cast<sockaddr_in *>(res->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    out.hostname = hostname;
    out.ip = ip;
    out.serviceType = "hostname";
    out.instanceName = hostname;
    out.port = 0;
    out.summary = "Hôte local résolu.";
    ESP_LOGI(TAG, "hostname_found host=%s ip=%s", hostname, ip);
    freeaddrinfo(res);
    return true;
}

void IkeaDiscovery::classifyCandidate(IkeaDiscoveryResult &candidate) {
    const bool ikea = containsIkeaIdentity(candidate.hostname) ||
                      containsIkeaIdentity(candidate.instanceName) ||
                      containsIkeaIdentity(candidate.serviceType);
    if (ikea) {
        candidate.classification = IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT;
        candidate.summary = "Identité IKEA/DIRIGERA détectée sur le réseau local.";
    } else if (isMatterService(candidate.serviceType)) {
        candidate.classification = IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED;
        candidate.summary = "Service Matter détecté, identité IKEA non confirmée.";
    } else {
        candidate.classification = IkeaDiscoveryClassification::LOCAL_MDNS_FOUND_UNCONFIRMED;
        candidate.summary = "Service local détecté, identité IKEA non confirmée.";
    }
}
