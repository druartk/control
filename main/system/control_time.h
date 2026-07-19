#pragma once

#include <cstdint>

#include "esp_err.h"

bool control_time_is_synced();
bool control_time_has_valid_utc();
esp_err_t control_time_start_sntp_once();
esp_err_t control_time_wait_synced(uint32_t timeout_ms);
