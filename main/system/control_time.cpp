#include "system/control_time.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"

namespace {
constexpr const char *TAG = "CONTROL_TIME";
constexpr time_t MIN_REASONABLE_TIME = 1704067200; // 2024-01-01T00:00:00Z
bool g_sntpStarted = false;

void formatUnixTime(time_t value, char *buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) return;
    buffer[0] = '\0';

    bool negative = value < 0;
    uint64_t remaining = 0;
    if (negative) {
        remaining = static_cast<uint64_t>(-(value + 1)) + 1U;
    } else {
        remaining = static_cast<uint64_t>(value);
    }

    char digits[24] = {};
    size_t digitCount = 0;
    do {
        digits[digitCount++] = static_cast<char>('0' + (remaining % 10U));
        remaining /= 10U;
    } while (remaining > 0 && digitCount < sizeof(digits));

    size_t out = 0;
    if (negative && out + 1 < bufferSize) {
        buffer[out++] = '-';
    }
    while (digitCount > 0 && out + 1 < bufferSize) {
        buffer[out++] = digits[--digitCount];
    }
    buffer[out] = '\0';
}

void formatUtc(time_t now, char *buffer, size_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) return;
    buffer[0] = '\0';

    struct tm tm_utc {};
    bool have_tm = gmtime_r(&now, &tm_utc) != nullptr;
    if (have_tm && strftime(buffer, bufferSize, "%Y-%m-%dT%H:%M:%SZ", &tm_utc) > 0) {
        return;
    }
    snprintf(buffer, bufferSize, "invalid");
}

void logCurrentUtc() {
    time_t now = 0;
    time(&now);

    char utc_buf[32] = {};
    formatUtc(now, utc_buf, sizeof(utc_buf));
    ESP_LOGI(TAG, "time_sync_success utc=%s", utc_buf);
}
}

bool control_time_is_synced() {
    return control_time_has_valid_utc();
}

bool control_time_has_valid_utc() {
    time_t now = 0;
    time(&now);
    const bool valid = now >= MIN_REASONABLE_TIME;

    char unix_buf[24] = {};
    char utc_buf[32] = {};
    formatUnixTime(now, unix_buf, sizeof(unix_buf));
    formatUtc(now, utc_buf, sizeof(utc_buf));
    ESP_LOGI(TAG, "unix_time=%s utc=%s valid_utc=%s", unix_buf, utc_buf, valid ? "yes" : "no");
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
