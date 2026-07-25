#include "weather/weather_manager.h"

#include <cstring>
#include <ctime>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace {
constexpr const char *TAG = "CONTROL_WEATHER";
constexpr const char *NAMESPACE = "control_weather";
constexpr const char *ENDPOINT = ""; // Configure a provider endpoint when available.
constexpr size_t FORECAST_COUNT = 3;

WeatherCurrent current;
WeatherForecastDay forecast[FORECAST_COUNT];
TaskHandle_t task = nullptr;
volatile bool refreshing = false;
volatile bool completed = false;

void setText(char *dst, size_t size, const char *src) {
    if (!dst || size == 0) return;
    std::strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

uint64_t nowSeconds() {
    const time_t wall = time(nullptr);
    return wall > 100000 ? static_cast<uint64_t>(wall)
                         : static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL);
}

void makeMockSnapshot(WeatherCurrent &out, WeatherForecastDay *days) {
    out = {};
    out.valid = true;
    out.temperature_c = 18;
    out.temp_min_c = 12;
    out.temp_max_c = 21;
    out.precipitation_percent = 20;
    out.wind_kmh = 14;
    setText(out.condition, sizeof(out.condition), "Variable");
    setText(out.location, sizeof(out.location), "Localisation configuree");
    out.updated_at = nowSeconds();
    const char *labels[] = {"Aujourd'hui", "Demain", "J+2"};
    const int highs[] = {21, 20, 22};
    const int lows[] = {12, 11, 13};
    for (size_t i = 0; i < FORECAST_COUNT; ++i) {
        days[i] = {};
        days[i].valid = true;
        setText(days[i].label, sizeof(days[i].label), labels[i]);
        days[i].temp_min_c = lows[i];
        days[i].temp_max_c = highs[i];
        days[i].precipitation_percent = static_cast<int>(20 + i * 15);
        setText(days[i].condition, sizeof(days[i].condition), i == 1 ? "Eclaircies" : "Variable");
    }
}

bool parseProviderJson(const char *body, WeatherCurrent &out, WeatherForecastDay *days) {
    cJSON *root = cJSON_Parse(body);
    if (!root) return false;
    cJSON *temp = cJSON_GetObjectItem(root, "temperature_c");
    cJSON *location = cJSON_GetObjectItem(root, "location");
    if (!cJSON_IsNumber(temp)) {
        cJSON_Delete(root);
        return false;
    }
    out = {};
    out.valid = true;
    out.temperature_c = temp->valueint;
    out.temp_min_c = cJSON_GetObjectItem(root, "temp_min_c") ? cJSON_GetObjectItem(root, "temp_min_c")->valueint : out.temperature_c;
    out.temp_max_c = cJSON_GetObjectItem(root, "temp_max_c") ? cJSON_GetObjectItem(root, "temp_max_c")->valueint : out.temperature_c;
    out.precipitation_percent = cJSON_GetObjectItem(root, "precipitation_percent") ? cJSON_GetObjectItem(root, "precipitation_percent")->valueint : 0;
    out.wind_kmh = cJSON_GetObjectItem(root, "wind_kmh") ? cJSON_GetObjectItem(root, "wind_kmh")->valueint : 0;
    setText(out.condition, sizeof(out.condition), cJSON_GetObjectItem(root, "condition") ? cJSON_GetObjectItem(root, "condition")->valuestring : "Inconnu");
    setText(out.location, sizeof(out.location), cJSON_IsString(location) ? location->valuestring : "Localisation");
    out.updated_at = nowSeconds();
    cJSON *daysJson = cJSON_GetObjectItem(root, "forecast");
    for (size_t i = 0; i < FORECAST_COUNT; ++i) days[i] = {};
    if (cJSON_IsArray(daysJson)) {
        size_t i = 0;
        cJSON *item = nullptr;
        cJSON_ArrayForEach(item, daysJson) {
            if (i >= FORECAST_COUNT || !cJSON_IsObject(item)) break;
            days[i].valid = true;
            setText(days[i].label, sizeof(days[i].label), cJSON_GetObjectItem(item, "label") ? cJSON_GetObjectItem(item, "label")->valuestring : "Jour");
            days[i].temp_min_c = cJSON_GetObjectItem(item, "temp_min_c") ? cJSON_GetObjectItem(item, "temp_min_c")->valueint : 0;
            days[i].temp_max_c = cJSON_GetObjectItem(item, "temp_max_c") ? cJSON_GetObjectItem(item, "temp_max_c")->valueint : 0;
            days[i].precipitation_percent = cJSON_GetObjectItem(item, "precipitation_percent") ? cJSON_GetObjectItem(item, "precipitation_percent")->valueint : 0;
            setText(days[i].condition, sizeof(days[i].condition), cJSON_GetObjectItem(item, "condition") ? cJSON_GetObjectItem(item, "condition")->valuestring : "Inconnu");
            ++i;
        }
    }
    cJSON_Delete(root);
    return true;
}

