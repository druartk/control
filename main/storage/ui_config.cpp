#include "storage/ui_config.h"

#include <cstddef>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

namespace {
constexpr const char *TAG = "CONTROL_UI_CONFIG";
constexpr const char *NAMESPACE = "control_ui";
constexpr const char *KEY = "config";
constexpr uint32_t MAGIC = 0x43554931; // CUI1
constexpr uint32_t COLLECTION_MAGIC = 0x43554331; // CUC1

// Readers and the configurator share scratch buffers and multi-key NVS writes.
class ConfigLock {
public:
    ConfigLock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }
    ~ConfigLock() { xSemaphoreGiveRecursive(mutex()); }
private:
    static SemaphoreHandle_t mutex() {
        static StaticSemaphore_t storage;
        static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutexStatic(&storage);
        return handle;
    }
};

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

struct StoredNamedConfig {
    uint32_t magic;
    uint32_t config_id;
    char name[CONTROL_UI_CONFIG_NAME_LEN];
    uint32_t revision;
    uint64_t modified_at;
    UiConfig ui;
    uint32_t crc32;
};

struct CollectionMeta {
    uint32_t magic;
    uint16_t schema_version;
    uint8_t config_count;
    uint32_t active_config_id;
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

bool validText(const char *text, size_t capacity) {
    return text && strnlen(text, capacity) < capacity;
}

bool validUiConfig(const UiConfig &config) {
    if (config.schema_version != CONTROL_UI_CONFIG_SCHEMA_VERSION ||
        config.room_count > CONTROL_UI_CONFIG_MAX_ROOMS ||
        config.binding_count > CONTROL_UI_CONFIG_MAX_BINDINGS) return false;
    for (size_t i = 0; i < config.room_count; ++i) {
        if (config.rooms[i].room_id == 0 || !validText(config.rooms[i].name, sizeof(config.rooms[i].name))) return false;
        for (size_t j = 0; j < i; ++j) if (config.rooms[i].room_id == config.rooms[j].room_id) return false;
    }
    for (size_t i = 0; i < config.binding_count; ++i) {
        const UiConfigBinding &binding = config.bindings[i];
        if (binding.node_id == 0 || binding.endpoint_id == 0 ||
            !validText(binding.custom_name, sizeof(binding.custom_name))) return false;
        if (binding.configured) {
            bool roomFound = false;
            for (size_t r = 0; r < config.room_count; ++r) roomFound |= config.rooms[r].room_id == binding.room_id;
            if (binding.room_id == 0 || !roomFound) return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (binding.node_id == config.bindings[j].node_id &&
                binding.endpoint_id == config.bindings[j].endpoint_id) return false;
        }
    }
    return true;
}

bool validCollection(const UiConfigCollection &configs) {
    if (configs.schema_version != CONTROL_UI_CONFIG_SCHEMA_VERSION ||
        configs.config_count == 0 || configs.config_count > CONTROL_UI_CONFIG_MAX_CONFIGS ||
        configs.active_config_id == 0) return false;
    bool activeFound = false;
    for (size_t i = 0; i < configs.config_count; ++i) {
        const UiNamedConfig &config = configs.configs[i];
        if (config.config_id == 0 || !validText(config.name, sizeof(config.name)) || !validUiConfig(config.ui)) return false;
        activeFound |= config.config_id == configs.active_config_id;
        for (size_t j = 0; j < i; ++j) if (config.config_id == configs.configs[j].config_id) return false;
    }
    return activeFound;
}

void packConfig(const UiConfig &config, StoredConfig &stored) {
    stored = {};
    stored.magic = MAGIC;
    stored.schema_version = config.schema_version;
    stored.room_count = config.room_count;
    stored.binding_count = config.binding_count;
    std::memcpy(stored.rooms, config.rooms, sizeof(stored.rooms));
    std::memcpy(stored.bindings, config.bindings, sizeof(stored.bindings));
    stored.crc32 = crc32(reinterpret_cast<const uint8_t *>(&stored), offsetof(StoredConfig, crc32));
}

bool unpackConfig(const StoredConfig &stored, UiConfig &out) {
    if (stored.magic != MAGIC || stored.schema_version != CONTROL_UI_CONFIG_SCHEMA_VERSION ||
        stored.room_count > CONTROL_UI_CONFIG_MAX_ROOMS || stored.binding_count > CONTROL_UI_CONFIG_MAX_BINDINGS ||
        stored.crc32 != crc32(reinterpret_cast<const uint8_t *>(&stored), offsetof(StoredConfig, crc32))) return false;
    out = UiConfig{};
    out.schema_version = stored.schema_version;
    out.room_count = stored.room_count;
    out.binding_count = stored.binding_count;
    std::memcpy(out.rooms, stored.rooms, sizeof(out.rooms));
    std::memcpy(out.bindings, stored.bindings, sizeof(out.bindings));
    return validUiConfig(out);
}

void packNamed(const UiNamedConfig &config, StoredNamedConfig &stored) {
    stored = {};
    stored.magic = COLLECTION_MAGIC;
    stored.config_id = config.config_id;
    std::memcpy(stored.name, config.name, sizeof(stored.name));
    stored.revision = config.revision;
    stored.modified_at = config.modified_at;
    stored.ui = config.ui;
    stored.crc32 = crc32(reinterpret_cast<const uint8_t *>(&stored), offsetof(StoredNamedConfig, crc32));
}

bool unpackNamed(const StoredNamedConfig &stored, UiNamedConfig &out) {
    if (stored.magic != COLLECTION_MAGIC || stored.config_id == 0 ||
        !validText(stored.name, sizeof(stored.name)) ||
        stored.crc32 != crc32(reinterpret_cast<const uint8_t *>(&stored), offsetof(StoredNamedConfig, crc32))) return false;
    out = UiNamedConfig{};
    out.config_id = stored.config_id;
    std::memcpy(out.name, stored.name, sizeof(out.name));
    out.revision = stored.revision;
    out.modified_at = stored.modified_at;
    out.ui = stored.ui;
    return validUiConfig(out.ui);
}

bool unpackMeta(const CollectionMeta &meta) {
    return meta.magic == COLLECTION_MAGIC && meta.schema_version == CONTROL_UI_CONFIG_SCHEMA_VERSION &&
           meta.config_count > 0 && meta.config_count <= CONTROL_UI_CONFIG_MAX_CONFIGS && meta.active_config_id != 0 &&
           meta.crc32 == crc32(reinterpret_cast<const uint8_t *>(&meta), offsetof(CollectionMeta, crc32));
}

esp_err_t readMeta(nvs_handle_t handle, CollectionMeta &meta) {
    size_t size = sizeof(meta);
    const esp_err_t err = nvs_get_blob(handle, "cfgmeta", &meta, &size);
    if (err != ESP_OK) return err;
    return size == sizeof(meta) && unpackMeta(meta) ? ESP_OK : ESP_ERR_INVALID_CRC;
}
}

