#pragma once

#include "esp_err.h"
#include "matter/matter_controller.h"

constexpr uint32_t CONTROL_CONFIG_UI_DEVICE_MARKER = 0xF0000000U;

esp_err_t control_config_serial_init();
// Called by the single HTTP server task; responses use the same bounded worker.
esp_err_t control_config_serial_web_request(const char *json, size_t length, char **response);
void control_config_serial_handle_matter_result(const MatterUiResult &result);
bool control_config_serial_take_config_update();
uint32_t control_config_serial_take_active_config_id();
