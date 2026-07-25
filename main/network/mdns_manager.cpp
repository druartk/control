#include "network/mdns_manager.h"

#include <atomic>
#include "esp_log.h"
#include "mdns.h"
#include "sdkconfig.h"

#ifndef CONFIG_USE_MINIMAL_MDNS
#define CONFIG_USE_MINIMAL_MDNS 0
#endif

namespace {
constexpr const char *TAG = "CONTROL_NETWORK";
std::atomic<bool> initialized{false};
}

esp_err_t control_mdns_init() {
    if (initialized.load()) return ESP_OK;

    ESP_LOGI(TAG, "mdns_backend=esp_idf_platform");
    ESP_LOGI(TAG, "chip_minimal_mdns=%s", CONFIG_USE_MINIMAL_MDNS ? "enabled" : "disabled");
    const esp_err_t err = mdns_init();
    ESP_LOGI(TAG, "mdns_init result=%s", esp_err_to_name(err));
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    initialized = true;
    return ESP_OK;
}
