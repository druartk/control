#include "matter/matter_controller.h"

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "system/control_heap.h"
#include "system/control_time.h"

#include <cstdio>

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
#include <algorithm>
#include <optional>
#include <vector>
#include <cstring>
#include <lwip/ip_addr.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <app/data-model/Decode.h>
#include <clusters/BasicInformation/AttributeIds.h>
#include <clusters/BasicInformation/ClusterId.h>
#include <clusters/BridgedDeviceBasicInformation/AttributeIds.h>
#include <clusters/BridgedDeviceBasicInformation/ClusterId.h>
#include <clusters/ColorControl/ClusterId.h>
#include <clusters/Descriptor/Attributes.h>
#include <clusters/LevelControl/ClusterId.h>
#include <clusters/OnOff/Attributes.h>
#include <clusters/OnOff/ClusterId.h>
#include <clusters/OnOff/CommandIds.h>
#include <clusters/Switch/ClusterId.h>
#include <esp_matter.h>
#include <esp_matter_attestation_trust_store.h>
#include <esp_matter_controller_client.h>
#include <esp_matter_controller_cluster_command.h>
#include <esp_matter_controller_pairing_command.h>
#include <esp_matter_controller_read_command.h>
#include <controller/CommissioningDelegate.h>
#include <credentials/attestation_verifier/DefaultDeviceAttestationVerifier.h>
#include <credentials/attestation_verifier/DeviceAttestationVerifier.h>
#include <credentials/FabricTable.h>
#include <lib/core/CHIPError.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/CHIPDeviceEvent.h>
#include <setup_payload/SetupPayload.h>
#endif

namespace {
constexpr const char *TAG = "CONTROL_MATTER";
constexpr const char *MATTER_APP_NVS_NAMESPACE = "control_matter";
constexpr const char *MATTER_DEVICE_CACHE_NVS_NAMESPACE = "control_matter_devices";
constexpr const char *KEY_COMMISSIONED = "commissioned";
constexpr const char *KEY_NODE_ID = "node_id";
constexpr const char *KEY_FABRIC_INDEX = "fabric_idx";
constexpr const char *KEY_CONTROLLER_NODE_ID = "ctrl_node";
constexpr const char *KEY_PREFERRED_REMOTE_NODE_ID = "pref_node";
constexpr const char *KEY_COMMISSIONING_MODE = "comm_mode";
constexpr const char *KEY_LAST_KNOWN_ADDR = "last_addr";
constexpr const char *KEY_LAST_KNOWN_HOST = "last_host";
constexpr const char *KEY_ATT_VERIFIED = "att_verified";
constexpr const char *KEY_ATT_BYPASSED = "att_bypassed";
constexpr const char *KEY_DEV_INSECURE = "dev_insecure";
constexpr const char *KEY_COMMISSIONED_AT = "commissioned_at";
constexpr const char *KEY_DEVICE_CACHE_BLOB = "devices";
constexpr size_t MIN_CONTROLLER_LARGEST_INTERNAL_HEAP = 40 * 1024;
constexpr uint32_t DEVICE_CACHE_MAGIC = 0x434D4431; // "CMD1"
constexpr uint16_t DEVICE_CACHE_VERSION = 1;
constexpr size_t DEVICE_CACHE_MAX_ITEMS = 32;
constexpr size_t DEVICE_CACHE_NAME_LEN = 48;
constexpr uint8_t COMMISSIONING_MODE_FIRST_PAIRING = 1;
constexpr uint8_t COMMISSIONING_MODE_MULTI_ADMIN = 2;

MatterCommissioningResult g_lastResult{};
MatterCommissioningStatus g_status = MatterCommissioningStatus::IDLE;
MatterSavedNode g_savedNode{};
bool g_savedNodeLoaded = false;
MatterSavedState g_savedState = MatterSavedState::None;
std::string g_savedStateReason;
MatterPostCommissioningResult g_postResult{};
constexpr uint64_t CONTROL_CONTROLLER_NODE_ID = 112233ULL; // 0x1B669, controller operational node id.
constexpr uint64_t CONTROL_PRIMARY_REMOTE_NODE_ID = 0x12344321ULL;
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
MatterControllerState g_controllerState = MatterControllerState::NotStarted;
bool g_platformStarted = false;
bool g_controllerClientInitialized = false;
bool g_commissionerSetupDone = false;
bool g_controllerInitialized = false;
bool g_controllerInitInProgress = false;
bool g_controllerInitFailed = false;
esp_err_t g_controllerInitLastError = ESP_OK;
bool g_matterStartInProgress = false;
uint64_t g_pendingPreferredRemoteNodeId = 0;
uint8_t g_pendingCommissioningMode = COMMISSIONING_MODE_FIRST_PAIRING;
bool g_previousSendNocFabricConflict = false;
std::string g_lastMdnsPrescanAddr;
std::string g_lastMdnsPrescanHost;
constexpr uint64_t CONTROL_CONTROLLER_FABRIC_ID = 1ULL;
constexpr uint16_t CONTROL_CONTROLLER_LISTEN_PORT = 5580;
constexpr uint32_t DISCOVERY_WORKER_STACK_SIZE = 16384;
constexpr uint32_t COMMAND_WORKER_STACK_SIZE = 12288;
constexpr UBaseType_t MATTER_DISCOVERY_WORKER_PRIORITY = 7;
constexpr UBaseType_t MATTER_COMMAND_WORKER_PRIORITY = 7;
constexpr uint64_t CONTROL_FABRIC_CONFLICT_REMOTE_NODE_ID = 0x12344322ULL;
constexpr uint32_t CHIP_ERROR_NOC_RESPONSE_STATUS = 0x7E;
constexpr const char *MATTER_KVS_NVS_NAMESPACE = "CHIP_KVS";
constexpr const char *MATTER_CONFIG_NVS_NAMESPACE = "chip-config";
constexpr const char *MATTER_COUNTERS_NVS_NAMESPACE = "chip-counters";
constexpr const char *MATTER_OPCERT_NVS_NAMESPACE = "chip-opcert";
#endif
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
bool g_attestationBypassObserved = false;
TaskHandle_t g_discoveryTask = nullptr;
TaskHandle_t g_commandTask = nullptr;

struct ReadWaitContext {
    SemaphoreHandle_t done = nullptr;
    CHIP_ERROR chip_error = CHIP_ERROR_TIMEOUT;
    bool got_value = false;
    uint64_t node_id = 0;
    uint16_t endpoint_id = 0;
    uint32_t cluster_id = 0;
    uint32_t attribute_id = 0;
    std::vector<uint16_t> endpoints;
    std::vector<uint32_t> cluster_ids;
    std::vector<uint32_t> device_types;
    bool bool_value = false;
    std::string string_value;
};

struct DeviceCacheHeader {
    uint32_t magic = DEVICE_CACHE_MAGIC;
    uint16_t version = DEVICE_CACHE_VERSION;
    uint16_t count = 0;
};

struct DeviceCacheRecord {
    uint16_t endpoint_id = 0;
    uint16_t flags = 0;
    uint32_t device_type = 0;
    uint32_t primary_cluster_class = 0;
    uint64_t last_seen = 0;
    char display_name[DEVICE_CACHE_NAME_LEN] = {};
    char type_label[24] = {};
};

enum DeviceCacheFlags : uint16_t {
    CACHE_HAS_ONOFF = 1 << 0,
    CACHE_HAS_LEVEL = 1 << 1,
    CACHE_HAS_COLOR = 1 << 2,
    CACHE_HAS_SWITCH = 1 << 3,
    CACHE_REACHABLE_KNOWN = 1 << 4,
    CACHE_REACHABLE = 1 << 5,
    CACHE_ONOFF_KNOWN = 1 << 6,
    CACHE_ONOFF_STATE = 1 << 7,
};

struct CommandWaitContext {
    SemaphoreHandle_t done = nullptr;
    CHIP_ERROR chip_error = CHIP_ERROR_TIMEOUT;
    bool got_result = false;
    uint16_t endpoint_id = 0;
};

ReadWaitContext *g_activeRead = nullptr;
CommandWaitContext *g_activeCommand = nullptr;
#endif

[[maybe_unused]] const char *safeText(const std::string &value) {
    return value.empty() ? "-" : value.c_str();
}

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
const char *controllerStateName(MatterControllerState state) {
    switch (state) {
        case MatterControllerState::NotStarted: return "NotStarted";
        case MatterControllerState::PlatformStarted: return "PlatformStarted";
        case MatterControllerState::ControllerReady: return "ControllerReady";
        case MatterControllerState::FailedInvalidMetadata: return "FailedInvalidMetadata";
        case MatterControllerState::FailedStorage: return "FailedStorage";
        case MatterControllerState::FailedLowHeap: return "FailedLowHeap";
        case MatterControllerState::FailedUnknown: return "FailedUnknown";
    }
    return "Unknown";
}

void setControllerState(MatterControllerState state) {
    if (g_controllerState == state) return;
    ESP_LOGI(TAG, "controller_state %s -> %s",
             controllerStateName(g_controllerState), controllerStateName(state));
    g_controllerState = state;
}
#endif

void logInitLifecycle(const char *phase) {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    ESP_LOGI(TAG, "lifecycle %s platform_started=%s controller_initialized=%s commissioner_ready=%s init_in_progress=%s init_failed=%s",
             phase ? phase : "-",
             g_platformStarted ? "yes" : "no",
             g_controllerClientInitialized ? "yes" : "no",
             g_commissionerSetupDone ? "yes" : "no",
             g_controllerInitInProgress ? "yes" : "no",
             g_controllerInitFailed ? "yes" : "no");
#else
    ESP_LOGI(TAG, "lifecycle %s platform_started=no controller_initialized=no commissioner_ready=no init_in_progress=no init_failed=no",
             phase ? phase : "-");
#endif
}

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
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

bool wifiStaConnected() {
    wifi_ap_record_t ap{};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

esp_err_t waitMatterNetworkPreflight(uint32_t timeoutMs) {
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_err_t lastErr = sta ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
    bool ip6CreateRequested = false;
    const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(timeoutMs) * 1000;
    while (esp_timer_get_time() < deadline) {
        esp_netif_ip_info_t ip{};
        const bool hasIpv4 = sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0;
        const bool connected = hasIpv4 && wifiStaConnected();
        if (sta && hasIpv4 && !ip6CreateRequested) {
            esp_ip6_addr_t tmp{};
            if (esp_netif_get_ip6_linklocal(sta, &tmp) != ESP_OK) {
                const esp_err_t ip6Err = esp_netif_create_ip6_linklocal(sta);
                ESP_LOGI(TAG, "network_preflight_ipv6_linklocal_create result=%s", esp_err_to_name(ip6Err));
            }
            ip6CreateRequested = true;
        }

        esp_ip6_addr_t ip6{};
        const bool hasIpv6LinkLocal =
            sta && esp_netif_get_ip6_linklocal(sta, &ip6) == ESP_OK &&
            esp_netif_ip6_get_addr_type(&ip6) == ESP_IP6_ADDR_IS_LINK_LOCAL;

        if (connected && hasIpv4 && hasIpv6LinkLocal) {
            ESP_LOGI(TAG, "network_ready_before_start ipv4=%s ipv6_ll=%s wifi_connected=yes",
                     ip4ToString(ip.ip).c_str(), ip6ToString(ip6).c_str());
            return ESP_OK;
        }
        lastErr = connected && hasIpv4 ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    esp_netif_ip_info_t ip{};
    esp_ip6_addr_t ip6{};
    const bool hasIpv4 = sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0;
    const bool hasIpv6LinkLocal =
        sta && esp_netif_get_ip6_linklocal(sta, &ip6) == ESP_OK &&
        esp_netif_ip6_get_addr_type(&ip6) == ESP_IP6_ADDR_IS_LINK_LOCAL;
    ESP_LOGW(TAG, "network_ready_before_start ipv4=%s ipv6_ll=%s wifi_connected=%s result=%s",
             hasIpv4 ? ip4ToString(ip.ip).c_str() : "none",
             hasIpv6LinkLocal ? ip6ToString(ip6).c_str() : "none",
             wifiStaConnected() ? "yes" : "no",
             esp_err_to_name(lastErr));
    return lastErr;
}

void logLwipMatterConfig() {
    ESP_LOGI(TAG, "lwip_config max_sockets=%d max_udp_pcbs=%d udp_recvmbox=%d tcpip_recvmbox=%d tcp_recvmbox=%d netbuf_recvinfo=%s ipv6=%s ipv6_num_addresses=%d",
             CONFIG_LWIP_MAX_SOCKETS,
             CONFIG_LWIP_MAX_UDP_PCBS,
             CONFIG_LWIP_UDP_RECVMBOX_SIZE,
             CONFIG_LWIP_TCPIP_RECVMBOX_SIZE,
             CONFIG_LWIP_TCP_RECVMBOX_SIZE,
#if CONFIG_LWIP_NETBUF_RECVINFO
             "yes",
#else
             "no",
#endif
#if CONFIG_LWIP_IPV6
             "yes",
#else
             "no",
#endif
             CONFIG_LWIP_IPV6_NUM_ADDRESSES);
}

bool internalHeapLooksLow() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 64 * 1024 ||
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < MIN_CONTROLLER_LARGEST_INTERNAL_HEAP;
}
#endif

bool isValidRemoteNodeId(uint64_t nodeId) {
    return nodeId != 0 && nodeId != CONTROL_CONTROLLER_NODE_ID;
}

void formatMatterId(uint64_t id, char *buf, size_t size) {
    if (!buf || size == 0) return;
    snprintf(buf, size, "0x%08lx%08lx",
             static_cast<unsigned long>(static_cast<uint32_t>(id >> 32)),
             static_cast<unsigned long>(static_cast<uint32_t>(id & 0xffffffffULL)));
}

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
bool controllerLargestInternalHeapReady() {
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) >= MIN_CONTROLLER_LARGEST_INTERNAL_HEAP;
}

esp_err_t ensureChipUniqueIdProvisioned() {
    using chip::DeviceLayer::Internal::ESP32Config;

    char existing[64] = {0};
    size_t existingLen = sizeof(existing);
    CHIP_ERROR readErr = ESP32Config::ReadConfigValueStr(
        ESP32Config::kConfigKey_UniqueId, existing, sizeof(existing), existingLen);
    if (readErr == CHIP_NO_ERROR && existing[0] != '\0') {
        ESP_LOGI(TAG, "chip_unique_id existing len=%u", static_cast<unsigned>(strlen(existing)));
        return ESP_OK;
    }
    if (readErr != CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "chip_unique_id missing_or_unreadable chip_error=0x%lx",
                 static_cast<unsigned long>(readErr.AsInteger()));
    }

