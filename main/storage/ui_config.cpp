#include "storage/ui_config.h"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

namespace {
constexpr const char *TAG = "CONTROL_UI_CONFIG";
constexpr const char *NAMESPACE = "control_ui";
constexpr const char *KEY = "config";
constexpr uint32_t MAGIC = 0x43554931; // CUI1

#pragma pack(push, 1)
struct StoredConfig {
    uint32_t magic;
    uint16_t schema_version;
    uint8_t room_count;
    uint8_t binding_count;
    UiConfigRoom rooms[CONTROL_UI_CONFIG_MAX_ROOMS];
    UiConfigBinding bindings[CONTROL_UI_CONFIG_MAX_BINDINGS];
    uint32_t crc32;
};
#pragma pack(pop)

uint32_t crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

esp_err_t clearNamespace() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
}

esp_err_t control_ui_config_load(UiConfig &out) {
    out = {};
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;

    size_t size = 0;
    err = nvs_get_blob(handle, KEY, nullptr, &size);
    if (err == ESP_OK && size == sizeof(StoredConfig)) {
        StoredConfig stored{};
        err = nvs_get_blob(handle, KEY, &stored, &size);
        const uint32_t expected = crc32(reinterpret_cast<const uint8_t *>(&stored),
                                        offsetof(StoredConfig, crc32));
        if (err == ESP_OK && stored.magic == MAGIC &&
            stored.schema_version == CONTROL_UI_CONFIG_SCHEMA_VERSION &&
            stored.room_count <= CONTROL_UI_CONFIG_MAX_ROOMS &&
            stored.binding_count <= CONTROL_UI_CONFIG_MAX_BINDINGS &&
            stored.crc32 == expected) {
            out.schema_version = stored.schema_version;
            out.room_count = stored.room_count;
            out.binding_count = stored.binding_count;
            std::memcpy(out.rooms, stored.rooms, sizeof(out.rooms));
            std::memcpy(out.bindings, stored.bindings, sizeof(out.bindings));
            nvs_close(handle);
            return ESP_OK;
        }
        err = ESP_ERR_INVALID_VERSION;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    ESP_LOGW(TAG, "invalid_config result=%s clearing_namespace=yes", esp_err_to_name(err));
    const esp_err_t clear_err = clearNamespace();
    return clear_err == ESP_OK ? err : clear_err;
}

esp_err_t control_ui_config_save(const UiConfig &config) {
    if (config.room_count > CONTROL_UI_CONFIG_MAX_ROOMS ||
        config.binding_count > CONTROL_UI_CONFIG_MAX_BINDINGS) return ESP_ERR_INVALID_ARG;
    StoredConfig stored{};
    stored.magic = MAGIC;
    stored.schema_version = CONTROL_UI_CONFIG_SCHEMA_VERSION;
    stored.room_count = config.room_count;
    stored.binding_count = config.binding_count;
    std::memcpy(stored.rooms, config.rooms, sizeof(stored.rooms));
    std::memcpy(stored.bindings, config.bindings, sizeof(stored.bindings));
    stored.crc32 = crc32(reinterpret_cast<const uint8_t *>(&stored), offsetof(StoredConfig, crc32));

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) err = nvs_set_blob(handle, KEY, &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (handle != 0) nvs_close(handle);
    ESP_LOGI(TAG, "save rooms=%u bindings=%u result=%s", config.room_count, config.binding_count,
             esp_err_to_name(err));
    return err;
}

esp_err_t control_ui_config_clear() {
    const esp_err_t err = clearNamespace();
    ESP_LOGI(TAG, "clear result=%s namespace=%s", esp_err_to_name(err), NAMESPACE);
    return err;
}
