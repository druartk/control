#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

constexpr uint16_t CONTROL_UI_CONFIG_SCHEMA_VERSION = 1;
constexpr size_t CONTROL_UI_CONFIG_MAX_ROOMS = 16;
// The persistent UI limit matches the existing Matter device-cache capacity.
constexpr size_t CONTROL_UI_CONFIG_MAX_BINDINGS = 32;
constexpr size_t CONTROL_UI_CONFIG_MAX_CONFIGS = 4;
constexpr size_t CONTROL_UI_CONFIG_NAME_LEN = 32;

struct UiConfigRoom {
    uint32_t room_id = 0;
    char name[32] = {};
    uint8_t order = 0;
};

struct UiConfigBinding {
    uint64_t node_id = 0;
    uint16_t endpoint_id = 0;
    uint32_t room_id = 0;
    char custom_name[48] = {};
    uint8_t order = 0;
    bool configured = false;
};

struct UiConfig {
    uint16_t schema_version = CONTROL_UI_CONFIG_SCHEMA_VERSION;
    uint8_t room_count = 0;
    uint8_t binding_count = 0;
    UiConfigRoom rooms[CONTROL_UI_CONFIG_MAX_ROOMS]{};
    UiConfigBinding bindings[CONTROL_UI_CONFIG_MAX_BINDINGS]{};
};

struct UiNamedConfig {
    uint32_t config_id = 0;
    char name[CONTROL_UI_CONFIG_NAME_LEN] = {};
    uint32_t revision = 0;
    uint64_t modified_at = 0;
    UiConfig ui{};
};

struct UiConfigCollection {
    uint16_t schema_version = CONTROL_UI_CONFIG_SCHEMA_VERSION;
    uint8_t config_count = 0;
    uint32_t active_config_id = 0;
    UiNamedConfig configs[CONTROL_UI_CONFIG_MAX_CONFIGS]{};
};

esp_err_t control_ui_config_load(UiConfig &out);
esp_err_t control_ui_config_save(const UiConfig &config);
esp_err_t control_ui_config_clear();
esp_err_t control_ui_config_collection_load(UiConfigCollection &out);
esp_err_t control_ui_config_collection_save(const UiConfigCollection &configs);
