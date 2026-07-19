#include "system/control_time.h"

#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

namespace {
constexpr const char *TAG = "CONTROL_TIME";
constexpr time_t MIN_REASONABLE_TIME = 1704067200; // 2024-01-01T00:00:00Z
bool g_sntpStarted = false;

void logCurrentUtc() {
    time_t now = 0;
    time(&now);
    struct tm utc {};
    gmtime_r(&now, &utc);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    ESP_LOGI(TAG, "time_sync_success utc=%s", buffer);
}
}

bool control_time_is_synced() {
    return control_time_has_valid_utc();
}

bool control_time_has_valid_utc() {
    time_t now = 0;
    time(&now);
    const bool valid = now >= MIN_REASONABLE_TIME;
    ESP_LOGI(TAG, "unix_time=%lld valid_utc=%s", static_cast<long long>(now), valid ? "yes" : "no");
    return valid;
}

esp_err_t control_time_start_sntp_once() {
    if (g_sntpStarted) return ESP_OK;
    ESP_LOGI(TAG, "sntp_start");
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("pool.ntp.org", "time.google.com", "time.cloudflare.com"));
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_netif_sntp_start();
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK;
    }
    if (err == ESP_OK) g_sntpStarted = true;
    ESP_LOGI(TAG, "sntp_start result=%s", esp_err_to_name(err));
    return err;
}

esp_err_t control_time_wait_synced(uint32_t timeout_ms) {
    if (control_time_is_synced()) {
        logCurrentUtc();
        return ESP_OK;
    }
    esp_err_t err = control_time_start_sntp_once();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "waiting_for_time_sync timeout_ms=%u", static_cast<unsigned>(timeout_ms));
    err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
    if (err == ESP_OK || control_time_is_synced()) {
        logCurrentUtc();
        return ESP_OK;
    }
    ESP_LOGW(TAG, "time_sync_timeout");
    return err == ESP_ERR_TIMEOUT ? ESP_ERR_TIMEOUT : err;
}