    uint8_t mac[6] = {0};
    const esp_err_t macErr = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (macErr != ESP_OK) {
        ESP_LOGE(TAG, "chip_unique_id mac_read_failed err=%s", esp_err_to_name(macErr));
        return macErr;
    }

    char uniqueId[33] = {0};
    snprintf(uniqueId, sizeof(uniqueId),
             "CTRL%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const CHIP_ERROR writeErr = ESP32Config::WriteConfigValueStr(
        ESP32Config::kConfigKey_UniqueId, uniqueId);
    if (writeErr != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "chip_unique_id write_failed chip_error=0x%lx",
                 static_cast<unsigned long>(writeErr.AsInteger()));
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "chip_unique_id provisioned len=%u source=wifi_sta_mac",
             static_cast<unsigned>(strlen(uniqueId)));
    return ESP_OK;
}

bool logHeapOnInitFailure() {
    ESP_LOGE(TAG, "heap_on_init_failure internal=%u largest_internal=%u psram=%u largest_psram=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    if (internalHeapLooksLow()) {
        ESP_LOGE(TAG, "init_failed_low_heap");
        return true;
    }
    ESP_LOGE(TAG, "init_failed_heap_not_low");
    return false;
}

void logCurrentTaskDiagnostics(const char *task, const char *phase) {
    ESP_LOGI(TAG, "task_diag phase=%s task=%s priority=%u stack_high_water=%u",
             phase ? phase : "-",
             task ? task : "-",
             static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
#endif

uint32_t partitionSizeByLabel(const char *label, esp_partition_type_t type) {
    const esp_partition_t *partition = esp_partition_find_first(type, ESP_PARTITION_SUBTYPE_ANY, label);
    return partition ? partition->size : 0;
}

bool boolFromU8(uint8_t value) {
    return value != 0;
}

std::string nvsGetString(nvs_handle_t handle, const char *key, bool *present = nullptr) {
    if (present) *present = false;
    size_t length = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &length);
    if (err != ESP_OK || length == 0) return {};
    std::vector<char> value(length);
    err = nvs_get_str(handle, key, value.data(), &length);
    if (err != ESP_OK) return {};
    if (present) *present = true;
    return value.data();
}

struct RawSavedNodeRead {
    MatterSavedNode node;
    bool namespace_exists = false;
    bool commissioned_key_present = false;
    bool node_id_key_present = false;
    bool fabric_index_key_present = false;
    bool any_key_present = false;
    esp_err_t error = ESP_OK;
};

RawSavedNodeRead readSavedNodeRaw() {
    RawSavedNodeRead raw;
    raw.node = {};
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MATTER_APP_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        raw.error = ESP_OK;
        return raw;
    }
    if (err != ESP_OK) {
        raw.error = err;
        return raw;
    }

    raw.namespace_exists = true;
    uint8_t commissioned = 0;
    uint8_t attestationVerified = 0;
    uint8_t attestationBypassed = 0;
    uint8_t devInsecure = 0;
    err = nvs_get_u8(handle, KEY_COMMISSIONED, &commissioned);
    if (err == ESP_OK) {
        raw.commissioned_key_present = true;
        raw.any_key_present = true;
        raw.node.commissioned = boolFromU8(commissioned);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        raw.error = err;
    }

    err = nvs_get_u64(handle, KEY_NODE_ID, &raw.node.node_id);
    if (err == ESP_OK) {
        raw.node_id_key_present = true;
        raw.any_key_present = true;
    } else if (err != ESP_ERR_NVS_NOT_FOUND && raw.error == ESP_OK) {
        raw.error = err;
    }

    err = nvs_get_u16(handle, KEY_FABRIC_INDEX, &raw.node.fabric_index);
    if (err == ESP_OK) {
        raw.fabric_index_key_present = true;
        raw.any_key_present = true;
    } else if (err != ESP_ERR_NVS_NOT_FOUND && raw.error == ESP_OK) {
        raw.error = err;
    }

    if (nvs_get_u8(handle, KEY_ATT_VERIFIED, &attestationVerified) == ESP_OK) raw.any_key_present = true;
    if (nvs_get_u8(handle, KEY_ATT_BYPASSED, &attestationBypassed) == ESP_OK) raw.any_key_present = true;
    if (nvs_get_u8(handle, KEY_DEV_INSECURE, &devInsecure) == ESP_OK) raw.any_key_present = true;
    if (nvs_get_u64(handle, KEY_CONTROLLER_NODE_ID, &raw.node.controller_node_id) == ESP_OK) raw.any_key_present = true;
    if (nvs_get_u64(handle, KEY_PREFERRED_REMOTE_NODE_ID, &raw.node.preferred_remote_node_id) == ESP_OK) raw.any_key_present = true;
    if (nvs_get_u8(handle, KEY_COMMISSIONING_MODE, &raw.node.commissioning_mode) == ESP_OK) raw.any_key_present = true;
    bool strPresent = false;
    raw.node.last_known_addr = nvsGetString(handle, KEY_LAST_KNOWN_ADDR, &strPresent);
    if (strPresent) raw.any_key_present = true;
    raw.node.last_known_host = nvsGetString(handle, KEY_LAST_KNOWN_HOST, &strPresent);
    if (strPresent) raw.any_key_present = true;
    if (nvs_get_u64(handle, KEY_COMMISSIONED_AT, &raw.node.commissioned_at_unix_or_boot_counter) == ESP_OK) {
        raw.any_key_present = true;
    }
    nvs_close(handle);

    if (raw.node.controller_node_id == 0) raw.node.controller_node_id = CONTROL_CONTROLLER_NODE_ID;
    if (raw.node.preferred_remote_node_id == 0) raw.node.preferred_remote_node_id = raw.node.node_id;
    raw.node.attestation_verified = boolFromU8(attestationVerified);
    raw.node.attestation_bypassed = boolFromU8(attestationBypassed);
    raw.node.dev_insecure_mode = boolFromU8(devInsecure);
    return raw;
}

void updateSavedNodeCache(MatterSavedState state, const MatterSavedNode &node, const std::string &reason) {
    g_savedNodeLoaded = true;
    g_savedState = state;
    g_savedStateReason = reason;
    g_savedNode = state == MatterSavedState::Valid || state == MatterSavedState::Invalid ? node : MatterSavedNode{};
    g_postResult.savedNode = g_savedNode;
}

[[maybe_unused]] esp_err_t saveSavedNodeToNvs(const MatterSavedNode &node) {
    if (!node.commissioned || !isValidRemoteNodeId(node.node_id)) {
        ESP_LOGE(TAG, "save_matter_node_metadata refused reason=invalid_remote_node_id");
        return ESP_ERR_INVALID_ARG;
    }
    char nodeBuf[24];
    formatMatterId(node.node_id, nodeBuf, sizeof(nodeBuf));
    ESP_LOGI(TAG, "save_matter_node_metadata start remote_node_id=%s fabric_index=%u",
             nodeBuf, static_cast<unsigned>(node.fabric_index));
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MATTER_APP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    bool opened = err == ESP_OK;
    if (err == ESP_OK) err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_COMMISSIONED, node.commissioned ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u64(handle, KEY_NODE_ID, node.node_id);
    if (err == ESP_OK) err = nvs_set_u16(handle, KEY_FABRIC_INDEX, node.fabric_index);
    if (err == ESP_OK) err = nvs_set_u64(handle, KEY_CONTROLLER_NODE_ID, node.controller_node_id);
    if (err == ESP_OK) err = nvs_set_u64(handle, KEY_PREFERRED_REMOTE_NODE_ID, node.preferred_remote_node_id);
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_COMMISSIONING_MODE, node.commissioning_mode);
    if (err == ESP_OK && !node.last_known_addr.empty()) err = nvs_set_str(handle, KEY_LAST_KNOWN_ADDR, node.last_known_addr.c_str());
    if (err == ESP_OK && !node.last_known_host.empty()) err = nvs_set_str(handle, KEY_LAST_KNOWN_HOST, node.last_known_host.c_str());
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_ATT_VERIFIED, node.attestation_verified ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_ATT_BYPASSED, node.attestation_bypassed ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, KEY_DEV_INSECURE, node.dev_insecure_mode ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u64(handle, KEY_COMMISSIONED_AT, node.commissioned_at_unix_or_boot_counter);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    ESP_LOGI(TAG, "save_matter_node_metadata result=%s", esp_err_to_name(err));
    if (err == ESP_OK) {
        updateSavedNodeCache(MatterSavedState::Valid, node, "");
    } else {
        MatterSavedNode empty;
        updateSavedNodeCache(MatterSavedState::None, empty, "");
    }
    return err;
}

void ensureSavedNodeLoaded() {
    if (g_savedNodeLoaded) return;
    MatterSavedNode loaded;
    std::string reason;
    (void)matter_saved_state_load_and_validate(loaded, reason);
}

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
void logEspStepResult(const char *step, esp_err_t err) {
    ESP_LOGI(TAG, "init_step=%s result=%s code=%d", step, esp_err_to_name(err), static_cast<int>(err));
}

void logChipStepResult(const char *step, CHIP_ERROR err) {
    ESP_LOGI(TAG, "init_step=%s chip_error=%s code=%" PRIu32 " subsystem=%" PRIu32,
             step, err.AsString(), static_cast<uint32_t>(err.GetValue()),
             static_cast<uint32_t>(err.GetRange()));
}

bool isPaaNotFoundOrUntrusted(chip::Credentials::AttestationVerificationResult result) {
    using chip::Credentials::AttestationVerificationResult;
    return result == AttestationVerificationResult::kPaaNotFound ||
           result == AttestationVerificationResult::kPaaUntrusted ||
           result == AttestationVerificationResult::kCertificationDeclarationInvalidPAA;
}

bool isSendNocFabricConflict(chip::Controller::CommissioningStage stage, CHIP_ERROR error) {
    return stage == chip::Controller::CommissioningStage::kSendNOC &&
           static_cast<uint32_t>(error.GetValue()) == CHIP_ERROR_NOC_RESPONSE_STATUS;
}

void dumpLocalStorageSummaryLocked();

bool isLightDeviceType(uint32_t deviceType) {
    switch (deviceType) {
        case 0x0100: // On/Off Light
        case 0x0101: // Dimmable Light
        case 0x010C: // Color Temperature Light
        case 0x010D: // Extended Color Light
            return true;
        default:
            return false;
    }
}

bool isKnownOnOffDeviceType(uint32_t deviceType) {
    switch (deviceType) {
        case 0x010A: // On/Off Plug-in Unit
        case 0x010B: // Dimmable Plug-in Unit
            return true;
        default:
            return isLightDeviceType(deviceType);
    }
}

const char *fallbackTypeLabel(const MatterDeviceItem &item) {
    if (item.has_switch && !item.has_onoff) return "Interrupteur";
    if (item.has_onoff && isLightDeviceType(item.device_type)) return "Lumière";
    if (item.has_color_control) return "Lumière";
    if (item.has_level_control && item.has_onoff) return isLightDeviceType(item.device_type) ? "Lumière" : "Appareil On/Off";
    if (item.has_onoff) return "Appareil On/Off";
    return "Appareil Matter";
}

std::string fallbackDeviceName(const MatterDeviceItem &item) {
    return std::string(fallbackTypeLabel(item)) + " " + std::to_string(item.endpoint_id);
}

bool isMainListVisible(const MatterDeviceItem &item) {
    return item.has_onoff || item.has_switch;
}

uint64_t timestampNow() {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL);
}

std::string boundedString(const char *value, size_t maxLen) {
    if (!value || maxLen == 0) return {};
    size_t len = 0;
    while (len < maxLen && value[len] != '\0') ++len;
    return std::string(value, len);
}

void copyBounded(char *dst, size_t dstLen, const std::string &src) {
    if (!dst || dstLen == 0) return;
    const size_t len = std::min(dstLen - 1, src.size());
    if (len > 0) memcpy(dst, src.data(), len);
    dst[len] = '\0';
}

const char *commissioningFlowName(chip::CommissioningFlow flow) {
    switch (flow) {
        case chip::CommissioningFlow::kStandard: return "standard";
        case chip::CommissioningFlow::kUserActionRequired: return "user_action_required";
        case chip::CommissioningFlow::kCustom: return "custom";
    }
    return "unknown";
}

const char *discoveryCapabilitiesText(chip::SetupPayload &payload) {
    if (!payload.rendezvousInformation.HasValue()) return "manual_code_no_rendezvous";
    const uint8_t flags = payload.rendezvousInformation.Value().Raw();
    if (flags == 0) return "none";
    if (flags & static_cast<uint8_t>(chip::RendezvousInformationFlag::kOnNetwork)) return "on_network";
    if (flags & static_cast<uint8_t>(chip::RendezvousInformationFlag::kBLE)) return "ble";
    if (flags & static_cast<uint8_t>(chip::RendezvousInformationFlag::kSoftAP)) return "softap";
    return "other";
}

void logSetupPayloadDetails(const std::string &normalizedCode) {
    std::vector<chip::SetupPayload> payloads;
    const CHIP_ERROR parseErr = chip::SetupPayload::FromStringRepresentation(normalizedCode.c_str(), payloads);
    if (parseErr != CHIP_NO_ERROR || payloads.empty()) {
        ESP_LOGW(TAG, "setup_payload log_failed err=%s count=%u",
                 chip::ErrorStr(parseErr), static_cast<unsigned>(payloads.size()));
        return;
    }
    chip::SetupPayload &payload = payloads.front();
    const bool shortDiscriminator = payload.discriminator.IsShortDiscriminator();
    const uint16_t discriminator = shortDiscriminator
        ? payload.discriminator.GetShortValue()
        : payload.discriminator.GetLongValue();
    const uint8_t rendezvousFlags = payload.rendezvousInformation.HasValue()
        ? payload.rendezvousInformation.Value().Raw()
        : 0;
    ESP_LOGI(TAG, "setup_payload discriminator=%u type=%s",
             static_cast<unsigned>(discriminator), shortDiscriminator ? "short" : "long");
    ESP_LOGI(TAG, "setup_payload passcode_present=%s",
             chip::PayloadContents::IsValidSetupPIN(payload.setUpPINCode) ? "yes" : "no");
    ESP_LOGI(TAG, "setup_payload commissioning_flow=%s",
             commissioningFlowName(payload.commissioningFlow));
    ESP_LOGI(TAG, "setup_payload rendezvous_flags=0x%02x",
             static_cast<unsigned>(rendezvousFlags));
    ESP_LOGI(TAG, "setup_payload discovery_capabilities=%s",
             discoveryCapabilitiesText(payload));
}

