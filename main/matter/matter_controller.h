#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"

enum class MatterCommissioningStatus {
    IDLE,
    INITIALIZING,
    READY,
    COMMISSIONING,
    COMMISSIONED,
    FAILED,
    NOT_SUPPORTED,
};

struct MatterCommissioningResult {
    MatterCommissioningStatus status = MatterCommissioningStatus::IDLE;
    esp_err_t error = ESP_OK;
    std::string errorText;
    uint64_t nodeId = 0;
    uint16_t fabricIndex = 0;
    uint16_t endpointCount = 0;
    uint16_t vendorId = 0;
    uint16_t productId = 0;
    bool attestation_verified = false;
    bool attestation_bypassed = false;
    bool dev_insecure_mode = false;
    std::string failure_stage;
    std::string failure_reason;
};

struct MatterSavedNode {
    bool commissioned = false;
    uint64_t node_id = 0;
    uint16_t fabric_index = 0;
    uint64_t controller_node_id = 0;
    uint64_t preferred_remote_node_id = 0;
    uint8_t commissioning_mode = 0; // 1=first_pairing, 2=multi_admin
    std::string last_known_addr;
    std::string last_known_host;
    bool attestation_verified = false;
    bool attestation_bypassed = false;
    bool dev_insecure_mode = false;
    uint64_t commissioned_at_unix_or_boot_counter = 0;
};

struct MatterDeviceItem {
    uint16_t endpoint_id = 0;
    std::string name;
    std::string type_label;
    bool has_onoff = false;
    bool has_level_control = false;
    bool has_color_control = false;
    bool has_switch = false;
    bool reachable_known = false;
    bool reachable = false;
    bool onoff_state_known = false;
    bool onoff_state = false;
    uint32_t device_type = 0;
    uint32_t primary_cluster_class = 0;
    uint64_t last_seen_unix_or_boot_counter = 0;
};

enum class MatterSavedState {
    None,
    Valid,
    Invalid,
};

enum class MatterControllerState {
    NotStarted,
    PlatformStarted,
    ControllerReady,
    FailedInvalidMetadata,
    FailedStorage,
    FailedLowHeap,
    FailedUnknown,
};

enum class MatterPostCommissioningStatus {
    IDLE,
    DEVICE_DISCOVERY,
    DEVICE_LIST,
    COMMAND_IN_PROGRESS,
    COMMAND_SUCCESS,
    COMMAND_FAILED,
    DISCOVERY_FAILED,
    CONNECTION_FAILED,
    SAVED_NODE_INVALID,
};

struct MatterPostCommissioningResult {
    MatterPostCommissioningStatus status = MatterPostCommissioningStatus::IDLE;
    esp_err_t error = ESP_OK;
    std::string errorText;
    MatterSavedNode savedNode;
    std::vector<MatterDeviceItem> devices;
    uint16_t activeEndpoint = 0;
    bool commandStateKnown = false;
    bool commandState = false;
};

enum class MatterUiAction : uint8_t {
    SetOn,
    SetOff,
    Toggle,
};

struct MatterUiCommand {
    uint64_t node_id = 0;
    uint16_t endpoint_id = 0;
    MatterUiAction action = MatterUiAction::SetOff;
    uint32_t ui_device_id = 0;
    uint32_t batch_id = 0;
};

struct MatterUiResult {
    MatterUiCommand command;
    bool success = false;
    esp_err_t error = ESP_FAIL;
};

esp_err_t matter_controller_init();
esp_err_t matter_controller_early_init();
esp_err_t matter_controller_start_platform();
void matter_controller_log_boot_status();
bool matter_controller_is_ready();
MatterSavedState matter_saved_state_load_and_validate(MatterSavedNode &out, std::string &reason);
bool matter_controller_has_saved_node();
esp_err_t matter_controller_load_saved_node(MatterSavedNode &out);
esp_err_t matter_controller_forget_saved_node();
esp_err_t matter_controller_reset_local_storage_preserve_wifi();
esp_err_t matter_controller_reset_local_storage_complete_preserve_wifi();
esp_err_t matter_controller_reset_controller_init_failure();
void matter_controller_dump_local_storage_summary();
uint64_t matter_controller_get_preferred_remote_node_id();
void matter_controller_set_pending_commissioning_mode(bool multi_admin);
esp_err_t matter_controller_validate_saved_node_fabric();
esp_err_t matter_controller_commission_with_manual_code(
    const std::string &normalizedCode,
    uint64_t preferredNodeId);
esp_err_t matter_controller_get_last_result(MatterCommissioningResult &out);
esp_err_t matter_controller_start_device_discovery();
esp_err_t matter_controller_get_post_commissioning_result(MatterPostCommissioningResult &out);
esp_err_t matter_controller_load_device_cache(MatterPostCommissioningResult &out);
esp_err_t matter_controller_clear_device_cache();
void matter_controller_log_ui_endpoints(const MatterPostCommissioningResult &result);
esp_err_t matter_controller_ui_init();
esp_err_t matter_controller_ui_enqueue(const MatterUiCommand &command);
bool matter_controller_ui_poll_result(MatterUiResult &out);
esp_err_t matter_controller_send_on(uint64_t node_id, uint16_t endpoint_id);
esp_err_t matter_controller_send_off(uint64_t node_id, uint16_t endpoint_id);
esp_err_t matter_controller_read_onoff(uint64_t node_id, uint16_t endpoint_id, bool &out_state);
const char *matter_commissioning_status_name(MatterCommissioningStatus status);
const char *matter_post_commissioning_status_name(MatterPostCommissioningStatus status);
