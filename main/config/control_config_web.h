#pragma once

#include <string>
#include "esp_err.h"

esp_err_t control_config_web_init();
std::string control_config_web_url();
const char *control_config_web_access_code();
