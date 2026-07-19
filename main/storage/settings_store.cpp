#include "storage/settings_store.h"

#include <vector>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {
constexpr const char *TAG = "CONTROL_STORAGE";
constexpr const char *NAMESPACE = "control";

std::string getString(nvs_handle_t handle, const char *key) {
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK || length == 0) return {};
    std::vector<char> value(length);
    if (nvs_get_str(handle, key, value.data(), &length) != ESP_OK) return {};
    return value.data();
}
}

bool SettingsStore::initialize() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS incompatible ou pleine; effacement de récupération");
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        status_ = "NVS indisponible";
        ESP_LOGE(TAG, "%s: %s; fonctionnement sans persistance", status_.c_str(), esp_err_to_name(err));
        return false;
    }
    ready_ = true;
    status_ = "OK";
    ESP_LOGI(TAG, "NVS initialisée");
    return true;
}

StoredSettings SettingsStore::load() const {
    StoredSettings settings;
    if (!ready_) {
        ESP_LOGW(TAG, "load_wifi_credentials found=no reason=nvs_not_ready");
        return settings;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "load_wifi_credentials found=no err=%s", esp_err_to_name(err));
        return settings;
    }
    settings.ssid = getString(handle, "wifi_ssid");
    settings.password = getString(handle, "wifi_pass");
    uint8_t complete = 0;
    nvs_get_u8(handle, "setup_done", &complete);
    settings.initialSetupComplete = complete != 0;
    nvs_close(handle);
    ESP_LOGI(TAG, "load_wifi_credentials found=%s ssid=\"%s\" password_len=%u",
             settings.hasWifiCredentials() ? "yes" : "no",
             settings.ssid.c_str(),
             static_cast<unsigned>(settings.password.size()));
    ESP_LOGI(TAG, "Réglages chargés: Wi-Fi=%s configuration_initiale=%s",
             settings.hasWifiCredentials() ? "présent" : "absent",
             settings.initialSetupComplete ? "oui" : "non");
    return settings;
}

bool SettingsStore::saveWifi(const std::string &ssid, const std::string &password) {
    ESP_LOGI(TAG, "save_wifi_credentials start ssid=\"%s\" password_len=%u",
             ssid.c_str(), static_cast<unsigned>(password.size()));
    if (!ready_) {
        ESP_LOGE(TAG, "save_wifi_credentials result=%s", esp_err_to_name(ESP_ERR_INVALID_STATE));
        return false;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    const bool opened = err == ESP_OK;
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_ssid", ssid.c_str());
    if (err == ESP_OK) err = nvs_set_str(handle, "wifi_pass", password.c_str());
    if (err == ESP_OK) err = nvs_set_u8(handle, "setup_done", 1);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    ESP_LOGI(TAG, "save_wifi_credentials result=%s", esp_err_to_name(err));
    if (err != ESP_OK) ESP_LOGE(TAG, "Échec enregistrement Wi-Fi: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "wifi_credentials_saved ssid=\"%s\"", ssid.c_str());
    return err == ESP_OK;
}

bool SettingsStore::setInitialSetupComplete(bool complete) {
    if (!ready_) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    const bool opened = err == ESP_OK;
    if (err == ESP_OK) err = nvs_set_u8(handle, "setup_done", complete ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    return err == ESP_OK;
}

bool SettingsStore::eraseWifi() {
    if (!ready_) return false;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    const bool opened = err == ESP_OK;
    if (err == ESP_OK) {
        const esp_err_t ssidResult = nvs_erase_key(handle, "wifi_ssid");
        const esp_err_t passResult = nvs_erase_key(handle, "wifi_pass");
        const esp_err_t setupResult = nvs_erase_key(handle, "setup_done");
        if (ssidResult != ESP_OK && ssidResult != ESP_ERR_NVS_NOT_FOUND) err = ssidResult;
        if (passResult != ESP_OK && passResult != ESP_ERR_NVS_NOT_FOUND) err = passResult;
        if (setupResult != ESP_OK && setupResult != ESP_ERR_NVS_NOT_FOUND) err = setupResult;
    }
    if (err == ESP_OK) err = nvs_commit(handle);
    if (opened) nvs_close(handle);
    ESP_LOGI(TAG, "Identifiants Wi-Fi effacés: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