void readAttributeCallback(uint64_t remoteNodeId, const chip::app::ConcreteDataAttributePath &path,
                           chip::TLV::TLVReader *data) {
    ReadWaitContext *ctx = g_activeRead;
    if (!ctx || !data || remoteNodeId != ctx->node_id ||
        path.mEndpointId != ctx->endpoint_id ||
        path.mClusterId != ctx->cluster_id ||
        path.mAttributeId != ctx->attribute_id) {
        return;
    }

    using namespace chip::app::Clusters;
    CHIP_ERROR err = CHIP_NO_ERROR;
    if (path.mClusterId == Descriptor::Id &&
        path.mAttributeId == Descriptor::Attributes::PartsList::Id) {
        Descriptor::Attributes::PartsList::TypeInfo::DecodableType list;
        err = chip::app::DataModel::Decode(*data, list);
        if (err == CHIP_NO_ERROR) {
            auto iter = list.begin();
            while (iter.Next()) {
                ctx->endpoints.push_back(iter.GetValue());
            }
            err = iter.GetStatus();
        }
    } else if (path.mClusterId == Descriptor::Id &&
               path.mAttributeId == Descriptor::Attributes::ServerList::Id) {
        Descriptor::Attributes::ServerList::TypeInfo::DecodableType list;
        err = chip::app::DataModel::Decode(*data, list);
        if (err == CHIP_NO_ERROR) {
            auto iter = list.begin();
            while (iter.Next()) {
                ctx->cluster_ids.push_back(iter.GetValue());
            }
            err = iter.GetStatus();
        }
    } else if (path.mClusterId == Descriptor::Id &&
               path.mAttributeId == Descriptor::Attributes::DeviceTypeList::Id) {
        Descriptor::Attributes::DeviceTypeList::TypeInfo::DecodableType list;
        err = chip::app::DataModel::Decode(*data, list);
        if (err == CHIP_NO_ERROR) {
            auto iter = list.begin();
            while (iter.Next()) {
                ctx->device_types.push_back(iter.GetValue().deviceType);
            }
            err = iter.GetStatus();
        }
    } else if (path.mClusterId == OnOff::Id &&
               path.mAttributeId == OnOff::Attributes::OnOff::Id) {
        err = chip::app::DataModel::Decode(*data, ctx->bool_value);
    } else if ((path.mClusterId == BasicInformation::Id &&
                (path.mAttributeId == BasicInformation::Attributes::NodeLabel::Id ||
                 path.mAttributeId == BasicInformation::Attributes::ProductName::Id ||
                 path.mAttributeId == BasicInformation::Attributes::VendorName::Id ||
                 path.mAttributeId == BasicInformation::Attributes::UniqueID::Id)) ||
               (path.mClusterId == BridgedDeviceBasicInformation::Id &&
                (path.mAttributeId == BridgedDeviceBasicInformation::Attributes::NodeLabel::Id ||
                 path.mAttributeId == BridgedDeviceBasicInformation::Attributes::ProductName::Id ||
                 path.mAttributeId == BridgedDeviceBasicInformation::Attributes::VendorName::Id ||
                 path.mAttributeId == BridgedDeviceBasicInformation::Attributes::UniqueID::Id))) {
        chip::CharSpan value;
        err = chip::app::DataModel::Decode(*data, value);
        if (err == CHIP_NO_ERROR && value.data() && value.size() > 0) {
            ctx->string_value.assign(value.data(), value.size());
        }
    } else {
        err = CHIP_ERROR_INVALID_ARGUMENT;
    }
    ctx->chip_error = err;
    ctx->got_value = err == CHIP_NO_ERROR;
}

void readDoneCallback(uint64_t remoteNodeId,
                      const chip::Platform::ScopedMemoryBufferWithSize<chip::app::AttributePathParams> &,
                      const chip::Platform::ScopedMemoryBufferWithSize<chip::app::EventPathParams> &) {
    ReadWaitContext *ctx = g_activeRead;
    if (!ctx || remoteNodeId != ctx->node_id) return;
    if (ctx->done) xSemaphoreGive(ctx->done);
}