esp_err_t control_ui_config_load(UiConfig &out) {
    ConfigLock lock;
    out = UiConfig{};
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;

    static CollectionMeta meta{};
    err = readMeta(handle, meta);
    if (err == ESP_OK) {
        for (size_t i = 0; i < meta.config_count; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "cfg%u", static_cast<unsigned>(i));
            static StoredNamedConfig stored{};
            size_t size = sizeof(stored);
            if (nvs_get_blob(handle, key, &stored, &size) != ESP_OK || size != sizeof(stored)) continue;
            static UiNamedConfig named;
            if (unpackNamed(stored, named) && named.config_id == meta.active_config_id) {
                out = named.ui;
                nvs_close(handle);
                return ESP_OK;
            }
        }
        nvs_close(handle);
        return ESP_ERR_INVALID_CRC;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return err;
    }

    size_t size = 0;
    err = nvs_get_blob(handle, KEY, nullptr, &size);
    if (err == ESP_OK && size == sizeof(StoredConfig)) {
        static StoredConfig stored{};
        err = nvs_get_blob(handle, KEY, &stored, &size);
        if (err == ESP_OK && unpackConfig(stored, out)) {
            nvs_close(handle);
            return ESP_OK;
        }
        if (err == ESP_OK) err = ESP_ERR_INVALID_VERSION;
    } else if (err == ESP_OK) {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    ESP_LOGW(TAG, "config_load_failed result=%s", esp_err_to_name(err));
    return err;
}

esp_err_t control_ui_config_save(const UiConfig &config) {
    ConfigLock lock;
    if (!validUiConfig(config)) return ESP_ERR_INVALID_ARG;
    StoredConfig stored{};
    packConfig(config, stored);

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
    ConfigLock lock;
    const esp_err_t err = clearNamespace();
    ESP_LOGI(TAG, "clear result=%s namespace=%s", esp_err_to_name(err), NAMESPACE);
    return err;
}

