#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

struct WeatherCurrent {
    bool valid = false;
    int temperature_c = 0;
    int temp_min_c = 0;
    int temp_max_c = 0;
    int precipitation_percent = 0;
    int wind_kmh = 0;
    char condition[32] = {};
    char location[48] = {};
    uint64_t updated_at = 0;
};

struct WeatherForecastDay {
    bool valid = false;
    char label[16] = {};
    int temp_min_c = 0;
    int temp_max_c = 0;
    int precipitation_percent = 0;
    char condition[32] = {};
};

esp_err_t weather_init();
esp_err_t weather_load_cache();
esp_err_t weather_save_cache();
void weather_ensure_placeholder();
esp_err_t weather_refresh_async();
bool weather_poll_refresh_complete();
bool weather_refresh_in_progress();
bool weather_has_valid_cache();
const WeatherCurrent &weather_get_current();
const WeatherForecastDay *weather_get_forecast(size_t *count);