esp_err_t readMatterAttribute(ReadWaitContext &ctx, uint64_t nodeId, uint16_t endpointId,
                              uint32_t clusterId, uint32_t attributeId, uint32_t timeoutMs = 10000) {
    ctx = {};
    ctx.done = xSemaphoreCreateBinary();
    if (!ctx.done) return ESP_ERR_NO_MEM;
    ctx.chip_error = CHIP_ERROR_TIMEOUT;
    ctx.node_id = nodeId;
    ctx.endpoint_id = endpointId;
    ctx.cluster_id = clusterId;
    ctx.attribute_id = attributeId;
    g_activeRead = &ctx;

    auto *cmd = chip::Platform::New<esp_matter::controller::read_command>(
        nodeId, endpointId, clusterId, attributeId, esp_matter::controller::READ_ATTRIBUTE,
        readAttributeCallback, readDoneCallback, nullptr);
    if (!cmd) {
        g_activeRead = nullptr;
        vSemaphoreDelete(ctx.done);
        ctx.done = nullptr;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err;
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        err = cmd->send_command();
    }
    if (err != ESP_OK) {
        g_activeRead = nullptr;
        vSemaphoreDelete(ctx.done);
        ctx.done = nullptr;
        return err;
    }
    if (xSemaphoreTake(ctx.done, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        g_activeRead = nullptr;
        vSemaphoreDelete(ctx.done);
        ctx.done = nullptr;
        char nodeBuf[24];
        formatMatterId(nodeId, nodeBuf, sizeof(nodeBuf));
        ESP_LOGW(TAG, "read_timeout node_id=%s endpoint=%u cluster=0x%lx attr=0x%lx",
                 nodeBuf, endpointId,
                 static_cast<unsigned long>(clusterId), static_cast<unsigned long>(attributeId));
        return ESP_ERR_TIMEOUT;
    }
    g_activeRead = nullptr;
    vSemaphoreDelete(ctx.done);
    ctx.done = nullptr;
    if (!ctx.got_value) return ESP_FAIL;
    return ctx.chip_error == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
}

esp_err_t readMatterStringAttribute(uint64_t nodeId, uint16_t endpointId,
                                    uint32_t clusterId, uint32_t attributeId,
                                    std::string &out, uint32_t timeoutMs = 6000) {
    ReadWaitContext ctx;
    const esp_err_t err = readMatterAttribute(ctx, nodeId, endpointId, clusterId, attributeId, timeoutMs);
    if (err != ESP_OK || ctx.string_value.empty()) return err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    out = ctx.string_value;
    return ESP_OK;
}

bool tryReadEndpointName(uint64_t nodeId, MatterDeviceItem &item) {
    struct NameAttempt {
        uint32_t cluster;
        uint32_t attribute;
        const char *source;
    };
    const NameAttempt attempts[] = {
        { chip::app::Clusters::BridgedDeviceBasicInformation::Id,
          chip::app::Clusters::BridgedDeviceBasicInformation::Attributes::NodeLabel::Id,
          "bridged_node_label" },
        { chip::app::Clusters::BasicInformation::Id,
          chip::app::Clusters::BasicInformation::Attributes::NodeLabel::Id,
          "basic_node_label" },
        { chip::app::Clusters::BridgedDeviceBasicInformation::Id,
          chip::app::Clusters::BridgedDeviceBasicInformation::Attributes::ProductName::Id,
          "bridged_product_name" },
        { chip::app::Clusters::BasicInformation::Id,
          chip::app::Clusters::BasicInformation::Attributes::ProductName::Id,
          "basic_product_name" },
    };
    for (const auto &attempt : attempts) {
        std::string name;
        if (readMatterStringAttribute(nodeId, item.endpoint_id, attempt.cluster, attempt.attribute, name) == ESP_OK &&
            !name.empty()) {
            item.name = name;
            ESP_LOGI(TAG, "read_endpoint_name endpoint=%u source=%s name=\"%s\"",
                     item.endpoint_id, attempt.source, item.name.c_str());
            return true;
        }
    }
    item.name = fallbackDeviceName(item);
    ESP_LOGI(TAG, "read_endpoint_name endpoint=%u source=fallback name=\"%s\"",
             item.endpoint_id, item.name.c_str());
    return false;
}

MatterDeviceItem recordToDevice(const DeviceCacheRecord &rec) {
    MatterDeviceItem item;
    item.endpoint_id = rec.endpoint_id;
    item.has_onoff = (rec.flags & CACHE_HAS_ONOFF) != 0;
    item.has_level_control = (rec.flags & CACHE_HAS_LEVEL) != 0;
    item.has_color_control = (rec.flags & CACHE_HAS_COLOR) != 0;
    item.has_switch = (rec.flags & CACHE_HAS_SWITCH) != 0;
    item.reachable_known = (rec.flags & CACHE_REACHABLE_KNOWN) != 0;
    item.reachable = (rec.flags & CACHE_REACHABLE) != 0;
    item.onoff_state_known = (rec.flags & CACHE_ONOFF_KNOWN) != 0;
    item.onoff_state = (rec.flags & CACHE_ONOFF_STATE) != 0;
    item.device_type = rec.device_type;
    item.primary_cluster_class = rec.primary_cluster_class;
    item.last_seen_unix_or_boot_counter = rec.last_seen;
    item.name = boundedString(rec.display_name, sizeof(rec.display_name));
    item.type_label = boundedString(rec.type_label, sizeof(rec.type_label));
    return item;
}

DeviceCacheRecord deviceToRecord(const MatterDeviceItem &item) {
    DeviceCacheRecord rec;
    rec.endpoint_id = item.endpoint_id;
    rec.flags = 0;
    if (item.has_onoff) rec.flags |= CACHE_HAS_ONOFF;
    if (item.has_level_control) rec.flags |= CACHE_HAS_LEVEL;
    if (item.has_color_control) rec.flags |= CACHE_HAS_COLOR;
    if (item.has_switch) rec.flags |= CACHE_HAS_SWITCH;
    if (item.reachable_known) rec.flags |= CACHE_REACHABLE_KNOWN;
    if (item.reachable) rec.flags |= CACHE_REACHABLE;
    if (item.onoff_state_known) rec.flags |= CACHE_ONOFF_KNOWN;
    if (item.onoff_state) rec.flags |= CACHE_ONOFF_STATE;
    rec.device_type = item.device_type;
    rec.primary_cluster_class = item.primary_cluster_class;
    rec.last_seen = item.last_seen_unix_or_boot_counter;
    copyBounded(rec.display_name, sizeof(rec.display_name), item.name);
    copyBounded(rec.type_label, sizeof(rec.type_label), item.type_label);
    return rec;
}

esp_err_t saveDeviceCache(const std::vector<MatterDeviceItem> &devices) {
    const size_t count = std::min(devices.size(), DEVICE_CACHE_MAX_ITEMS);
    std::vector<uint8_t> blob(sizeof(DeviceCacheHeader) + count * sizeof(DeviceCacheRecord));
    auto *header = reinterpret_cast<DeviceCacheHeader *>(blob.data());
    header->magic = DEVICE_CACHE_MAGIC;
    header->version = DEVICE_CACHE_VERSION;
    header->count = static_cast<uint16_t>(count);
    auto *records = reinterpret_cast<DeviceCacheRecord *>(blob.data() + sizeof(DeviceCacheHeader));
    for (size_t i = 0; i < count; ++i) records[i] = deviceToRecord(devices[i]);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(MATTER_DEVICE_CACHE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    const bool opened = err == ESP_OK;
    if (err == ESP_OK) err = nvs_set_blob(handle, KEY_DEVICE_CACHE_BLOB, blob.data(), blob.size());
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    ESP_LOGI(TAG, "device_cache_save count=%u result=%s",
             static_cast<unsigned>(count), esp_err_to_name(err));
    return err;
}

esp_err_t loadDeviceCache(std::vector<MatterDeviceItem> &devices) {
    devices.clear();
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MATTER_DEVICE_CACHE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "device_cache_load count=0 result=%s", esp_err_to_name(ESP_OK));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device_cache_load count=0 result=%s", esp_err_to_name(err));
        return err;
    }
    size_t size = 0;
    err = nvs_get_blob(handle, KEY_DEVICE_CACHE_BLOB, nullptr, &size);
    if (err == ESP_OK && size >= sizeof(DeviceCacheHeader)) {
        std::vector<uint8_t> blob(size);
        err = nvs_get_blob(handle, KEY_DEVICE_CACHE_BLOB, blob.data(), &size);
        if (err == ESP_OK) {
            const auto *header = reinterpret_cast<const DeviceCacheHeader *>(blob.data());
            if (header->magic == DEVICE_CACHE_MAGIC && header->version == DEVICE_CACHE_VERSION) {
                const size_t available = (size - sizeof(DeviceCacheHeader)) / sizeof(DeviceCacheRecord);
                const size_t count = std::min<size_t>(header->count, std::min(available, DEVICE_CACHE_MAX_ITEMS));
                const auto *records = reinterpret_cast<const DeviceCacheRecord *>(blob.data() + sizeof(DeviceCacheHeader));
                for (size_t i = 0; i < count; ++i) devices.push_back(recordToDevice(records[i]));
            } else {
                err = ESP_ERR_INVALID_VERSION;
            }
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "device_cache_load count=%u result=%s",
             static_cast<unsigned>(devices.size()), esp_err_to_name(err));
    return err;
}

void commandSuccessCallback(void *, const chip::app::ConcreteCommandPath &commandPath,
                            const chip::app::StatusIB &status, chip::TLV::TLVReader *) {
    CommandWaitContext *ctx = g_activeCommand;
    if (!ctx || commandPath.mEndpointId != ctx->endpoint_id) return;
    ctx->chip_error = status.ToChipError();
    ctx->got_result = true;
    if (ctx->done) xSemaphoreGive(ctx->done);
}

void commandErrorCallback(void *, CHIP_ERROR error) {
    CommandWaitContext *ctx = g_activeCommand;
    if (!ctx) return;
    ctx->chip_error = error;
    ctx->got_result = true;
    if (ctx->done) xSemaphoreGive(ctx->done);
}

esp_err_t sendOnOffCommandSync(uint64_t nodeId, uint16_t endpointId, uint32_t commandId, uint32_t timeoutMs = 10000) {
    CommandWaitContext ctx{};
    ctx.done = xSemaphoreCreateBinary();
    if (!ctx.done) return ESP_ERR_NO_MEM;
    ctx.endpoint_id = endpointId;
    ctx.chip_error = CHIP_ERROR_TIMEOUT;
    g_activeCommand = &ctx;

    auto *cmd = chip::Platform::New<esp_matter::controller::cluster_command>(
        nodeId, endpointId, chip::app::Clusters::OnOff::Id, commandId, nullptr,
        chip::NullOptional, commandSuccessCallback, commandErrorCallback);
    if (!cmd) {
        g_activeCommand = nullptr;
        vSemaphoreDelete(ctx.done);
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err;
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        err = cmd->send_command();
    }
    if (err != ESP_OK) {
        g_activeCommand = nullptr;
        vSemaphoreDelete(ctx.done);
        return err;
    }
    if (xSemaphoreTake(ctx.done, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        g_activeCommand = nullptr;
        vSemaphoreDelete(ctx.done);
        return ESP_ERR_TIMEOUT;
    }
    g_activeCommand = nullptr;
    vSemaphoreDelete(ctx.done);
    return ctx.chip_error == CHIP_NO_ERROR ? ESP_OK : ESP_FAIL;
}

const char *attestationResultName(chip::Credentials::AttestationVerificationResult result) {
    using chip::Credentials::AttestationVerificationResult;
    switch (result) {
        case AttestationVerificationResult::kSuccess: return "success";
        case AttestationVerificationResult::kPaaUntrusted: return "paa_untrusted";
        case AttestationVerificationResult::kPaaNotFound: return "paa_not_found";
        case AttestationVerificationResult::kCertificationDeclarationInvalidPAA: return "cd_invalid_paa";
        default: return "other";
    }
}

#if CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION
class DevPaaBypassVerifier : public chip::Credentials::DeviceAttestationVerifier {
public:
    explicit DevPaaBypassVerifier(chip::Credentials::DeviceAttestationVerifier *strictVerifier)
        : strictVerifier_(strictVerifier) {}

    void VerifyAttestationInformation(
        const AttestationInfo &info,
        chip::Callback::Callback<OnAttestationInformationVerification> *onCompletion) override {
        CallbackContext context{this, onCompletion};
        chip::Callback::Callback<OnAttestationInformationVerification> callback(OnStrictAttestationComplete, &context);
        strictVerifier_->VerifyAttestationInformation(info, &callback);
    }

    chip::Credentials::AttestationVerificationResult ValidateCertificationDeclarationSignature(
        const chip::ByteSpan &cmsEnvelopeBuffer,
        chip::ByteSpan &certDeclBuffer) override {
        return strictVerifier_->ValidateCertificationDeclarationSignature(cmsEnvelopeBuffer, certDeclBuffer);
    }

    chip::Credentials::AttestationVerificationResult ValidateCertificateDeclarationPayload(
        const chip::ByteSpan &certDeclBuffer,
        const chip::ByteSpan &firmwareInfo,
        const chip::Credentials::DeviceInfoForAttestation &deviceInfo) override {
        auto result = strictVerifier_->ValidateCertificateDeclarationPayload(certDeclBuffer, firmwareInfo, deviceInfo);
        return maybeBypass("CertificationDeclaration", result);
    }

    CHIP_ERROR VerifyNodeOperationalCSRInformation(
        const chip::ByteSpan &nocsrElementsBuffer,
        const chip::ByteSpan &attestationChallengeBuffer,
        const chip::ByteSpan &attestationSignatureBuffer,
        const chip::Crypto::P256PublicKey &dacPublicKey,
        const chip::ByteSpan &csrNonce) override {
        return strictVerifier_->VerifyNodeOperationalCSRInformation(
            nocsrElementsBuffer, attestationChallengeBuffer, attestationSignatureBuffer, dacPublicKey, csrNonce);
    }

    void CheckForRevokedDACChain(
        const AttestationInfo &info,
        chip::Callback::Callback<OnAttestationInformationVerification> *onCompletion) override {
        CallbackContext context{this, onCompletion};
        chip::Callback::Callback<OnAttestationInformationVerification> callback(OnStrictAttestationComplete, &context);
        strictVerifier_->CheckForRevokedDACChain(info, &callback);
    }

    chip::Credentials::WellKnownKeysTrustStore *GetCertificationDeclarationTrustStore() override {
        return strictVerifier_->GetCertificationDeclarationTrustStore();
    }

    CHIP_ERROR SetRevocationDelegate(chip::Credentials::DeviceAttestationRevocationDelegate *revocationDelegate) override {
        return strictVerifier_->SetRevocationDelegate(revocationDelegate);
    }

private:
    struct CallbackContext {
        DevPaaBypassVerifier *self;
        chip::Callback::Callback<OnAttestationInformationVerification> *original;
    };

    static void OnStrictAttestationComplete(
        void *context,
        const AttestationInfo &info,
        chip::Credentials::AttestationVerificationResult result) {
        auto *callbackContext = static_cast<CallbackContext *>(context);
        result = callbackContext->self->maybeBypass("AttestationVerification", result);
        callbackContext->original->mCall(callbackContext->original->mContext, info, result);
    }

    chip::Credentials::AttestationVerificationResult maybeBypass(
        const char *stage,
        chip::Credentials::AttestationVerificationResult result) {
        if (!isPaaNotFoundOrUntrusted(result)) {
            return result;
        }
        g_attestationBypassObserved = true;
        ESP_LOGW(TAG, "attestation_failure stage=%s chip_error=%s result=%s",
                 stage, chip::ErrorStr(CHIP_ERROR_INTERNAL), attestationResultName(result));
        ESP_LOGW(TAG, "attestation_failure_reason=paa_not_found_or_untrusted");
        ESP_LOGW(TAG, "attestation_unverified_continue=yes");
        return chip::Credentials::AttestationVerificationResult::kSuccess;
    }

    chip::Credentials::DeviceAttestationVerifier *strictVerifier_ = nullptr;
};
#endif

chip::Credentials::DeviceAttestationVerifier *configureAttestationVerifier() {
    const chip::Credentials::AttestationTrustStore *trustStore = chip::Credentials::get_attestation_trust_store();
    chip::Credentials::DeviceAttestationVerifier *strictVerifier = chip::Credentials::GetDefaultDACVerifier(trustStore);
#if CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION
    static DevPaaBypassVerifier devVerifier(strictVerifier);
    ESP_LOGW(TAG, "attestation_policy=allow_untrusted_dev");
    ESP_LOGW(TAG, "WARNING Matter device attestation is not verified in this build");
    ESP_LOGW(TAG, "WARNING development-only, do not use for production");
    chip::Credentials::SetDeviceAttestationVerifier(&devVerifier);
    return &devVerifier;
#else
    ESP_LOGI(TAG, "attestation_policy=strict");
    chip::Credentials::SetDeviceAttestationVerifier(strictVerifier);
    return strictVerifier;
#endif
}

esp_err_t logNvsStorageStatus() {
    const uint32_t nvsSize = partitionSizeByLabel("nvs", ESP_PARTITION_TYPE_DATA);
    ESP_LOGI(TAG, "nvs_partition size=%u", static_cast<unsigned>(nvsSize));

    ESP_LOGI(TAG, "init_step=platform_storage");
    esp_err_t err = nvs_flash_init();
    logEspStepResult("platform_storage", err);
    ESP_LOGI(TAG, "nvs_flash_init status=%s code=%d", esp_err_to_name(err), static_cast<int>(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    nvs_stats_t stats{};
    esp_err_t statsErr = nvs_get_stats("nvs", &stats);
    ESP_LOGI(TAG, "chip_storage status=%s used_entries=%u free_entries=%u total_entries=%u",
             esp_err_to_name(statsErr), static_cast<unsigned>(stats.used_entries),
             static_cast<unsigned>(stats.free_entries), static_cast<unsigned>(stats.total_entries));
    return statsErr == ESP_OK ? ESP_OK : statsErr;
}

void matter_app_event_cb(const ChipDeviceEvent *event, intptr_t) {
    if (!event) return;
    if (event->Type == chip::DeviceLayer::DeviceEventType::kESPSystemEvent &&
        event->Platform.ESPSystemEvent.Base == IP_EVENT &&
        event->Platform.ESPSystemEvent.Id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "matter_event ip_ready");
    }
}

void pase_callback(CHIP_ERROR err) {
    logChipStepResult("pase_callback", err);
    if (err == CHIP_NO_ERROR) {
        ESP_LOGI(TAG, "pase_session_ok");
        return;
    }
    g_status = MatterCommissioningStatus::FAILED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_FAIL;
    g_lastResult.errorText = std::string("Échec PASE: ") + chip::ErrorStr(err);
    ESP_LOGW(TAG, "pase_session_failed err=%s", chip::ErrorStr(err));
}

void commissioning_success_callback(chip::ScopedNodeId peerId) {
    const uint64_t callbackNodeId = peerId.GetNodeId();
    const uint64_t preferredNodeId = g_pendingPreferredRemoteNodeId;
    uint64_t remoteNodeId = isValidRemoteNodeId(callbackNodeId) ? callbackNodeId : 0;
    char callbackNodeBuf[24];
    char preferredNodeBuf[24];
    char remoteNodeBuf[24];
    formatMatterId(callbackNodeId, callbackNodeBuf, sizeof(callbackNodeBuf));
    formatMatterId(preferredNodeId, preferredNodeBuf, sizeof(preferredNodeBuf));
    bool usedPreferredNodeId = false;
    if (remoteNodeId == 0 && isValidRemoteNodeId(preferredNodeId)) {
        remoteNodeId = preferredNodeId;
        usedPreferredNodeId = true;
    }
    formatMatterId(remoteNodeId, remoteNodeBuf, sizeof(remoteNodeBuf));
    if (usedPreferredNodeId) {
        ESP_LOGW(TAG, "commissioning_success using_preferred_node_id=%s", remoteNodeBuf);
    }
    ESP_LOGI(TAG, "commissioning_success callback_node_id=%s", callbackNodeBuf);
    ESP_LOGI(TAG, "commissioning_success preferred_node_id=%s", preferredNodeBuf);
    ESP_LOGI(TAG, "commissioning_success selected_remote_node_id=%s", remoteNodeBuf);

    if (!isValidRemoteNodeId(remoteNodeId)) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = ESP_ERR_INVALID_STATE;
        g_lastResult.errorText = "Node ID Matter distant invalide";
        g_lastResult.nodeId = 0;
        ESP_LOGE(TAG, "save_matter_node_metadata refused reason=invalid_remote_node_id");
        return;
    }

    MatterSavedNode saved;
    saved.commissioned = true;
    saved.node_id = remoteNodeId;
    saved.fabric_index = peerId.GetFabricIndex();
    saved.controller_node_id = CONTROL_CONTROLLER_NODE_ID;
    saved.preferred_remote_node_id = preferredNodeId;
    saved.commissioning_mode = g_pendingCommissioningMode;
    saved.last_known_addr = g_lastMdnsPrescanAddr;
    saved.last_known_host = g_lastMdnsPrescanHost;
    g_lastResult.dev_insecure_mode =
#if CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION
        true;
#else
        false;
#endif
    g_lastResult.attestation_bypassed = g_attestationBypassObserved;
    g_lastResult.attestation_verified = !g_lastResult.attestation_bypassed;
    saved.attestation_verified = g_lastResult.attestation_verified;
    saved.attestation_bypassed = g_lastResult.attestation_bypassed;
    saved.dev_insecure_mode = g_lastResult.dev_insecure_mode;
    saved.commissioned_at_unix_or_boot_counter =
        static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL);

    esp_err_t saveErr = saveSavedNodeToNvs(saved);
    if (saveErr != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = saveErr;
        g_lastResult.errorText = std::string("Sauvegarde metadata Matter échouée: ") + esp_err_to_name(saveErr);
        return;
    }
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    g_previousSendNocFabricConflict = false;
#endif
    g_status = MatterCommissioningStatus::COMMISSIONED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_OK;
    g_lastResult.errorText.clear();
    g_lastResult.nodeId = remoteNodeId;
    g_lastResult.fabricIndex = saved.fabric_index;
    g_postResult.savedNode = saved;
    formatMatterId(g_lastResult.nodeId, remoteNodeBuf, sizeof(remoteNodeBuf));
    ESP_LOGI(TAG, "commissioning_success remote_node_id=%s fabric_index=%u",
             remoteNodeBuf, static_cast<unsigned>(g_lastResult.fabricIndex));
}

void commissioning_failure_callback(
    chip::ScopedNodeId peerId,
    CHIP_ERROR error,
    chip::Controller::CommissioningStage stage,
    std::optional<chip::Credentials::AttestationVerificationResult> additionalErrorInfo) {
    g_status = MatterCommissioningStatus::FAILED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_FAIL;
    g_lastResult.nodeId = peerId.GetNodeId();
    g_lastResult.fabricIndex = peerId.GetFabricIndex();
    g_lastResult.attestation_verified = false;
    g_lastResult.attestation_bypassed = g_attestationBypassObserved;
    g_lastResult.dev_insecure_mode =
#if CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION
        true;
#else
        false;
#endif
    g_lastResult.failure_stage = chip::Controller::StageToString(stage);
    const bool attestationFailure =
        additionalErrorInfo.has_value() && isPaaNotFoundOrUntrusted(additionalErrorInfo.value());
    const bool sendNocFailure = stage == chip::Controller::CommissioningStage::kSendNOC;
    const bool sendNocFabricConflict = isSendNocFabricConflict(stage, error);
    char nodeBuf[24];
    formatMatterId(g_lastResult.nodeId, nodeBuf, sizeof(nodeBuf));
    if (sendNocFabricConflict) {
        g_previousSendNocFabricConflict = true;
        g_lastResult.failure_reason = "send_noc_fabric_conflict";
        g_lastResult.errorText =
            "Matter déjà appairé ou fabric en conflit\n"
            "Oublier Matter côté IKEA puis réessayer";
        ESP_LOGW(TAG, "failure_reason=send_noc_fabric_conflict chip_error=%s node_id=%s fabric_index=%u noc_status=9",
                 chip::ErrorStr(error), nodeBuf,
                 static_cast<unsigned>(g_lastResult.fabricIndex));
        dumpLocalStorageSummaryLocked();
    } else if (sendNocFailure) {
        g_lastResult.failure_reason = "send_noc_failed";
        g_lastResult.errorText = std::string("Échec SendNOC: ") + chip::ErrorStr(error);
        ESP_LOGW(TAG, "send_noc_failed chip_error=%s node_id=%s fabric_index=%u",
                 chip::ErrorStr(error), nodeBuf,
                 static_cast<unsigned>(g_lastResult.fabricIndex));
    } else if (attestationFailure
#if CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION
               && false
#endif
    ) {
        g_lastResult.failure_reason = "paa_not_found";
        g_lastResult.errorText = "Échec attestation: PAA non trouvé, trust store Matter manquant";
        ESP_LOGW(TAG, "attestation_failure stage=%s chip_error=%s result=%s",
                 chip::Controller::StageToString(stage), chip::ErrorStr(error),
                 attestationResultName(additionalErrorInfo.value()));
        ESP_LOGW(TAG, "attestation_failure_reason=paa_not_found_or_untrusted");
    } else {
        if (attestationFailure) {
            ESP_LOGW(TAG, "attestation_failure_nonfatal_after_bypass stage=%s chip_error=%s",
                     chip::Controller::StageToString(stage), chip::ErrorStr(error));
        }
        g_lastResult.errorText = std::string("Échec commissioning: ") + chip::ErrorStr(error) +
                                 " étape " + chip::Controller::StageToString(stage);
    }
    ESP_LOGW(TAG, "commissioning_failed err=%s stage=%s node_id=%s fabric_index=%u",
             chip::ErrorStr(error), chip::Controller::StageToString(stage),
             nodeBuf, static_cast<unsigned>(g_lastResult.fabricIndex));
    logChipStepResult("commissioning_failure", error);
}

void dumpLocalStorageSummaryLocked() {
    ensureSavedNodeLoaded();

    bool fabricExists = false;
    uint64_t controllerNodeId = CONTROL_CONTROLLER_NODE_ID;
    uint64_t fabricId = CONTROL_CONTROLLER_FABRIC_ID;
    uint64_t compressedFabricId = 0;

    if (g_commissionerSetupDone) {
        auto &client = esp_matter::controller::matter_controller_client::get_instance();
        auto *commissioner = client.get_commissioner();
        if (commissioner && commissioner->GetFabricTable()) {
            const chip::FabricIndex fabricIndex = commissioner->GetFabricIndex();
            const chip::FabricInfo *fabric = commissioner->GetFabricTable()->FindFabricWithIndex(fabricIndex);
            if (fabric) {
                fabricExists = true;
                controllerNodeId = fabric->GetNodeId();
                fabricId = fabric->GetFabricId();
                compressedFabricId = fabric->GetCompressedFabricId();
            } else {
                controllerNodeId = commissioner->GetNodeId();
                fabricId = commissioner->GetFabricId();
                compressedFabricId = commissioner->GetCompressedFabricId();
            }
        }
    }

    ESP_LOGI(TAG, "local_storage_summary fabric_exists=%s", fabricExists ? "yes" : "no");
    char controllerNodeBuf[24];
    char fabricBuf[24];
    char compressedFabricBuf[24];
    char remoteNodeBuf[24];
    formatMatterId(controllerNodeId, controllerNodeBuf, sizeof(controllerNodeBuf));
    formatMatterId(fabricId, fabricBuf, sizeof(fabricBuf));
    formatMatterId(compressedFabricId, compressedFabricBuf, sizeof(compressedFabricBuf));
    formatMatterId(g_savedNode.node_id, remoteNodeBuf, sizeof(remoteNodeBuf));
    ESP_LOGI(TAG, "local_storage_summary controller_node_id=%s", controllerNodeBuf);
    ESP_LOGI(TAG, "local_storage_summary fabric_id=%s", fabricBuf);
    ESP_LOGI(TAG, "local_storage_summary compressed_fabric_id=%s", compressedFabricBuf);
    ESP_LOGI(TAG, "local_storage_summary remote_node_id=%s", remoteNodeBuf);
}
#endif
}