esp_err_t control_ui_config_collection_load(UiConfigCollection &out) {
    ConfigLock lock;
    std::memset(&out, 0, sizeof(out));
    out.schema_version = CONTROL_UI_CONFIG_SCHEMA_VERSION;
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NOT_FOUND;
    if (err != ESP_OK) return err;
    static CollectionMeta meta{};
    err = readMeta(handle, meta);
    if (err != ESP_OK) {
        nvs_close(handle);
        if (err != ESP_ERR_NVS_NOT_FOUND) return err;
        static UiConfig legacy;
        err = control_ui_config_load(legacy);
        if (err != ESP_OK) return err;
        out.config_count = 1;
        out.active_config_id = 1;
        out.configs[0].config_id = 1;
        std::strncpy(out.configs[0].name, "Maison", sizeof(out.configs[0].name) - 1);
        out.configs[0].revision = 1;
        out.configs[0].ui = legacy;
        return ESP_OK;
    }
    out.schema_version = meta.schema_version;
    out.config_count = meta.config_count;
    out.active_config_id = meta.active_config_id;
    for (size_t i = 0; i < meta.config_count; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "cfg%u", static_cast<unsigned>(i));
        static StoredNamedConfig stored{};
        size_t size = sizeof(stored);
        if (nvs_get_blob(handle, key, &stored, &size) != ESP_OK || size != sizeof(stored) ||
            !unpackNamed(stored, out.configs[i])) {
            nvs_close(handle);
            std::memset(&out, 0, sizeof(out));
            return ESP_ERR_INVALID_CRC;
        }
    }
    nvs_close(handle);
    if (!validCollection(out)) {
        out = UiConfigCollection{};
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t control_ui_config_collection_save(const UiConfigCollection &configs) {
    ConfigLock lock;
    if (!validCollection(configs)) return ESP_ERR_INVALID_ARG;
    CollectionMeta meta{};
    meta.magic = COLLECTION_MAGIC;
    meta.schema_version = configs.schema_version;
    meta.config_count = configs.config_count;
    meta.active_config_id = configs.active_config_id;
    meta.crc32 = crc32(reinterpret_cast<const uint8_t *>(&meta), offsetof(CollectionMeta, crc32));

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    for (size_t i = 0; err == ESP_OK && i < configs.config_count; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "tmp%u", static_cast<unsigned>(i));
        StoredNamedConfig stored{};
        packNamed(configs.configs[i], stored);
        err = nvs_set_blob(handle, key, &stored, sizeof(stored));
    }
    if (err == ESP_OK) err = nvs_set_blob(handle, "cfgmeta", &meta, sizeof(meta));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err == ESP_OK) {
        for (size_t i = 0; i < configs.config_count; ++i) {
            char source[8], target[8];
            snprintf(source, sizeof(source), "tmp%u", static_cast<unsigned>(i));
            snprintf(target, sizeof(target), "cfg%u", static_cast<unsigned>(i));
            StoredNamedConfig stored{};
            size_t size = sizeof(stored);
            if (nvs_get_blob(handle, source, &stored, &size) != ESP_OK || size != sizeof(stored)) { err = ESP_ERR_INVALID_CRC; break; }
            err = nvs_set_blob(handle, target, &stored, sizeof(stored));
            (void)nvs_erase_key(handle, source);
            if (err != ESP_OK) break;
        }
        for (size_t i = configs.config_count; err == ESP_OK && i < CONTROL_UI_CONFIG_MAX_CONFIGS; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "cfg%u", static_cast<unsigned>(i));
            (void)nvs_erase_key(handle, key);
        }
        if (err == ESP_OK) {
            const UiNamedConfig *active = nullptr;
            for (size_t i = 0; i < configs.config_count; ++i) if (configs.configs[i].config_id == configs.active_config_id) active = &configs.configs[i];
            StoredConfig legacy{};
            packConfig(active->ui, legacy);
            err = nvs_set_blob(handle, KEY, &legacy, sizeof(legacy));
        }
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    if (handle != 0) nvs_close(handle);
    ESP_LOGI(TAG, "collection_save count=%u active=%lu result=%s", configs.config_count,
             static_cast<unsigned long>(configs.active_config_id), esp_err_to_name(err));
    return err;
}