void refreshTask(void *) {
    ESP_LOGI(TAG, "refresh_start");
    if (ENDPOINT[0] != '\0') {
        esp_http_client_config_t config = {};
        config.url = ENDPOINT;
        config.timeout_ms = 5000;
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client) {
            const esp_err_t err = esp_http_client_perform(client);
            ESP_LOGI(TAG, "provider_request result=%s", esp_err_to_name(err));
            char body[2048] = {};
            if (err == ESP_OK && esp_http_client_read_response(client, body, sizeof(body) - 1) > 0) {
                if (parseProviderJson(body, current, forecast)) {
                    (void)weather_save_cache();
                    ESP_LOGI(TAG, "refresh_success location=%s temp=%d", current.location, current.temperature_c);
                    refreshing = false;
                    completed = true;
                    task = nullptr;
                    esp_http_client_cleanup(client);
                    vTaskDelete(nullptr);
                    return;
                }
            }
            esp_http_client_cleanup(client);
        }
    }
    makeMockSnapshot(current, forecast);
    const esp_err_t saveErr = weather_save_cache();
    if (saveErr == ESP_OK) {
        ESP_LOGI(TAG, "refresh_success location=%s temp=%d", current.location, current.temperature_c);
    } else {
        ESP_LOGW(TAG, "refresh_failed err=%s", esp_err_to_name(saveErr));
    }
    refreshing = false;
    completed = true;
    task = nullptr;
    vTaskDelete(nullptr);
}
}

esp_err_t weather_init() {
    ESP_LOGI(TAG, "init");
    return ESP_OK;
}

esp_err_t weather_load_cache() {
    current = {};
    std::memset(forecast, 0, sizeof(forecast));
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) {
        size_t size = sizeof(current);
        err = nvs_get_blob(handle, "current", &current, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        size = sizeof(forecast);
        if (err == ESP_OK) {
            esp_err_t forecastErr = nvs_get_blob(handle, "forecast", forecast, &size);
            if (forecastErr != ESP_OK && forecastErr != ESP_ERR_NVS_NOT_FOUND) err = forecastErr;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "cache_load valid=%s", weather_has_valid_cache() ? "yes" : "no");
    return err;
}

void weather_ensure_placeholder() {
    if (weather_has_valid_cache()) return;
    makeMockSnapshot(current, forecast);
    ESP_LOGI(TAG, "placeholder_ready source=mock");
}

esp_err_t weather_save_cache() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, "current", &current, sizeof(current));
    if (err == ESP_OK) err = nvs_set_blob(handle, "forecast", forecast, sizeof(forecast));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle) nvs_close(handle);
    ESP_LOGI(TAG, "cache_save result=%s", esp_err_to_name(err));
    return err;
}

esp_err_t weather_refresh_async() {
    if (refreshing) return ESP_ERR_INVALID_STATE;
    refreshing = true;
    completed = false;
    if (xTaskCreate(refreshTask, "weather_fetch", 4096, nullptr, 3, &task) != pdPASS) {
        refreshing = false;
        ESP_LOGW(TAG, "refresh_failed err=%s", esp_err_to_name(ESP_ERR_NO_MEM));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool weather_poll_refresh_complete() {
    const bool result = completed;
    completed = false;
    return result;
}

bool weather_refresh_in_progress() { return refreshing; }
bool weather_has_valid_cache() { return current.valid; }
const WeatherCurrent &weather_get_current() { return current; }
const WeatherForecastDay *weather_get_forecast(size_t *count) {
    if (count) *count = FORECAST_COUNT;
    return forecast;
}