const char *matter_commissioning_status_name(MatterCommissioningStatus status) {
    switch (status) {
        case MatterCommissioningStatus::IDLE: return "IDLE";
        case MatterCommissioningStatus::INITIALIZING: return "INITIALIZING";
        case MatterCommissioningStatus::READY: return "READY";
        case MatterCommissioningStatus::COMMISSIONING: return "COMMISSIONING";
        case MatterCommissioningStatus::COMMISSIONED: return "COMMISSIONED";
        case MatterCommissioningStatus::FAILED: return "FAILED";
        case MatterCommissioningStatus::NOT_SUPPORTED: return "NOT_SUPPORTED";
    }
    return "UNKNOWN";
}

MatterSavedState matter_saved_state_load_and_validate(MatterSavedNode &out, std::string &reason) {
    out = {};
    reason.clear();
    RawSavedNodeRead raw = readSavedNodeRaw();
    if (raw.error != ESP_OK) {
        reason = "read_error";
        updateSavedNodeCache(MatterSavedState::Invalid, raw.node, reason);
        ESP_LOGW(TAG, "saved_state=invalid reason=%s err=%s", reason.c_str(), esp_err_to_name(raw.error));
        return MatterSavedState::Invalid;
    }
    if (!raw.namespace_exists || !raw.any_key_present) {
        updateSavedNodeCache(MatterSavedState::None, MatterSavedNode{}, "");
        ESP_LOGI(TAG, "saved_state=none");
        ESP_LOGI(TAG, "saved_node found=no");
        return MatterSavedState::None;
    }
    if (!raw.commissioned_key_present || !raw.node_id_key_present || !raw.node.commissioned) {
        reason = "partial_metadata";
        updateSavedNodeCache(MatterSavedState::Invalid, raw.node, reason);
        ESP_LOGW(TAG, "saved_state=invalid reason=%s", reason.c_str());
        return MatterSavedState::Invalid;
    }
    if (raw.node.node_id == 0) {
        reason = "remote_node_id_zero";
        updateSavedNodeCache(MatterSavedState::Invalid, raw.node, reason);
        ESP_LOGW(TAG, "saved_remote_node_id=0x0 invalid=yes");
        ESP_LOGW(TAG, "saved_node_invalid reason=remote_node_id_zero");
        ESP_LOGW(TAG, "saved_state=invalid reason=%s", reason.c_str());
        return MatterSavedState::Invalid;
    }
    if (raw.node.node_id == CONTROL_CONTROLLER_NODE_ID) {
        reason = "remote_node_id_is_controller";
        updateSavedNodeCache(MatterSavedState::Invalid, raw.node, reason);
        char nodeBuf[24];
        formatMatterId(raw.node.node_id, nodeBuf, sizeof(nodeBuf));
        ESP_LOGW(TAG, "saved_remote_node_id=%s invalid=yes", nodeBuf);
        ESP_LOGW(TAG, "saved_node_invalid reason=remote_node_id_is_controller");
        ESP_LOGW(TAG, "saved_state=invalid reason=%s", reason.c_str());
        return MatterSavedState::Invalid;
    }

    out = raw.node;
    updateSavedNodeCache(MatterSavedState::Valid, raw.node, "");
    char nodeBuf[24];
    formatMatterId(raw.node.node_id, nodeBuf, sizeof(nodeBuf));
    ESP_LOGI(TAG, "saved_state=valid remote_node_id=%s", nodeBuf);
    ESP_LOGI(TAG, "saved_node found=yes node_id=%s fabric_index=%u",
             nodeBuf, static_cast<unsigned>(raw.node.fabric_index));
    ESP_LOGI(TAG, "saved_node attestation_verified=%s attestation_bypassed=%s",
             raw.node.attestation_verified ? "yes" : "no",
             raw.node.attestation_bypassed ? "yes" : "no");
    return MatterSavedState::Valid;
}

void matter_controller_log_boot_status() {
    ensureSavedNodeLoaded();
    logInitLifecycle("boot_status");
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    ESP_LOGI(TAG, "experimental compiled=yes");
    ESP_LOGI(TAG, "init_deferred=yes");
    ESP_LOGI(TAG, "ble_enabled=%s bt_enabled=%s nimble_enabled=%s",
#if CONFIG_ENABLE_ESP32_BLE_CONTROLLER
             "yes",
#else
             "no",
#endif
#if CONFIG_BT_ENABLED
             "yes",
#else
             "no",
#endif
#if CONFIG_BT_NIMBLE_ENABLED
             "yes");
#else
             "no");
#endif
    ESP_LOGI(TAG, "controller init deferred until user action");
#else
    ESP_LOGI(TAG, "experimental compiled=no");
    ESP_LOGI(TAG, "init_deferred=yes");
    ESP_LOGI(TAG, "controller init deferred until user action");
#endif
}

