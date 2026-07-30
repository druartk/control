#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

constexpr uint16_t CONTROL_UI_CONFIG_SCHEMA_VERSION = 1;
constexpr size_t CONTROL_UI_CONFIG_MAX_ROOMS = 8;
constexpr size_t CONTROL_UI_CONFIG_MAX_BINDINGS = 48;

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

esp_err_t control_ui_config_load(UiConfig &out);
esp_err_t control_ui_config_save(const UiConfig &config);
esp_err_t control_ui_config_clear();