esp_err_t matter_controller_start_platform() {
    ESP_LOGI(TAG, "heap_before_user_init internal=%u psram=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    ESP_LOGI(TAG, "largest_free_block internal=%u psram=%u",
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));

    const esp_partition_t *running = esp_ota_get_running_partition();
    const uint32_t nvsSize = partitionSizeByLabel("nvs", ESP_PARTITION_TYPE_DATA);
    ESP_LOGI(TAG, "app_partition label=%s size=%u used=unknown free=unknown",
             running ? running->label : "-",
             running ? static_cast<unsigned>(running->size) : 0U);
    ESP_LOGI(TAG, "nvs_partition size=%u", static_cast<unsigned>(nvsSize));

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    control_heap_log("before_matter_platform_start");
    ESP_LOGI(TAG, "start_guard active=%s already_started=%s failed_previous=%s",
             g_matterStartInProgress ? "yes" : "no",
             g_platformStarted ? "yes" : "no",
             g_controllerInitFailed ? "yes" : "no");
    if (g_matterStartInProgress) {
        logEspStepResult("esp_matter_start", ESP_ERR_INVALID_STATE);
        ESP_LOGW(TAG, "esp_matter_start_rejected reason=already_in_progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_platformStarted) {
        logEspStepResult("esp_matter_start", ESP_OK);
        ESP_LOGI(TAG, "esp_matter_start_result=%s code=%d already_started=yes",
                 esp_err_to_name(ESP_OK), static_cast<int>(ESP_OK));
        logInitLifecycle("platform_already_started");
        return ESP_OK;
    }

    g_status = MatterCommissioningStatus::INITIALIZING;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_OK;
    g_lastResult.errorText.clear();

    const esp_err_t timeErr = control_time_wait_synced(10000);
    const bool timeValid = control_time_has_valid_utc();
    ESP_LOGI(TAG, "time_valid_utc=%s before_matter", timeValid ? "yes" : "no");
    ESP_LOGI(TAG, "chip_last_known_good_time_seen=%s", timeValid ? "no" : "yes");
    if (!timeValid) {
        ESP_LOGW(TAG, "matter_platform_start_context time_valid_utc=no err=%s", esp_err_to_name(timeErr));
    }

    ESP_LOGI(TAG, "init_step=before_platform");
    esp_err_t err = logNvsStorageStatus();
    if (err != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = err;
        g_lastResult.errorText = std::string("NVS/CHIP storage failed: ") + esp_err_to_name(err);
        setControllerState(MatterControllerState::FailedStorage);
        return err;
    }

    err = ensureChipUniqueIdProvisioned();
    if (err != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = err;
        g_lastResult.errorText = std::string("Provisioning unique-id Matter échoué: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "chip_unique_id provision_failed err=%s", esp_err_to_name(err));
        setControllerState(MatterControllerState::FailedStorage);
        return err;
    }

    logLwipMatterConfig();
    err = waitMatterNetworkPreflight(10000);
    if (err != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = err;
        g_lastResult.errorText = "Wi-Fi/IPv6 non prêt pour Matter";
        ESP_LOGE(TAG, "controller_init_failed reason=network_preflight err=%s", esp_err_to_name(err));
        setControllerState(MatterControllerState::FailedUnknown);
        return err;
    }

    ESP_LOGI(TAG, "init_step=esp_matter_start");
    ESP_LOGI(TAG, "start_enter");
    ESP_LOGI(TAG, "heap_before_start internal=%u largest_internal=%u psram=%u largest_psram=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    ESP_LOGI("CONTROL_TASKS", "main_stack_high_water before_esp_matter_start=%u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    g_matterStartInProgress = true;
    err = esp_matter::start(matter_app_event_cb);
    g_matterStartInProgress = false;
    ESP_LOGI("CONTROL_TASKS", "main_stack_high_water after_esp_matter_start=%u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    logEspStepResult("esp_matter_start", err);
    ESP_LOGI(TAG, "esp_matter_start_result=%s code=%d", esp_err_to_name(err), static_cast<int>(err));
    if (err != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = err;
        g_lastResult.errorText = std::string("esp_matter::start failed: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "controller_init_failed err=%s step=esp_matter_start", esp_err_to_name(err));
        const bool heapLow = logHeapOnInitFailure();
        if (!heapLow) {
            ESP_LOGE(TAG, "controller_init_failed reason=platform_start err=%s", esp_err_to_name(err));
            setControllerState(MatterControllerState::FailedUnknown);
        } else {
            setControllerState(MatterControllerState::FailedLowHeap);
        }
        return err;
    }
    g_platformStarted = true;
    setControllerState(MatterControllerState::PlatformStarted);
    control_heap_log("after_matter_platform_start");
    ESP_LOGI(TAG, "matter_platform_start ok mode=official_controller_bootstrap");
    logInitLifecycle("platform_started");
#else
    g_status = MatterCommissioningStatus::NOT_SUPPORTED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_ERR_NOT_SUPPORTED;
#endif
    return g_lastResult.error;
}

esp_err_t matter_controller_init_internal(bool early, bool requireSavedMetadata) {
    logInitLifecycle("before_init");
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    if (g_controllerInitialized && g_controllerClientInitialized && g_commissionerSetupDone) {
        logInitLifecycle("already_ready");
        return g_lastResult.error;
    }
    if (requireSavedMetadata) {
        MatterSavedNode savedNode;
        std::string savedReason;
        const MatterSavedState savedState = matter_saved_state_load_and_validate(savedNode, savedReason);
        if (savedState == MatterSavedState::Invalid) {
            ESP_LOGW(TAG, "controller_init_skipped reason=invalid_saved_metadata");
            g_postResult = {};
            g_postResult.savedNode = g_savedNode;
            g_postResult.status = MatterPostCommissioningStatus::SAVED_NODE_INVALID;
            g_postResult.error = ESP_ERR_INVALID_STATE;
            g_postResult.errorText = savedReason == "remote_node_id_zero"
                ? "Node ID distant manquant"
                : "Réinitialisation nécessaire";
            g_status = MatterCommissioningStatus::FAILED;
            g_lastResult.status = g_status;
            g_lastResult.error = ESP_ERR_INVALID_STATE;
            g_lastResult.errorText = "Métadonnées Matter invalides";
            g_controllerInitFailed = true;
            g_controllerInitLastError = ESP_ERR_INVALID_STATE;
            setControllerState(MatterControllerState::FailedInvalidMetadata);
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (g_controllerInitInProgress) {
        ESP_LOGW(TAG, "controller_init_rejected reason=already_in_progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_controllerInitFailed) {
        ESP_LOGW(TAG, "controller_init_rejected reason=previous_failure err=%s",
                 esp_err_to_name(g_controllerInitLastError == ESP_OK ? ESP_FAIL : g_controllerInitLastError));
        return g_controllerInitLastError == ESP_OK ? ESP_FAIL : g_controllerInitLastError;
    }

    g_controllerInitInProgress = true;
    if (early) {
        ESP_LOGI(TAG, "early_init_start");
        control_heap_log("before_early_matter_init");
    }
    esp_err_t err = matter_controller_start_platform();
    control_heap_log("after_matter_platform_start");
    if (err != ESP_OK) {
        g_controllerInitInProgress = false;
        g_controllerInitFailed = true;
        g_controllerInitLastError = err;
        logInitLifecycle("after_platform_failure");
        return err;
    }
    const char *failureStep = "controller_client_init";
    const char *failureReason = "operational_keystore";
    const bool timeValid = control_time_has_valid_utc();
    ESP_LOGI(TAG, "time_valid_utc=%s before_controller_init", timeValid ? "yes" : "no");
    if (!timeValid) {
        ESP_LOGW(TAG, "controller_init_context time_synced=no");
    }
    control_heap_log("before_controller_client_init");
    if (!controllerLargestInternalHeapReady()) {
        ESP_LOGW(TAG, "low_heap_preflight_warning largest_internal=%u",
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        if (early) {
            ESP_LOGW(TAG, "attempting_controller_init_despite_warning=yes");
        } else {
            err = ESP_ERR_NO_MEM;
            g_status = MatterCommissioningStatus::FAILED;
            g_lastResult.status = g_status;
            g_lastResult.error = err;
            g_lastResult.errorText = "Mémoire interne insuffisante pour Matter";
            ESP_LOGE(TAG, "controller_init_failed err=%s step=controller_client_init", esp_err_to_name(err));
            (void)logHeapOnInitFailure();
            ESP_LOGE(TAG, "controller_init_failed reason=operational_keystore_low_internal_heap");
            g_controllerInitInProgress = false;
            g_controllerInitFailed = true;
            g_controllerInitLastError = err;
            setControllerState(MatterControllerState::FailedLowHeap);
            logInitLifecycle("after_init_failure");
            return err;
        }
    }

    {
        // CHIP controller APIs must be called while holding the CHIP stack lock.
        // Without this, CHIP asserts in SystemLayerImplFreeRTOS::CancelTimer().
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        chip::Credentials::DeviceAttestationVerifier *attestationVerifier = configureAttestationVerifier();
        auto &client = esp_matter::controller::matter_controller_client::get_instance();
        if (!g_controllerClientInitialized) {
            ESP_LOGI(TAG, "init_step=controller_client_init early=%s", early ? "yes" : "no");
            char controllerNodeBuf[24];
            char fabricBuf[24];
            char savedNodeBuf[24];
            formatMatterId(CONTROL_CONTROLLER_NODE_ID, controllerNodeBuf, sizeof(controllerNodeBuf));
            formatMatterId(CONTROL_CONTROLLER_FABRIC_ID, fabricBuf, sizeof(fabricBuf));
            formatMatterId(g_savedNode.node_id, savedNodeBuf, sizeof(savedNodeBuf));
            ESP_LOGI(TAG, "controller_init_args controller_node_id=%s fabric_id=%s listen_port=%u",
                     controllerNodeBuf, fabricBuf, static_cast<unsigned>(CONTROL_CONTROLLER_LISTEN_PORT));
            ESP_LOGI(TAG, "saved_remote_node_id=%s", savedNodeBuf);
            err = client.init(CONTROL_CONTROLLER_NODE_ID, CONTROL_CONTROLLER_FABRIC_ID,
                              CONTROL_CONTROLLER_LISTEN_PORT);
            control_heap_log("after_controller_client_init");
            logEspStepResult("controller_client_init", err);
            ESP_LOGI(TAG, "controller_client init result=%s code=%d", esp_err_to_name(err), static_cast<int>(err));
            ESP_LOGI(TAG, "device_controller_factory_init result=%s", esp_err_to_name(err));
            if (err == ESP_OK) {
                g_controllerClientInitialized = true;
            }
        } else {
            err = ESP_OK;
            ESP_LOGI(TAG, "init_step=controller_client_init skipped already_initialized=yes");
        }
        if (err == ESP_OK && !g_commissionerSetupDone) {
            failureStep = "setup_commissioner";
            failureReason = "setup_commissioner";
            ESP_LOGI(TAG, "init_step=setup_commissioner");
            ESP_LOGI(TAG, "credentials_issuer_init start issuer=ExampleOperationalCredentialsIssuer");
            char controllerNodeBuf[24];
            char fabricBuf[24];
            formatMatterId(CONTROL_CONTROLLER_NODE_ID, controllerNodeBuf, sizeof(controllerNodeBuf));
            formatMatterId(CONTROL_CONTROLLER_FABRIC_ID, fabricBuf, sizeof(fabricBuf));
            ESP_LOGI(TAG, "noc_chain_generation_start node_id=%s fabric_id=%s",
                     controllerNodeBuf, fabricBuf);
            err = client.setup_commissioner();
            logEspStepResult("setup_commissioner", err);
            ESP_LOGI(TAG, "setup_commissioner result=%s code=%d", esp_err_to_name(err), static_cast<int>(err));
            ESP_LOGI(TAG, "credentials_issuer_init result=%s", esp_err_to_name(err));
            ESP_LOGI(TAG, "noc_chain_generation_done result=%s", esp_err_to_name(err));
            if (err == ESP_OK) {
                g_commissionerSetupDone = true;
                client.get_commissioner()->SetDeviceAttestationVerifier(attestationVerifier);
            }
        } else if (err == ESP_OK) {
            ESP_LOGI(TAG, "init_step=setup_commissioner skipped already_ready=yes");
            ESP_LOGI(TAG, "credentials_issuer_init result=ESP_OK already_initialized=yes");
            ESP_LOGI(TAG, "noc_chain_generation_done result=ESP_OK already_initialized=yes");
            client.get_commissioner()->SetDeviceAttestationVerifier(attestationVerifier);
        }
        if (err == ESP_OK) {
            dumpLocalStorageSummaryLocked();
        }
    }

    if (err != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = err;
        g_lastResult.errorText = std::string("Initialisation Matter échouée: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "controller_init_failed err=%s step=%s", esp_err_to_name(err), failureStep);
        if (g_platformStarted) {
            ESP_LOGW(TAG, "platform_started_but_controller_failed=yes");
            ESP_LOGW(TAG, "dns_sd_update_after_failed_controller skipped=no_safe_api");
        }
        const bool heapLow = logHeapOnInitFailure();
        if (!heapLow) {
            if (failureReason == std::string("operational_keystore")) {
                ESP_LOGE(TAG, "controller_init_failed reason=operational_keystore_storage_error err=%s",
                         esp_err_to_name(err));
                g_lastResult.errorText = "Stockage opérationnel Matter indisponible";
                setControllerState(MatterControllerState::FailedStorage);
            } else {
                ESP_LOGE(TAG, "controller_init_failed reason=%s err=%s", failureReason, esp_err_to_name(err));
                setControllerState(MatterControllerState::FailedUnknown);
            }
        } else if (failureReason == std::string("operational_keystore")) {
            g_lastResult.errorText = "Mémoire interne insuffisante pour Matter";
            ESP_LOGE(TAG, "controller_init_failed reason=operational_keystore_low_internal_heap");
            setControllerState(MatterControllerState::FailedLowHeap);
        } else {
            ESP_LOGE(TAG, "controller_init_failed reason=operational_keystore_storage_error");
            g_lastResult.errorText = "Stockage opérationnel Matter indisponible";
            setControllerState(MatterControllerState::FailedStorage);
        }
        g_controllerInitInProgress = false;
        g_controllerInitFailed = true;
        g_controllerInitLastError = err;
        logInitLifecycle("after_init_failure");
        return err;
    }

    esp_matter::controller::pairing_command_callbacks_t callbacks = {
        .pase_callback = pase_callback,
        .commissioning_success_callback = commissioning_success_callback,
        .commissioning_failure_callback = commissioning_failure_callback,
    };
    esp_matter::controller::pairing_command::get_instance().set_callbacks(callbacks);

    g_status = MatterCommissioningStatus::READY;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_OK;
    g_controllerInitialized = true;
    g_controllerInitInProgress = false;
    g_controllerInitFailed = false;
    g_controllerInitLastError = ESP_OK;
    setControllerState(MatterControllerState::ControllerReady);
    ESP_LOGI(TAG, "controller_init ok mode=official_esp_matter_controller");
    if (early) {
        control_heap_log("after_early_matter_init");
    }
    logInitLifecycle("after_init_success");
#elif CONFIG_CONTROL_MATTER_CONTROLLER
    g_status = MatterCommissioningStatus::NOT_SUPPORTED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_ERR_NOT_SUPPORTED;
    g_lastResult.errorText =
        "ESP-Matter controller SDK not linked in this build. "
        "Commissioning is intentionally disabled unless the experimental "
        "ESP-Matter integration is completed.";
    ESP_LOGW(TAG, "controller_stub active: %s", safeText(g_lastResult.errorText));
#else
    g_status = MatterCommissioningStatus::NOT_SUPPORTED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_ERR_NOT_SUPPORTED;
    g_lastResult.errorText = "Matter controller disabled in Kconfig.";
    ESP_LOGW(TAG, "controller disabled in Kconfig");
#endif

    ESP_LOGI(TAG, "init_result=%s", esp_err_to_name(g_lastResult.error));
    ESP_LOGI(TAG, "heap_after_user_init internal=%u psram=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    ESP_LOGI(TAG, "largest_free_block_after internal=%u psram=%u",
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    return g_lastResult.error;
}

esp_err_t matter_controller_early_init() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    if (!sta || esp_netif_get_ip_info(sta, &ip) != ESP_OK || ip.ip.addr == 0) {
        ESP_LOGW(TAG, "early_init_skipped reason=wifi_not_connected");
        return ESP_ERR_INVALID_STATE;
    }
    return matter_controller_init_internal(true, false);
#else
    return matter_controller_init();
#endif
}

esp_err_t matter_controller_init() {
    return matter_controller_init_internal(false, true);
}

bool matter_controller_is_ready() {
    return g_status == MatterCommissioningStatus::READY
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
        && g_controllerState == MatterControllerState::ControllerReady
#endif
        ;
}

bool matter_controller_has_saved_node() {
    MatterSavedNode node;
    std::string reason;
    return matter_saved_state_load_and_validate(node, reason) == MatterSavedState::Valid;
}

esp_err_t matter_controller_load_saved_node(MatterSavedNode &out) {
    std::string reason;
    const MatterSavedState state = matter_saved_state_load_and_validate(out, reason);
    if (state == MatterSavedState::Valid) return ESP_OK;
    out = g_savedNode;
    return state == MatterSavedState::None ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_STATE;
}

esp_err_t matter_controller_validate_saved_node_fabric() {
    MatterSavedNode node;
    std::string reason;
    const MatterSavedState state = matter_saved_state_load_and_validate(node, reason);
    ESP_LOGI(TAG, "fabric_storage_check start");
    if (state == MatterSavedState::None) {
        ESP_LOGW(TAG, "saved_node metadata found=no");
        ESP_LOGW(TAG, "fabric_storage_check result=invalid");
        return ESP_ERR_NOT_FOUND;
    }
    if (state == MatterSavedState::Invalid) {
        ESP_LOGW(TAG, "controller_init_skipped reason=invalid_saved_metadata");
        g_postResult.status = MatterPostCommissioningStatus::SAVED_NODE_INVALID;
        g_postResult.error = ESP_ERR_INVALID_STATE;
        g_postResult.errorText = reason == "remote_node_id_zero"
            ? "Node ID distant manquant"
            : "Réinitialisation nécessaire";
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
        setControllerState(MatterControllerState::FailedInvalidMetadata);
#endif
        ESP_LOGW(TAG, "fabric_storage_check result=invalid");
        return ESP_ERR_INVALID_STATE;
    }
    char nodeBuf[24];
    formatMatterId(node.node_id, nodeBuf, sizeof(nodeBuf));
    ESP_LOGI(TAG, "saved_node metadata found=yes node_id=%s", nodeBuf);
    ESP_LOGI(TAG, "saved_remote_node_id=%s", nodeBuf);
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    esp_err_t err = matter_controller_early_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fabric_count unavailable");
        ESP_LOGW(TAG, "fabric_storage_check result=invalid");
        return err;
    }
    esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
    auto &client = esp_matter::controller::matter_controller_client::get_instance();
    chip::FabricIndex fabricIndex = client.get_fabric_index();
    uint8_t fabricCount = 0;
    const chip::FabricInfo *fabric = nullptr;
    auto *commissioner = client.get_commissioner();
    if (commissioner && commissioner->GetFabricTable()) {
        fabricCount = commissioner->GetFabricTable()->FabricCount();
        fabric = commissioner->GetFabricTable()->FindFabricWithIndex(fabricIndex);
    }
    ESP_LOGI(TAG, "fabric_count=%u", static_cast<unsigned>(fabricCount));
    ESP_LOGI(TAG, "fabric_index=%u", static_cast<unsigned>(fabricIndex));
    if (fabric) {
        char restoredNodeBuf[24];
        formatMatterId(fabric->GetNodeId(), restoredNodeBuf, sizeof(restoredNodeBuf));
        ESP_LOGI(TAG, "restored_fabric_node_id=%s", restoredNodeBuf);
    } else {
        ESP_LOGW(TAG, "restored_fabric_node_id unavailable");
    }
    const bool valid = fabricIndex != chip::kUndefinedFabricIndex && fabricCount > 0 && fabric != nullptr &&
                       (g_savedNode.fabric_index == 0 || g_savedNode.fabric_index == fabricIndex);
    ESP_LOGI(TAG, "fabric_storage_check result=%s", valid ? "valid" : "invalid");
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
#else
    ESP_LOGW(TAG, "fabric_count unavailable");
    ESP_LOGW(TAG, "fabric_storage_check result=invalid");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void matter_controller_dump_local_storage_summary() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    if (g_platformStarted) {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        dumpLocalStorageSummaryLocked();
        return;
    }
    dumpLocalStorageSummaryLocked();
#else
    ensureSavedNodeLoaded();
    char controllerNodeBuf[24];
    char remoteNodeBuf[24];
    formatMatterId(CONTROL_CONTROLLER_NODE_ID, controllerNodeBuf, sizeof(controllerNodeBuf));
    formatMatterId(g_savedNode.node_id, remoteNodeBuf, sizeof(remoteNodeBuf));
    ESP_LOGI(TAG, "local_storage_summary fabric_exists=no");
    ESP_LOGI(TAG, "local_storage_summary controller_node_id=%s", controllerNodeBuf);
    ESP_LOGI(TAG, "local_storage_summary fabric_id=0x0");
    ESP_LOGI(TAG, "local_storage_summary compressed_fabric_id=0x0");
    ESP_LOGI(TAG, "local_storage_summary remote_node_id=%s", remoteNodeBuf);
#endif
}

uint64_t matter_controller_get_preferred_remote_node_id() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    const uint64_t nodeId = g_previousSendNocFabricConflict
        ? CONTROL_FABRIC_CONFLICT_REMOTE_NODE_ID
        : CONTROL_PRIMARY_REMOTE_NODE_ID;
    char nodeBuf[24];
    formatMatterId(nodeId, nodeBuf, sizeof(nodeBuf));
    ESP_LOGI(TAG, "preferred_remote_node_id=%s", nodeBuf);
    ESP_LOGI(TAG, "previous_send_noc_fabric_conflict=%s",
             g_previousSendNocFabricConflict ? "yes" : "no");
    return nodeId;
#else
    char nodeBuf[24];
    formatMatterId(CONTROL_PRIMARY_REMOTE_NODE_ID, nodeBuf, sizeof(nodeBuf));
    ESP_LOGI(TAG, "preferred_remote_node_id=%s", nodeBuf);
    ESP_LOGI(TAG, "previous_send_noc_fabric_conflict=no");
    return CONTROL_PRIMARY_REMOTE_NODE_ID;
#endif
}

void matter_controller_set_pending_commissioning_mode(bool multi_admin) {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    g_pendingCommissioningMode = multi_admin ? COMMISSIONING_MODE_MULTI_ADMIN : COMMISSIONING_MODE_FIRST_PAIRING;
#endif
    ESP_LOGI(TAG, "commissioning_mode_pending=%s", multi_admin ? "multi_admin" : "first_pairing");
}

esp_err_t clearNvsNamespace(const char *ns) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

esp_err_t matter_controller_reset_local_storage_preserve_wifi() {
    return matter_controller_reset_local_storage_complete_preserve_wifi();
}

esp_err_t matter_controller_reset_local_storage_complete_preserve_wifi() {
    ESP_LOGI(TAG, "reset_matter_local_complete_start");
    esp_err_t appErr = clearNvsNamespace(MATTER_APP_NVS_NAMESPACE);
    ESP_LOGI(TAG, "clear_app_metadata result=%s", esp_err_to_name(appErr));
    esp_err_t cacheErr = clearNvsNamespace(MATTER_DEVICE_CACHE_NVS_NAMESPACE);
    ESP_LOGI(TAG, "device_cache_clear result=%s", esp_err_to_name(cacheErr));

    MatterSavedNode empty;
    updateSavedNodeCache(MatterSavedState::None, empty, "");
    g_postResult = {};
    g_lastResult = {};
    g_status = MatterCommissioningStatus::IDLE;
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    g_pendingPreferredRemoteNodeId = 0;
    g_previousSendNocFabricConflict = false;
    g_attestationBypassObserved = false;
    g_controllerInitInProgress = false;
    g_controllerInitFailed = false;
    g_controllerInitLastError = ESP_OK;

    esp_err_t matterErr = ESP_OK;
    const char *namespaces[] = {
        MATTER_KVS_NVS_NAMESPACE,
        MATTER_CONFIG_NVS_NAMESPACE,
        MATTER_COUNTERS_NVS_NAMESPACE,
        MATTER_OPCERT_NVS_NAMESPACE,
    };
    for (const char *ns : namespaces) {
        esp_err_t nsErr = clearNvsNamespace(ns);
        ESP_LOGI(TAG, "clear_matter_namespace namespace=%s result=%s", ns, esp_err_to_name(nsErr));
        if (matterErr == ESP_OK && nsErr != ESP_OK) matterErr = nsErr;
    }
    ESP_LOGI(TAG, "clear_matter_fabric_storage result=%s", esp_err_to_name(matterErr));
    setControllerState(g_platformStarted ? MatterControllerState::PlatformStarted : MatterControllerState::NotStarted);
#else
    esp_err_t matterErr = ESP_ERR_NOT_SUPPORTED;
    ESP_LOGW(TAG, "clear_matter_fabric_storage result=%s", esp_err_to_name(matterErr));
#endif
    ESP_LOGI(TAG, "wifi_preserved=yes");
    ESP_LOGI(TAG, "reset_matter_local_complete_done");
    if (appErr != ESP_OK) return appErr;
    if (cacheErr != ESP_OK) return cacheErr;
    return matterErr;
}

esp_err_t matter_controller_forget_saved_node() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    MatterSavedNode node;
    std::string reason;
    const MatterSavedState state = matter_saved_state_load_and_validate(node, reason);
    (void)reason;
    if (state == MatterSavedState::Valid &&
        g_controllerInitialized && g_commissionerSetupDone) {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        esp_err_t unpairErr = esp_matter::controller::matter_controller_client::get_instance().unpair(node.node_id);
        ESP_LOGI(TAG, "saved_node unpair_remote_result=%s", esp_err_to_name(unpairErr));
    } else {
        ESP_LOGW(TAG, "saved_node unpair_remote_skipped reason=controller_factory_not_initialized_or_failed");
    }
#endif
    esp_err_t err = matter_controller_reset_local_storage_preserve_wifi();
    ESP_LOGI(TAG, "saved_node forget result=%s", esp_err_to_name(err));
    return err;
}

esp_err_t matter_controller_reset_controller_init_failure() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    if (g_controllerInitInProgress) {
        ESP_LOGW(TAG, "controller_init_retry_reset rejected=in_progress");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_controllerInitFailed) {
        ESP_LOGI(TAG, "controller_init_retry_reset previous_err=%s",
                 esp_err_to_name(g_controllerInitLastError == ESP_OK ? ESP_FAIL : g_controllerInitLastError));
    }
    g_controllerInitFailed = false;
    g_controllerInitLastError = ESP_OK;
    logInitLifecycle("retry_reset");
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
void matterDiscoveryWorker(void *) {
    ESP_LOGI(TAG, "worker_stack_size=%u task=discovery", static_cast<unsigned>(DISCOVERY_WORKER_STACK_SIZE));
    logCurrentTaskDiagnostics("discovery", "worker_start");
    MatterSavedNode savedNode;
    std::string reason;
    const MatterSavedState state = matter_saved_state_load_and_validate(savedNode, reason);
    g_postResult = {};
    g_postResult.status = MatterPostCommissioningStatus::DEVICE_DISCOVERY;
    g_postResult.savedNode = g_savedNode;

    if (state != MatterSavedState::Valid) {
        g_postResult.status = state == MatterSavedState::Invalid
            ? MatterPostCommissioningStatus::SAVED_NODE_INVALID
            : MatterPostCommissioningStatus::DISCOVERY_FAILED;
        g_postResult.error = state == MatterSavedState::Invalid ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
        g_postResult.errorText = state == MatterSavedState::Invalid
            ? (reason == "remote_node_id_zero" ? "Node ID distant manquant" : "Réinitialisation nécessaire")
            : "Aucun appareil Matter enregistré";
        if (state == MatterSavedState::Invalid) {
            ESP_LOGW(TAG, "controller_init_skipped reason=invalid_saved_metadata");
            setControllerState(MatterControllerState::FailedInvalidMetadata);
        }
        g_discoveryTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    char savedNodeBuf[24];
    formatMatterId(savedNode.node_id, savedNodeBuf, sizeof(savedNodeBuf));
    ESP_LOGI(TAG, "use_saved_node remote_node_id=%s", savedNodeBuf);
    ESP_LOGI(TAG, "discovery_refresh_start node_id=%s", savedNodeBuf);

    ESP_LOGI(TAG, "controller_ready=%s", matter_controller_is_ready() ? "yes" : "no");
    esp_err_t err = matter_controller_is_ready() ? ESP_OK : matter_controller_early_init();
    if (err != ESP_OK) {
        g_postResult.status = MatterPostCommissioningStatus::SAVED_NODE_INVALID;
        g_postResult.error = err;
        g_postResult.errorText = "Matter fabric corrompue";
        g_discoveryTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ReadWaitContext readCtx;
    err = readMatterAttribute(readCtx, savedNode.node_id, 0,
                              chip::app::Clusters::Descriptor::Id,
                              chip::app::Clusters::Descriptor::Attributes::PartsList::Id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "device_discovery_failed step=parts_list err=%s", esp_err_to_name(err));
        g_postResult.status = err == ESP_ERR_TIMEOUT ? MatterPostCommissioningStatus::CONNECTION_FAILED
                                                     : MatterPostCommissioningStatus::DISCOVERY_FAILED;
        g_postResult.error = err;
        g_postResult.errorText = err == ESP_ERR_TIMEOUT
            ? "Connexion Matter impossible"
            : "Lecture appareils échouée";
        g_discoveryTask = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    for (uint16_t endpoint : readCtx.endpoints) {
        if (endpoint == 0) continue;
        ESP_LOGI(TAG, "endpoint_found endpoint=%u", endpoint);
        MatterDeviceItem item;
        item.endpoint_id = endpoint;

        ReadWaitContext serverCtx;
        if (readMatterAttribute(serverCtx, savedNode.node_id, endpoint,
                                chip::app::Clusters::Descriptor::Id,
                                chip::app::Clusters::Descriptor::Attributes::ServerList::Id) == ESP_OK) {
            for (uint32_t clusterId : serverCtx.cluster_ids) {
                if (clusterId == chip::app::Clusters::OnOff::Id) {
                    item.has_onoff = true;
                } else if (clusterId == chip::app::Clusters::LevelControl::Id) {
                    item.has_level_control = true;
                } else if (clusterId == chip::app::Clusters::ColorControl::Id) {
                    item.has_color_control = true;
                } else if (clusterId == chip::app::Clusters::Switch::Id) {
                    item.has_switch = true;
                }
            }
        }

        ReadWaitContext typeCtx;
        if (readMatterAttribute(typeCtx, savedNode.node_id, endpoint,
                                chip::app::Clusters::Descriptor::Id,
                                chip::app::Clusters::Descriptor::Attributes::DeviceTypeList::Id) == ESP_OK) {
            for (uint32_t deviceType : typeCtx.device_types) {
                if (item.device_type == 0) item.device_type = deviceType;
                if (isLightDeviceType(deviceType) || isKnownOnOffDeviceType(deviceType)) {
                    item.device_type = deviceType;
                    break;
                }
            }
        }

        if (item.has_color_control) item.primary_cluster_class = chip::app::Clusters::ColorControl::Id;
        else if (item.has_level_control) item.primary_cluster_class = chip::app::Clusters::LevelControl::Id;
        else if (item.has_onoff) item.primary_cluster_class = chip::app::Clusters::OnOff::Id;
        else if (item.has_switch) item.primary_cluster_class = chip::app::Clusters::Switch::Id;
        item.type_label = fallbackTypeLabel(item);

        ReadWaitContext reachableCtx;
        if (readMatterAttribute(reachableCtx, savedNode.node_id, endpoint,
                                chip::app::Clusters::BridgedDeviceBasicInformation::Id,
                                chip::app::Clusters::BridgedDeviceBasicInformation::Attributes::Reachable::Id,
                                6000) == ESP_OK) {
            item.reachable_known = true;
            item.reachable = reachableCtx.bool_value;
        }

        if (item.has_onoff) {
            ReadWaitContext onoffCtx;
            if (readMatterAttribute(onoffCtx, savedNode.node_id, endpoint,
                                    chip::app::Clusters::OnOff::Id,
                                    chip::app::Clusters::OnOff::Attributes::OnOff::Id) == ESP_OK) {
                item.onoff_state_known = true;
                item.onoff_state = onoffCtx.bool_value;
                ESP_LOGI(TAG, "read_onoff endpoint=%u state=%s", endpoint, item.onoff_state ? "on" : "off");
            }
        }

        item.last_seen_unix_or_boot_counter = timestampNow();
        (void)tryReadEndpointName(savedNode.node_id, item);
        if (!isMainListVisible(item)) {
            ESP_LOGI(TAG, "endpoint_classified endpoint=%u type=%s has_onoff=%s has_switch=%s name=\"%s\" visible=diagnostic_only",
                     endpoint, item.type_label.c_str(), item.has_onoff ? "yes" : "no",
                     item.has_switch ? "yes" : "no", item.name.c_str());
            continue;
        }
        ESP_LOGI(TAG, "endpoint_classified endpoint=%u type=%s has_onoff=%s has_switch=%s name=\"%s\"",
                 endpoint, item.type_label.c_str(), item.has_onoff ? "yes" : "no",
                 item.has_switch ? "yes" : "no", item.name.c_str());
        g_postResult.devices.push_back(item);
    }

    std::stable_sort(g_postResult.devices.begin(), g_postResult.devices.end(),
                     [](const MatterDeviceItem &a, const MatterDeviceItem &b) {
                         if (a.has_onoff != b.has_onoff) return a.has_onoff > b.has_onoff;
                         if (a.has_switch != b.has_switch) return a.has_switch < b.has_switch;
                         return a.endpoint_id < b.endpoint_id;
                     });
    (void)saveDeviceCache(g_postResult.devices);

    g_postResult.status = MatterPostCommissioningStatus::DEVICE_LIST;
    g_postResult.error = ESP_OK;
    control_heap_log("after_device_discovery");
    ESP_LOGI(TAG, "discovery_refresh_done count=%u", static_cast<unsigned>(g_postResult.devices.size()));
    ESP_LOGI(TAG, "worker_stack_high_water=%u task=discovery",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    g_discoveryTask = nullptr;
    vTaskDelete(nullptr);
}

struct CommandWorkerArgs {
    uint64_t node_id;
    uint16_t endpoint_id;
    bool on;
};

void matterCommandWorker(void *arg) {
    ESP_LOGI(TAG, "worker_stack_size=%u task=command", static_cast<unsigned>(COMMAND_WORKER_STACK_SIZE));
    logCurrentTaskDiagnostics("command", "worker_start");
    auto *args = static_cast<CommandWorkerArgs *>(arg);
    const uint32_t commandId = args->on
        ? chip::app::Clusters::OnOff::Commands::On::Id
        : chip::app::Clusters::OnOff::Commands::Off::Id;
    char nodeBuf[24];
    formatMatterId(args->node_id, nodeBuf, sizeof(nodeBuf));
    ESP_LOGI(TAG, "%s node_id=%s endpoint=%u",
             args->on ? "command_on" : "command_off",
             nodeBuf, args->endpoint_id);
    g_postResult.status = MatterPostCommissioningStatus::COMMAND_IN_PROGRESS;
    g_postResult.activeEndpoint = args->endpoint_id;
    esp_err_t err = matter_controller_is_ready() ? ESP_OK : matter_controller_early_init();
    if (err == ESP_OK) {
        err = sendOnOffCommandSync(args->node_id, args->endpoint_id, commandId);
    }
    if (err == ESP_OK) {
        bool state = false;
        esp_err_t readErr = matter_controller_read_onoff(args->node_id, args->endpoint_id, state);
        g_postResult.status = MatterPostCommissioningStatus::COMMAND_SUCCESS;
        g_postResult.error = ESP_OK;
        g_postResult.errorText = readErr == ESP_OK ? "" : "Commande envoyée; État non relu";
        g_postResult.commandStateKnown = readErr == ESP_OK;
        g_postResult.commandState = state;
        for (auto &item : g_postResult.devices) {
            if (item.endpoint_id == args->endpoint_id && readErr == ESP_OK) {
                item.onoff_state_known = true;
                item.onoff_state = state;
            }
        }
        (void)saveDeviceCache(g_postResult.devices);
        ESP_LOGI(TAG, "command_result endpoint=%u result=ok", args->endpoint_id);
    } else {
        g_postResult.status = MatterPostCommissioningStatus::COMMAND_FAILED;
        g_postResult.error = err;
        g_postResult.errorText = "Commande échouée";
        ESP_LOGW(TAG, "command_result endpoint=%u result=%s", args->endpoint_id, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "worker_stack_high_water=%u task=command",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    delete args;
    g_commandTask = nullptr;
    vTaskDelete(nullptr);
}
#endif

esp_err_t matter_controller_commission_with_manual_code(
    const std::string &normalizedCode,
    uint64_t preferredNodeId) {
    char preferredNodeBuf[24];
    formatMatterId(preferredNodeId, preferredNodeBuf, sizeof(preferredNodeBuf));
    ESP_LOGI(TAG, "commissioning_start code_len=%u node_id=%s",
             static_cast<unsigned>(normalizedCode.size()), preferredNodeBuf);

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    g_pendingPreferredRemoteNodeId = preferredNodeId;
    logSetupPayloadDetails(normalizedCode);
    ESP_LOGI(TAG, "pairing_requires_controller_ready");
    ESP_LOGI(TAG, "controller_ready=%s", matter_controller_is_ready() ? "yes" : "no");
    if (!matter_controller_is_ready()) {
        esp_err_t initErr = matter_controller_early_init();
        if (initErr != ESP_OK) return initErr;
    }
    matter_controller_dump_local_storage_summary();
    if (g_status == MatterCommissioningStatus::COMMISSIONING) {
        ESP_LOGW(TAG, "commissioning rejected: already running");
        return ESP_ERR_INVALID_STATE;
    }

    g_status = MatterCommissioningStatus::COMMISSIONING;
    g_lastResult = {};
    g_lastResult.status = g_status;
    g_lastResult.nodeId = preferredNodeId;
    g_lastResult.dev_insecure_mode =
#if CONFIG_CONTROL_MATTER_ALLOW_UNTRUSTED_ATTESTATION
        true;
#else
        false;
#endif
    g_attestationBypassObserved = false;

    ESP_LOGI(TAG, "pairing_code_start");
    ESP_LOGI(TAG, "init_step=pairing_code enter node_id=%s code_len=%u",
             preferredNodeBuf,
             static_cast<unsigned>(normalizedCode.size()));
    esp_err_t err = ESP_OK;
    {
        // CHIP controller APIs must be called while holding the CHIP stack lock.
        // Without this, CHIP asserts in SystemLayerImplFreeRTOS::CancelTimer().
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        ESP_LOGI(TAG, "chip_lock=held step=pairing_code");
        err = esp_matter::controller::pairing_code(preferredNodeId, normalizedCode.c_str());
    }
    logEspStepResult("pairing_code", err);
    ESP_LOGI(TAG, "pairing_code result=%s code=%d", esp_err_to_name(err), static_cast<int>(err));
    if (err != ESP_OK) {
        g_status = MatterCommissioningStatus::FAILED;
        g_lastResult.status = g_status;
        g_lastResult.error = err;
        g_lastResult.errorText = std::string("Démarrage commissioning échoué: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "commissioning_start_failed err=%s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "commissioning_async_started node_id=%s method=pairing_code_on_network",
             preferredNodeBuf);
    return ESP_OK;
#else
    g_status = MatterCommissioningStatus::NOT_SUPPORTED;
    g_lastResult.status = g_status;
    g_lastResult.error = ESP_ERR_NOT_SUPPORTED;
    g_lastResult.nodeId = preferredNodeId;
    g_lastResult.errorText =
        "Commissioning disabled in this build. Enable "
        "CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL only after linking "
        "official ESP-Matter.";
    ESP_LOGW(TAG, "commissioning_disabled err=%s text=%s",
             esp_err_to_name(g_lastResult.error), safeText(g_lastResult.errorText));
    return g_lastResult.error;
#endif
}

esp_err_t matter_controller_get_last_result(MatterCommissioningResult &out) {
    out = g_lastResult;
    return out.error;
}

esp_err_t matter_controller_start_device_discovery() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    MatterSavedNode node;
    std::string reason;
    const MatterSavedState state = matter_saved_state_load_and_validate(node, reason);
    if (state != MatterSavedState::Valid) {
        g_postResult = {};
        g_postResult.status = state == MatterSavedState::Invalid
            ? MatterPostCommissioningStatus::SAVED_NODE_INVALID
            : MatterPostCommissioningStatus::DISCOVERY_FAILED;
        g_postResult.error = state == MatterSavedState::Invalid ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
        g_postResult.savedNode = g_savedNode;
        g_postResult.errorText = state == MatterSavedState::Invalid
            ? (reason == "remote_node_id_zero" ? "Node ID distant manquant" : "Réinitialisation nécessaire")
            : "Aucun appareil Matter enregistré";
        if (state == MatterSavedState::Invalid) {
            ESP_LOGW(TAG, "controller_init_skipped reason=invalid_saved_metadata");
            setControllerState(MatterControllerState::FailedInvalidMetadata);
        }
        return g_postResult.error;
    }
    esp_err_t validErr = matter_controller_validate_saved_node_fabric();
    if (validErr != ESP_OK) {
        g_postResult = {};
        g_postResult.status = MatterPostCommissioningStatus::SAVED_NODE_INVALID;
        g_postResult.savedNode = g_savedNode;
        g_postResult.error = validErr;
        g_postResult.errorText = "Matter fabric corrompue";
        return ESP_ERR_INVALID_STATE;
    }
    if (g_discoveryTask != nullptr) return ESP_ERR_INVALID_STATE;
    g_postResult.status = MatterPostCommissioningStatus::DEVICE_DISCOVERY;
    g_postResult.savedNode = g_savedNode;
    const BaseType_t created = xTaskCreate(
        matterDiscoveryWorker,
        "control_matter_discovery",
        DISCOVERY_WORKER_STACK_SIZE,
        nullptr,
        MATTER_DISCOVERY_WORKER_PRIORITY,
        &g_discoveryTask);
    if (created != pdPASS) {
        g_discoveryTask = nullptr;
        g_postResult.status = MatterPostCommissioningStatus::DISCOVERY_FAILED;
        g_postResult.error = ESP_ERR_NO_MEM;
        g_postResult.errorText = "Lecture appareils échouée";
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    g_postResult.status = MatterPostCommissioningStatus::DISCOVERY_FAILED;
    g_postResult.error = ESP_ERR_NOT_SUPPORTED;
    g_postResult.errorText = "Matter désactivé dans cette build";
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t matter_controller_get_post_commissioning_result(MatterPostCommissioningResult &out) {
    ensureSavedNodeLoaded();
    out = g_postResult;
    out.savedNode = g_savedNode;
    return out.error;
}

esp_err_t matter_controller_load_device_cache(MatterPostCommissioningResult &out) {
    out = {};
    ensureSavedNodeLoaded();
    out.savedNode = g_savedNode;
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    if (g_savedState != MatterSavedState::Valid) {
        out.status = g_savedState == MatterSavedState::Invalid
            ? MatterPostCommissioningStatus::SAVED_NODE_INVALID
            : MatterPostCommissioningStatus::IDLE;
        out.error = g_savedState == MatterSavedState::Invalid ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
        out.errorText = g_savedStateReason;
        ESP_LOGI(TAG, "device_cache_load count=0 result=%s reason=no_valid_saved_node", esp_err_to_name(out.error));
        return out.error;
    }
    const esp_err_t err = loadDeviceCache(out.devices);
    out.status = out.devices.empty() ? MatterPostCommissioningStatus::IDLE
                                     : MatterPostCommissioningStatus::DEVICE_LIST;
    out.error = err;
    if (err == ESP_OK) {
        g_postResult = out;
    }
    return err;
#else
    out.error = ESP_ERR_NOT_SUPPORTED;
    ESP_LOGI(TAG, "device_cache_load count=0 result=%s", esp_err_to_name(out.error));
    return out.error;
#endif
}

esp_err_t matter_controller_clear_device_cache() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    const esp_err_t err = clearNvsNamespace(MATTER_DEVICE_CACHE_NVS_NAMESPACE);
    ESP_LOGI(TAG, "device_cache_clear result=%s", esp_err_to_name(err));
    return err;
#else
    ESP_LOGI(TAG, "device_cache_clear result=%s", esp_err_to_name(ESP_ERR_NOT_SUPPORTED));
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t matter_controller_read_onoff(uint64_t node_id, uint16_t endpoint_id, bool &out_state) {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    MatterSavedNode savedNode;
    std::string reason;
    if (matter_saved_state_load_and_validate(savedNode, reason) != MatterSavedState::Valid ||
        !isValidRemoteNodeId(node_id) || node_id != savedNode.node_id ||
        g_controllerState != MatterControllerState::ControllerReady) {
        ESP_LOGW(TAG, "read_onoff_rejected endpoint=%u reason=controller_not_ready_or_invalid_saved_metadata",
                 endpoint_id);
        return ESP_ERR_INVALID_STATE;
    }
    ReadWaitContext ctx;
    esp_err_t err = readMatterAttribute(ctx, node_id, endpoint_id,
                                        chip::app::Clusters::OnOff::Id,
                                        chip::app::Clusters::OnOff::Attributes::OnOff::Id);
    if (err == ESP_OK) {
        out_state = ctx.bool_value;
        ESP_LOGI(TAG, "read_onoff endpoint=%u state=%s", endpoint_id, out_state ? "on" : "off");
    }
    return err;
#else
    (void)node_id;
    (void)endpoint_id;
    out_state = false;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t matter_controller_send_on(uint64_t node_id, uint16_t endpoint_id) {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    MatterSavedNode savedNode;
    std::string reason;
    if (matter_saved_state_load_and_validate(savedNode, reason) != MatterSavedState::Valid ||
        !isValidRemoteNodeId(node_id) || node_id != savedNode.node_id) {
        ESP_LOGW(TAG, "command_start_rejected reason=invalid_saved_metadata_or_node_id");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_commandTask != nullptr) return ESP_ERR_INVALID_STATE;
    auto *args = new CommandWorkerArgs{node_id, endpoint_id, true};
    if (!args) return ESP_ERR_NO_MEM;
    const BaseType_t created = xTaskCreate(
        matterCommandWorker,
        "control_matter_cmd",
        COMMAND_WORKER_STACK_SIZE,
        args,
        MATTER_COMMAND_WORKER_PRIORITY,
        &g_commandTask);
    if (created != pdPASS) {
        delete args;
        g_commandTask = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    (void)node_id;
    (void)endpoint_id;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t matter_controller_send_off(uint64_t node_id, uint16_t endpoint_id) {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    MatterSavedNode savedNode;
    std::string reason;
    if (matter_saved_state_load_and_validate(savedNode, reason) != MatterSavedState::Valid ||
        !isValidRemoteNodeId(node_id) || node_id != savedNode.node_id) {
        ESP_LOGW(TAG, "command_start_rejected reason=invalid_saved_metadata_or_node_id");
        return ESP_ERR_INVALID_STATE;
    }
    if (g_commandTask != nullptr) return ESP_ERR_INVALID_STATE;
    auto *args = new CommandWorkerArgs{node_id, endpoint_id, false};
    if (!args) return ESP_ERR_NO_MEM;
    const BaseType_t created = xTaskCreate(
        matterCommandWorker,
        "control_matter_cmd",
        COMMAND_WORKER_STACK_SIZE,
        args,
        MATTER_COMMAND_WORKER_PRIORITY,
        &g_commandTask);
    if (created != pdPASS) {
        delete args;
        g_commandTask = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
#else
    (void)node_id;
    (void)endpoint_id;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

const char *matter_post_commissioning_status_name(MatterPostCommissioningStatus status) {
    switch (status) {
        case MatterPostCommissioningStatus::IDLE: return "IDLE";
        case MatterPostCommissioningStatus::DEVICE_DISCOVERY: return "DEVICE_DISCOVERY";
        case MatterPostCommissioningStatus::DEVICE_LIST: return "DEVICE_LIST";
        case MatterPostCommissioningStatus::COMMAND_IN_PROGRESS: return "COMMAND_IN_PROGRESS";
        case MatterPostCommissioningStatus::COMMAND_SUCCESS: return "COMMAND_SUCCESS";
        case MatterPostCommissioningStatus::COMMAND_FAILED: return "COMMAND_FAILED";
        case MatterPostCommissioningStatus::DISCOVERY_FAILED: return "DISCOVERY_FAILED";
        case MatterPostCommissioningStatus::CONNECTION_FAILED: return "CONNECTION_FAILED";
        case MatterPostCommissioningStatus::SAVED_NODE_INVALID: return "SAVED_NODE_INVALID";
    }
    return "UNKNOWN";
}
