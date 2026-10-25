#include "config/control_config_serial.h"

#include "sdkconfig.h"

#if CONFIG_CONTROL_WEB_SERIAL_CONFIGURATOR

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <new>
#include <unistd.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_private/usb_console.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "storage/ui_config.h"
#include "config/control_config_web.h"

namespace {
constexpr const char *TAG = "CONTROL_CONFIG";
constexpr size_t MAX_FRAME = 8192;
constexpr size_t REQUEST_QUEUE_LENGTH = 3;
constexpr size_t RESULT_QUEUE_LENGTH = 8;
constexpr size_t RESPONSE_QUEUE_LENGTH = 8;
constexpr size_t MAX_PENDING = 8;
constexpr size_t MAX_COMMAND_LENGTH = 32;
constexpr uint32_t PROTOCOL_VERSION = 1;
constexpr uint32_t MAX_IDENTIFY_MS = 10000;
constexpr uint32_t SERIAL_TASK_STACK_SIZE = 24576;

struct ConfigRequest {
    uint32_t web_request = 0;
    uint16_t length = 0;
    uint32_t request_id = 0;
    char cmd[MAX_COMMAND_LENGTH] = {};
    char *json = nullptr;
};

struct ConfigResponse {
    uint32_t web_request = 0;
    uint32_t request_id = 0;
    uint16_t length = 0;
    char type[24] = {};
    char *json = nullptr;
};

enum class SerialTransport {
    NativeUsbSerialJtag,
    Uart,
    UsbCdc,
    Unsupported,
};

struct PendingRequest {
    bool used = false;
    uint32_t web_request = 0;
    uint32_t command_id = 0;
    uint32_t request_id = 0;
    uint64_t node_id = 0;
    uint16_t endpoint = 0;
    MatterUiAction action = MatterUiAction::SetOff;
    uint8_t brightness = 0;
    bool identify = false;
    bool original_state = false;
    uint8_t identify_stage = 0;
    uint32_t identify_duration_ms = 3000;
    int64_t identify_deadline_us = 0;
    int64_t next_identify_us = 0;
};

QueueHandle_t g_requests = nullptr;
QueueHandle_t g_results = nullptr;
QueueHandle_t g_responses = nullptr;
QueueHandle_t g_web_responses = nullptr;
TaskHandle_t g_task = nullptr;
PendingRequest g_pending[MAX_PENDING]{};
UiConfigCollection *g_configs = nullptr;
UiConfigCollection *g_verified_configs = nullptr;
bool g_configs_loaded = false;
volatile bool g_config_update = false;
volatile uint32_t g_config_update_id = 0;
char *g_line = nullptr;
size_t g_line_length = 0;
bool g_line_discard = false;
SerialTransport g_transport = SerialTransport::Unsupported;
int g_input_fd = -1;
uint32_t g_dispatch_request_id = 0;
uint32_t g_dispatch_web_request = 0;
uint32_t g_next_command_id = 0;
char g_dispatch_cmd[MAX_COMMAND_LENGTH] = {};
StaticSemaphore_t g_console_mutex_storage;
SemaphoreHandle_t g_console_mutex = nullptr;
vprintf_like_t g_previous_log_vprintf = nullptr;

int serializedLogVprintf(const char *format, va_list args) {
    if (!g_previous_log_vprintf) return 0;
    if (g_console_mutex) xSemaphoreTake(g_console_mutex, portMAX_DELAY);
    const int result = g_previous_log_vprintf(format, args);
    if (g_console_mutex) xSemaphoreGive(g_console_mutex);
    return result;
}

void *configJsonAllocate(size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void configJsonFree(void *pointer) {
    heap_caps_free(pointer);
}

void configureJsonAllocator() {
    cJSON_Hooks hooks = {configJsonAllocate, configJsonFree};
    cJSON_InitHooks(&hooks);
}

const char *transportName(SerialTransport transport) {
    switch (transport) {
    case SerialTransport::NativeUsbSerialJtag: return "native_usb_serial_jtag";
    case SerialTransport::Uart: return "uart";
    case SerialTransport::UsbCdc: return "usb_cdc";
    default: return "unsupported";
    }
}

void logInitHeap(const char *step) {
    ESP_LOGE(TAG, "init_heap step=%s internal_free=%u internal_largest=%u", step,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

esp_err_t initFailure(const char *step, const char *error, int code) {
    ESP_LOGE(TAG, "serial_init_failed step=%s error=%s code=%d", step, error, code);
    logInitHeap(step);
    g_input_fd = -1;
    configJsonFree(g_configs);
    g_configs = nullptr;
    configJsonFree(g_verified_configs);
    g_verified_configs = nullptr;
    g_configs_loaded = false;
    configJsonFree(g_line);
    g_line = nullptr;
    g_line_length = 0;
    g_line_discard = false;
    if (g_results) { vQueueDelete(g_results); g_results = nullptr; }
    if (g_responses) { vQueueDelete(g_responses); g_responses = nullptr; }
    if (g_web_responses) { vQueueDelete(g_web_responses); g_web_responses = nullptr; }
    if (g_requests) { vQueueDelete(g_requests); g_requests = nullptr; }
    g_dispatch_request_id = 0;
    g_dispatch_cmd[0] = '\0';
    return ESP_FAIL;
}

SerialTransport configuredTransport() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return SerialTransport::NativeUsbSerialJtag;
#elif CONFIG_ESP_CONSOLE_USB_CDC
    return SerialTransport::UsbCdc;
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    return SerialTransport::Uart;
#else
    return SerialTransport::Unsupported;
#endif
}

const char *primaryConsolePath() {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    return "stdin";
#else
    return "unsupported";
#endif
}

const char *secondaryConsolePath() {
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    return "/dev/secondary";
#else
    return "disabled";
#endif
}

esp_err_t initTransport(int &errorCode) {
    errorCode = 0;
    g_transport = configuredTransport();
    if (g_transport == SerialTransport::Unsupported) return ESP_ERR_NOT_SUPPORTED;

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_CDC || CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    // STDIN is the primary ESP-IDF console VFS. With USB CDC selected it
    // forwards to the already-open /dev/cdcacm descriptor used by COM3.
    g_input_fd = STDIN_FILENO;
    const int flags = fcntl(g_input_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(g_input_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        errorCode = errno;
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void formatNode(uint64_t node, char *out, size_t size) {
    snprintf(out, size, "0x%08llx%08llx",
             static_cast<unsigned long long>(node >> 32),
             static_cast<unsigned long long>(node & 0xffffffffULL));
}

void sendFrame(const char *json) {
    const size_t jsonLength = json ? strlen(json) : 0;
    if (!json || jsonLength > MAX_FRAME - 7) return;
    char frame[MAX_FRAME];
    const int frameLength = snprintf(frame, sizeof(frame), "@@CFG:%s\n", json);
    if (frameLength <= 0 || static_cast<size_t>(frameLength) >= sizeof(frame)) return;
    ESP_LOGI("CONTROL_CFG", "response_frame_bytes=%u", static_cast<unsigned>(frameLength));
    if (g_console_mutex) xSemaphoreTake(g_console_mutex, portMAX_DELAY);
    size_t written = 0;
    bool failed = false;
    while (written < static_cast<size_t>(frameLength)) {
        const ssize_t count = write(STDOUT_FILENO, frame + written,
                                    static_cast<size_t>(frameLength) - written);
        if (count > 0) written += static_cast<size_t>(count);
        else if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) vTaskDelay(pdMS_TO_TICKS(10));
        else { failed = true; break; }
    }
    if (g_console_mutex) xSemaphoreGive(g_console_mutex);
    if (failed) ESP_LOGW("CONTROL_CFG", "response_write_failed bytes=%u written=%u errno=%d",
                         static_cast<unsigned>(frameLength), static_cast<unsigned>(written), errno);
}

bool sendObject(cJSON *object) {
    if (!object) return false;
    char *text = cJSON_PrintUnformatted(object);
    if (!text) {
        ESP_LOGE(TAG, "command_failed id=%lu cmd=%s step=response_encode error=no_memory code=%d",
                 static_cast<unsigned long>(g_dispatch_request_id),
                 g_dispatch_cmd[0] ? g_dispatch_cmd : "response", ESP_ERR_NO_MEM);
        cJSON_Delete(object);
        return false;
    }
    const size_t length = strlen(text);
    const cJSON *idValue = cJSON_GetObjectItemCaseSensitive(object, "request_id");
    const cJSON *typeValue = cJSON_GetObjectItemCaseSensitive(object, "type");
    ConfigResponse response;
    response.web_request = g_dispatch_web_request;
    response.request_id = cJSON_IsNumber(idValue) ? static_cast<uint32_t>(idValue->valuedouble) : 0;
    response.length = length <= UINT16_MAX ? static_cast<uint16_t>(length) : 0;
    const char *type = cJSON_IsString(typeValue) ? typeValue->valuestring : "unknown";
    strncpy(response.type, type, sizeof(response.type) - 1);
    response.type[sizeof(response.type) - 1] = '\0';
    if (length > MAX_FRAME - 7 || response.length == 0) {
        ESP_LOGE(TAG, "command_failed id=%lu cmd=%s step=response_size error=frame_too_large code=%u",
                 static_cast<unsigned long>(response.request_id),
                 g_dispatch_cmd[0] ? g_dispatch_cmd : "response", static_cast<unsigned>(length));
        cJSON_free(text);
        cJSON_Delete(object);
        return false;
    }
    response.json = text;
    ESP_LOGI("CONTROL_CFG", "response_type=%s response_json_bytes=%u response_frame_bytes=%u",
             response.type, static_cast<unsigned>(response.length),
             static_cast<unsigned>(response.length + 7));
    ESP_LOGI(TAG, "dispatch_done id=%lu bytes=%u",
             static_cast<unsigned long>(response.request_id),
             static_cast<unsigned>(response.length));
    const BaseType_t queued = g_responses ? xQueueSend(g_responses, &response, 0) : pdFAIL;
    ESP_LOGI(TAG, "enqueue_response id=%lu result=%s",
             static_cast<unsigned long>(response.request_id), queued == pdPASS ? "ok" : "fail");
    if (queued != pdPASS) {
        ESP_LOGE(TAG, "command_failed id=%lu cmd=%s step=response_queue error=queue_full code=%d",
                 static_cast<unsigned long>(response.request_id),
                 g_dispatch_cmd[0] ? g_dispatch_cmd : "response", -1);
        cJSON_free(response.json);
    }
    cJSON_Delete(object);
    return queued == pdPASS;
}

void sendError(uint32_t requestId, const char *code, const char *message) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "error");
    if (requestId != 0) cJSON_AddNumberToObject(response, "request_id", requestId);
    cJSON_AddStringToObject(response, "code", code ? code : "ERROR");
    cJSON_AddStringToObject(response, "message", message ? message : "Erreur");
    sendObject(response);
}

void respond(cJSON *response, uint32_t requestId) {
    if (requestId != 0) cJSON_AddNumberToObject(response, "request_id", requestId);
    sendObject(response);
}

const cJSON *field(const cJSON *object, const char *name) {
    return object && cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object, name) : nullptr;
}

bool getRequestId(const cJSON *root, uint32_t &out) {
    const cJSON *id = field(root, "request_id");
    if (!cJSON_IsNumber(id) || id->valuedouble < 1 || id->valuedouble > 0xffffffffULL ||
        static_cast<double>(static_cast<uint32_t>(id->valuedouble)) != id->valuedouble) return false;
    out = static_cast<uint32_t>(id->valuedouble);
    return true;
}

PendingRequest *pendingFor(uint32_t id) {
    for (auto &pending : g_pending) if (pending.used && pending.request_id == id &&
                                       pending.web_request == g_dispatch_web_request) return &pending;
    return nullptr;
}

PendingRequest *newPending(uint32_t id) {
    if (pendingFor(id)) return nullptr;
    for (auto &pending : g_pending) {
        if (!pending.used) {
            pending = PendingRequest{};
            pending.used = true;
            pending.web_request = g_dispatch_web_request;
            if (++g_next_command_id == 0) ++g_next_command_id;
            pending.command_id = g_next_command_id;
            pending.request_id = id;
            return &pending;
        }
    }
    return nullptr;
}

void releasePending(PendingRequest &pending) {
    pending = PendingRequest{};
}

bool ensureConfigs() {
    if (g_configs_loaded) return true;
    const esp_err_t loadErr = control_ui_config_collection_load(*g_configs);
    if (loadErr != ESP_OK) {
        *g_configs = UiConfigCollection{};
        g_configs->config_count = 1;
        g_configs->active_config_id = 1;
        g_configs->configs[0].config_id = 1;
        strncpy(g_configs->configs[0].name, "Maison", sizeof(g_configs->configs[0].name) - 1);
        g_configs->configs[0].revision = 1;
    }
    g_configs_loaded = true;
    return true;
}

UiNamedConfig *configById(uint32_t id) {
    ensureConfigs();
    for (size_t i = 0; i < g_configs->config_count; ++i) if (g_configs->configs[i].config_id == id) return &g_configs->configs[i];
    return nullptr;
}

UiNamedConfig *activeConfig() {
    return configById(g_configs->active_config_id);
}

bool getDevices(MatterPostCommissioningResult &out) {
    out = MatterPostCommissioningResult{};
    if (matter_controller_load_device_cache(out) == ESP_OK && !out.devices.empty()) return true;
    MatterDiscoveryResult discovery;
    if (matter_controller_get_discovery_result(discovery) != ESP_OK ||
        discovery.phase != MatterDiscoveryPhase::COMPLETE) return false;
    out.savedNode.node_id = discovery.node_id;
    out.savedNode.commissioned = discovery.node_id != 0;
    for (size_t i = 0; i < discovery.devices.size(); ++i) {
        const MatterDiscoveredDevice &source = discovery.devices[i];
        MatterDeviceItem device;
        device.endpoint_id = source.endpoint_id;
        device.name = source.display_name;
        device.name_source = source.name_source;
        device.has_onoff = source.has_onoff;
        device.has_level_control = source.has_level_control;
        device.reachable_known = source.reachable_known;
        device.reachable = source.reachable;
        device.onoff_state_known = source.onoff_state_known;
        device.onoff_state = source.onoff_state;
        device.device_type = source.primary_device_type;
        out.devices.push_back(device);
    }
    return true;
}

const MatterDeviceItem *findDevice(uint64_t node, uint16_t endpoint, MatterPostCommissioningResult &cache) {
    if (!getDevices(cache) || cache.savedNode.node_id != node) return nullptr;
    for (const auto &device : cache.devices) if (device.endpoint_id == endpoint) return &device;
    return nullptr;
}

uint32_t roomFor(const UiNamedConfig *config, uint64_t node, uint16_t endpoint, bool *configured = nullptr) {
    if (configured) *configured = false;
    if (!config) return 0;
    for (size_t i = 0; i < config->ui.binding_count; ++i) {
        const UiConfigBinding &binding = config->ui.bindings[i];
        if (binding.node_id == node && binding.endpoint_id == endpoint) {
            if (configured) *configured = binding.configured;
            return binding.room_id;
        }
    }
    return 0;
}

cJSON *roomJson(const UiConfigRoom &room) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddNumberToObject(item, "id", room.room_id);
    cJSON_AddStringToObject(item, "name", room.name);
    cJSON_AddNumberToObject(item, "order", room.order);
    return item;
}

cJSON *configJson(const UiNamedConfig &config, bool active) {
    cJSON *item = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "type", "config");
    cJSON_AddNumberToObject(item, "id", config.config_id);
    cJSON_AddStringToObject(item, "name", config.name);
    cJSON_AddNumberToObject(item, "schema", config.ui.schema_version);
    cJSON_AddNumberToObject(item, "revision", config.revision);
    cJSON_AddBoolToObject(item, "active", active);
    cJSON *rooms = cJSON_AddArrayToObject(item, "rooms");
    for (size_t i = 0; i < config.ui.room_count; ++i) cJSON_AddItemToArray(rooms, roomJson(config.ui.rooms[i]));
    cJSON *bindings = cJSON_AddArrayToObject(item, "bindings");
    for (size_t i = 0; i < config.ui.binding_count; ++i) {
        const UiConfigBinding &binding = config.ui.bindings[i];
        cJSON *entry = cJSON_CreateObject();
        char node[24];
        formatNode(binding.node_id, node, sizeof(node));
        cJSON_AddStringToObject(entry, "node_id", node);
        cJSON_AddNumberToObject(entry, "endpoint", binding.endpoint_id);
        if (binding.configured) cJSON_AddNumberToObject(entry, "room_id", binding.room_id);
        else cJSON_AddNullToObject(entry, "room_id");
        cJSON_AddStringToObject(entry, "display_name", binding.custom_name);
        cJSON_AddNumberToObject(entry, "order", binding.order);
        cJSON_AddBoolToObject(entry, "configured", binding.configured);
        cJSON_AddItemToArray(bindings, entry);
    }
    return item;
}

bool parseNode(const cJSON *value, uint64_t &out) {
    if (cJSON_IsNumber(value) && value->valuedouble >= 1 && value->valuedouble <= 18446744073709551615.0) {
        out = static_cast<uint64_t>(value->valuedouble);
        return true;
    }
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    char *end = nullptr;
    errno = 0;
    out = strtoull(value->valuestring, &end, 0);
    return errno == 0 && end != value->valuestring && *end == '\0' && out != 0;
}

bool parseConfig(const cJSON *object, UiNamedConfig &out, uint32_t fallbackId = 0) {
    if (!cJSON_IsObject(object)) return false;
    const cJSON *id = field(object, "id");
    const cJSON *name = field(object, "name");
    const cJSON *schema = field(object, "schema");
    if (id && (!cJSON_IsNumber(id) || id->valuedouble < 1 || id->valuedouble > 0xffffffffULL)) return false;
    if (!cJSON_IsString(name) || !name->valuestring || strlen(name->valuestring) >= sizeof(out.name)) return false;
    if (schema && (!cJSON_IsNumber(schema) || schema->valueint != CONTROL_UI_CONFIG_SCHEMA_VERSION)) return false;
    out = UiNamedConfig{};
    out.config_id = id ? static_cast<uint32_t>(id->valueint) : fallbackId;
    if (out.config_id == 0) return false;
    strncpy(out.name, name->valuestring, sizeof(out.name) - 1);
    out.ui.schema_version = CONTROL_UI_CONFIG_SCHEMA_VERSION;
    const cJSON *rooms = field(object, "rooms");
    const cJSON *bindings = field(object, "bindings");
    if (!cJSON_IsArray(rooms) || cJSON_GetArraySize(rooms) > CONTROL_UI_CONFIG_MAX_ROOMS ||
        !cJSON_IsArray(bindings) || cJSON_GetArraySize(bindings) > CONTROL_UI_CONFIG_MAX_BINDINGS) return false;
    cJSON *roomItem = nullptr;
    cJSON_ArrayForEach(roomItem, rooms) {
        if (out.ui.room_count >= CONTROL_UI_CONFIG_MAX_ROOMS) return false;
        const cJSON *roomId = field(roomItem, "id");
        const cJSON *roomName = field(roomItem, "name");
        if (!cJSON_IsNumber(roomId) || roomId->valueint <= 0 || !cJSON_IsString(roomName) ||
            !roomName->valuestring || strlen(roomName->valuestring) >= sizeof(out.ui.rooms[0].name)) return false;
        UiConfigRoom &room = out.ui.rooms[out.ui.room_count++];
        room.room_id = static_cast<uint32_t>(roomId->valueint);
        room.order = static_cast<uint8_t>(std::min(255, std::max(0, field(roomItem, "order") ? field(roomItem, "order")->valueint : static_cast<int>(out.ui.room_count - 1))));
        strncpy(room.name, roomName->valuestring, sizeof(room.name) - 1);
        for (size_t i = 0; i + 1 < out.ui.room_count; ++i) if (out.ui.rooms[i].room_id == room.room_id) return false;
    }
    cJSON *bindingItem = nullptr;
    cJSON_ArrayForEach(bindingItem, bindings) {
        if (out.ui.binding_count >= CONTROL_UI_CONFIG_MAX_BINDINGS) return false;
        const cJSON *node = field(bindingItem, "node_id");
        const cJSON *endpoint = field(bindingItem, "endpoint");
        const cJSON *display = field(bindingItem, "display_name");
        const cJSON *room = field(bindingItem, "room_id");
        if (!node || !cJSON_IsNumber(endpoint) || endpoint->valueint <= 0 || endpoint->valueint > 65535 ||
            !cJSON_IsString(display) || !display->valuestring || strlen(display->valuestring) >= sizeof(out.ui.bindings[0].custom_name)) return false;
        UiConfigBinding &binding = out.ui.bindings[out.ui.binding_count++];
        if (!parseNode(node, binding.node_id)) return false;
        binding.endpoint_id = static_cast<uint16_t>(endpoint->valueint);
        binding.order = static_cast<uint8_t>(std::min(255, std::max(0, field(bindingItem, "order") ? field(bindingItem, "order")->valueint : static_cast<int>(out.ui.binding_count - 1))));
        strncpy(binding.custom_name, display->valuestring, sizeof(binding.custom_name) - 1);
        binding.configured = cJSON_IsNumber(room) && room->valueint != 0;
        binding.room_id = binding.configured ? static_cast<uint32_t>(room->valueint) : 0;
        if (binding.configured) {
            bool roomFound = false;
            for (size_t i = 0; i < out.ui.room_count; ++i) roomFound |= out.ui.rooms[i].room_id == binding.room_id;
            if (!roomFound) return false;
        }
        for (size_t i = 0; i + 1 < out.ui.binding_count; ++i) {
            if (out.ui.bindings[i].node_id == binding.node_id && out.ui.bindings[i].endpoint_id == binding.endpoint_id) return false;
        }
    }
    return true;
}

bool validateEndpoints(const UiNamedConfig &config, bool allowUnassigned) {
    MatterPostCommissioningResult cache;
    if (!getDevices(cache)) return false;
    for (size_t i = 0; i < config.ui.binding_count; ++i) {
        const UiConfigBinding &binding = config.ui.bindings[i];
        const MatterDeviceItem *device = findDevice(binding.node_id, binding.endpoint_id, cache);
        if (!device && (!allowUnassigned || binding.configured)) return false;
        if (device && !device->has_onoff) return false;
    }
    return true;
}

bool enqueueMatter(PendingRequest &pending, MatterUiAction action) {
    MatterUiCommand command;
    command.node_id = pending.node_id;
    command.endpoint_id = pending.endpoint;
    command.action = action;
    command.ui_device_id = CONTROL_CONFIG_UI_DEVICE_MARKER;
    command.batch_id = pending.command_id;
    command.brightness = pending.brightness;
    return matter_controller_ui_enqueue(command) == ESP_OK;
}

void sendCommandResult(PendingRequest &pending, bool success, esp_err_t error, bool stateKnown = false, bool state = false) {
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "command_result");
    cJSON_AddNumberToObject(response, "endpoint", pending.endpoint);
    cJSON_AddStringToObject(response, "action", pending.action == MatterUiAction::SetOn ? "on" :
                             pending.action == MatterUiAction::SetOff ? "off" :
                             pending.action == MatterUiAction::Toggle ? "toggle" : "brightness");
    cJSON_AddBoolToObject(response, "success", success);
    if (stateKnown) cJSON_AddBoolToObject(response, "state", state);
    if (!success) cJSON_AddStringToObject(response, "error", esp_err_to_name(error));
    const uint32_t previousRoute = g_dispatch_web_request;
    g_dispatch_web_request = pending.web_request;
    respond(response, pending.request_id);
    g_dispatch_web_request = previousRoute;
    releasePending(pending);
}

void startIdentify(PendingRequest &pending) {
    bool state = false;
    if (matter_controller_read_onoff(pending.node_id, pending.endpoint, state) != ESP_OK) {
        sendCommandResult(pending, false, ESP_ERR_TIMEOUT);
        return;
    }
    pending.identify = true;
    pending.original_state = state;
    pending.identify_stage = 0;
    const uint32_t boundedDuration = std::min<uint32_t>(MAX_IDENTIFY_MS, std::max<uint32_t>(2000, pending.identify_duration_ms));
    pending.identify_deadline_us = esp_timer_get_time() + static_cast<int64_t>(boundedDuration) * 1000;
    pending.next_identify_us = 0;
    pending.action = state ? MatterUiAction::SetOff : MatterUiAction::SetOn;
    if (!enqueueMatter(pending, pending.action)) sendCommandResult(pending, false, ESP_ERR_TIMEOUT);
}

void handleResult(const MatterUiResult &result) {
    PendingRequest *pending = nullptr;
    for (auto &candidate : g_pending) {
        if (candidate.used && candidate.command_id == result.command.batch_id) { pending = &candidate; break; }
    }
    if (!pending) return;
    if (!result.success) {
        sendCommandResult(*pending, false, result.error);
        return;
    }
    if (!pending->identify) {
        sendCommandResult(*pending, true, ESP_OK, result.state_known, result.state);
        return;
    }
    ++pending->identify_stage;
    if (pending->identify_stage >= 4 || esp_timer_get_time() >= pending->identify_deadline_us) {
        sendCommandResult(*pending, pending->identify_stage >= 4, ESP_ERR_TIMEOUT, true, pending->original_state);
        return;
    }
    pending->next_identify_us = esp_timer_get_time() + 350000;
}

void progressIdentify() {
    const int64_t now = esp_timer_get_time();
    for (auto &pending : g_pending) {
        if (!pending.used || !pending.identify || pending.next_identify_us == 0 || now < pending.next_identify_us) continue;
        if (now >= pending.identify_deadline_us) {
            sendCommandResult(pending, false, ESP_ERR_TIMEOUT);
            continue;
        }
        pending.next_identify_us = 0;
        pending.action = pending.original_state
            ? (pending.identify_stage % 2 == 0 ? MatterUiAction::SetOff : MatterUiAction::SetOn)
            : (pending.identify_stage % 2 == 0 ? MatterUiAction::SetOn : MatterUiAction::SetOff);
        if (!enqueueMatter(pending, pending.action)) sendCommandResult(pending, false, ESP_ERR_TIMEOUT);
    }
}

void sendDevices(uint32_t id, size_t offset, size_t limit) {
    MatterPostCommissioningResult cache;
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "devices");
    if (!getDevices(cache)) { cJSON_Delete(response); sendError(id, "DEVICES_UNAVAILABLE", "Aucun cache Matter disponible"); return; }
    char node[24];
    formatNode(cache.savedNode.node_id, node, sizeof(node));
    cJSON_AddStringToObject(response, "node_id", node);
    cJSON_AddNumberToObject(response, "count", cache.devices.size());
    cJSON_AddNumberToObject(response, "offset", offset);
    cJSON_AddNumberToObject(response, "limit", limit);
    cJSON *items = cJSON_AddArrayToObject(response, "devices");
    const UiNamedConfig *config = activeConfig();
    for (size_t i = offset; i < cache.devices.size() && i < offset + limit; ++i) {
        const MatterDeviceItem &device = cache.devices[i];
        cJSON *item = cJSON_CreateObject();
        bool configured = false;
        const uint32_t roomId = roomFor(config, cache.savedNode.node_id, device.endpoint_id, &configured);
        cJSON_AddNumberToObject(item, "endpoint", device.endpoint_id);
        cJSON_AddStringToObject(item, "name", device.name.c_str());
        cJSON_AddStringToObject(item, "name_source", device.name_source.c_str());
        if (device.reachable_known) cJSON_AddBoolToObject(item, "reachable", device.reachable);
        else cJSON_AddNullToObject(item, "reachable");
        if (device.onoff_state_known) cJSON_AddBoolToObject(item, "onoff", device.onoff_state);
        else cJSON_AddNullToObject(item, "onoff");
        const uint8_t brightness = 0;
        const bool brightnessKnown = false;
        cJSON_AddNumberToObject(item, "brightness", brightness);
        cJSON_AddBoolToObject(item, "brightness_known", brightnessKnown);
        cJSON_AddBoolToObject(item, "supports_onoff", device.has_onoff);
        cJSON_AddBoolToObject(item, "supports_level", device.has_level_control);
        cJSON_AddBoolToObject(item, "supports_color", device.has_color_control);
        cJSON_AddBoolToObject(item, "supports_switch", device.has_switch);
        cJSON_AddNumberToObject(item, "device_type", device.device_type);
        cJSON_AddStringToObject(item, "type_label", device.type_label.c_str());
        cJSON_AddNumberToObject(item, "primary_cluster_class", device.primary_cluster_class);
        if (configured) cJSON_AddNumberToObject(item, "room_id", roomId); else cJSON_AddNullToObject(item, "room_id");
        cJSON_AddBoolToObject(item, "configured", configured);
        cJSON_AddItemToArray(items, item);
    }
    ESP_LOGI(TAG, "devices_sent count=%u", static_cast<unsigned>(cJSON_GetArraySize(items)));
    respond(response, id);
}

const char *discoveryPhaseName(MatterDiscoveryPhase phase) {
    switch (phase) {
        case MatterDiscoveryPhase::IDLE: return "idle";
        case MatterDiscoveryPhase::CONNECTING: return "connecting";
        case MatterDiscoveryPhase::READING_ROOT_DESCRIPTOR: return "reading_root_descriptor";
        case MatterDiscoveryPhase::READING_ENDPOINTS: return "reading_endpoints";
        case MatterDiscoveryPhase::COMPLETE: return "complete";
        case MatterDiscoveryPhase::FAILED: return "failed";
    }
    return "unknown";
}

void sendDiscoveryStatus(uint32_t id) {
    MatterDiscoveryResult result;
    matter_controller_get_discovery_result(result);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "type", "discovery_status");
    cJSON_AddNumberToObject(response, "scan_id", result.scan_id);
    cJSON_AddStringToObject(response, "phase", discoveryPhaseName(result.phase));
    cJSON_AddBoolToObject(response, "scanning", result.phase == MatterDiscoveryPhase::CONNECTING ||
                                             result.phase == MatterDiscoveryPhase::READING_ROOT_DESCRIPTOR ||
                                             result.phase == MatterDiscoveryPhase::READING_ENDPOINTS);
    cJSON_AddNumberToObject(response, "endpoint_count", result.endpoint_count);
    cJSON_AddNumberToObject(response, "device_count", result.endpoint_count);
    cJSON_AddNumberToObject(response, "light_count", result.light_count);
    cJSON_AddNumberToObject(response, "duration_ms", result.duration_ms);
    cJSON_AddBoolToObject(response, "truncated", result.truncated);
    cJSON_AddNumberToObject(response, "error", result.error);
    respond(response, id);
}

void handleRequest(const ConfigRequest &request) {
    cJSON *root = cJSON_ParseWithLength(request.json, request.length);
    if (!root) return;
    const cJSON *cmdValue = field(root, "cmd");
    const char *cmd = cJSON_IsString(cmdValue) ? cmdValue->valuestring : nullptr;
    uint32_t id = 0;
    if (!cmd || (strcmp(cmd, "hello") != 0 && !getRequestId(root, id))) {
        sendError(id, "INVALID_REQUEST", "cmd et request_id sont obligatoires");
        cJSON_Delete(root);
        return;
    }
    if (!strcmp(cmd, "hello")) (void)getRequestId(root, id);
    if (id != 0 && pendingFor(id)) {
        sendError(id, "DUPLICATE_REQUEST_ID", "request_id déjà utilisé");
        cJSON_Delete(root);
        return;
    }
    ESP_LOGI(TAG, "request id=%lu cmd=%s", static_cast<unsigned long>(id), cmd);

    if (!strcmp(cmd, "hello")) {
        const cJSON *protocol = field(root, "protocol");
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "hello");
        cJSON_AddNumberToObject(response, "protocol", PROTOCOL_VERSION);
        cJSON_AddStringToObject(response, "firmware", esp_app_get_description()->version);
        cJSON_AddBoolToObject(response, "controller_ready", matter_controller_is_ready());
        MatterSavedNode node;
        const bool commissioned = matter_controller_load_saved_node(node) == ESP_OK && node.commissioned;
        cJSON_AddBoolToObject(response, "matter_commissioned", commissioned);
        char nodeId[24]; formatNode(node.node_id, nodeId, sizeof(nodeId));
        cJSON_AddStringToObject(response, "remote_node_id", nodeId);
        cJSON_AddNumberToObject(response, "config_schema", CONTROL_UI_CONFIG_SCHEMA_VERSION);
        cJSON_AddStringToObject(response, "wifi_url", control_config_web_url().c_str());
        if (!request.web_request) cJSON_AddStringToObject(response, "wifi_access_code", control_config_web_access_code());
        respond(response, id);
        ESP_LOGI(TAG, "client_hello protocol=%d", cJSON_IsNumber(protocol) ? protocol->valueint : 0);
        cJSON_Delete(root);
        return;
    }
    if (!strcmp(cmd, "get_status")) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "status");
        cJSON_AddBoolToObject(response, "controller_ready", matter_controller_is_ready());
        cJSON_AddNumberToObject(response, "protocol", PROTOCOL_VERSION);
        respond(response, id);
        cJSON_Delete(root);
        return;
    }
    if (!strcmp(cmd, "get_discovery_status")) {
        sendDiscoveryStatus(id);
        cJSON_Delete(root);
        return;
    }
    if (!strcmp(cmd, "get_devices")) {
        const cJSON *offset = field(root, "offset");
        const cJSON *limit = field(root, "limit");
        const size_t start = offset && cJSON_IsNumber(offset) && offset->valueint >= 0 ? static_cast<size_t>(offset->valueint) : 0;
        const size_t page = limit && cJSON_IsNumber(limit) && limit->valueint > 0 ? std::min<size_t>(16, limit->valueint) : 8;
        sendDevices(id, start, page);
        cJSON_Delete(root);
        return;
    }
    ensureConfigs();
    if (!strcmp(cmd, "get_rooms")) {
        cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "rooms");
        const UiNamedConfig *config = activeConfig(); cJSON_AddNumberToObject(response, "config_id", config ? config->config_id : 0);
        cJSON *rooms = cJSON_AddArrayToObject(response, "rooms");
        if (config) for (size_t i = 0; i < config->ui.room_count; ++i) cJSON_AddItemToArray(rooms, roomJson(config->ui.rooms[i]));
        respond(response, id); cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "get_configs")) {
        cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "configs");
        cJSON_AddNumberToObject(response, "active_config_id", g_configs->active_config_id);
        cJSON *items = cJSON_AddArrayToObject(response, "configs");
        if (!items) { cJSON_Delete(response); sendError(id, "RESPONSE_FAILED", "Mémoire insuffisante"); cJSON_Delete(root); return; }
        for (size_t i = 0; i < g_configs->config_count; ++i) cJSON_AddItemToArray(items, configJson(g_configs->configs[i], g_configs->configs[i].config_id == g_configs->active_config_id));
        respond(response, id); cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "get_config") || !strcmp(cmd, "export_config")) {
        const cJSON *cfgId = field(root, "config_id");
        const uint32_t requested = cfgId && cJSON_IsNumber(cfgId) ? static_cast<uint32_t>(cfgId->valueint) : g_configs->active_config_id;
        UiNamedConfig *config = configById(requested);
        if (!config) sendError(id, "CONFIG_NOT_FOUND", "Configuration inconnue");
        else { cJSON *response = configJson(*config, requested == g_configs->active_config_id); cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString(!strcmp(cmd, "export_config") ? "config_export" : "config")); respond(response, id); }
        cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "create_config")) {
        if (g_configs->config_count >= CONTROL_UI_CONFIG_MAX_CONFIGS) { sendError(id, "CONFIG_LIMIT", "Trop de configurations"); cJSON_Delete(root); return; }
        const cJSON *name = field(root, "name");
        if (!cJSON_IsString(name) || strlen(name->valuestring) >= CONTROL_UI_CONFIG_NAME_LEN) { sendError(id, "INVALID_NAME", "Nom invalide"); cJSON_Delete(root); return; }
        uint32_t newId = 1; for (size_t i = 0; i < g_configs->config_count; ++i) newId = std::max(newId, g_configs->configs[i].config_id + 1);
        UiNamedConfig &config = g_configs->configs[g_configs->config_count++]; config = UiNamedConfig{}; config.config_id = newId; strncpy(config.name, name->valuestring, sizeof(config.name) - 1); config.ui.schema_version = 1; config.revision = 1;
        cJSON *response = configJson(config, false); cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("config_created")); respond(response, id); cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "duplicate_config")) {
        if (g_configs->config_count >= CONTROL_UI_CONFIG_MAX_CONFIGS) { sendError(id, "CONFIG_LIMIT", "Trop de configurations"); cJSON_Delete(root); return; }
        const cJSON *sourceId = field(root, "config_id"); const cJSON *name = field(root, "name");
        UiNamedConfig *source = sourceId && cJSON_IsNumber(sourceId) ? configById(sourceId->valueint) : nullptr;
        if (!source) { sendError(id, "CONFIG_NOT_FOUND", "Configuration inconnue"); cJSON_Delete(root); return; }
        uint32_t newId = 1; for (size_t i = 0; i < g_configs->config_count; ++i) newId = std::max(newId, g_configs->configs[i].config_id + 1);
        UiNamedConfig &copy = g_configs->configs[g_configs->config_count++]; copy = *source; copy.config_id = newId; if (cJSON_IsString(name) && strlen(name->valuestring) < sizeof(copy.name)) strncpy(copy.name, name->valuestring, sizeof(copy.name) - 1); else snprintf(copy.name, sizeof(copy.name), "%s copie", source->name);
        cJSON *response = configJson(copy, false); cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("config_created")); respond(response, id); cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "rename_config")) {
        const cJSON *cfgId = field(root, "config_id"); const cJSON *name = field(root, "name"); UiNamedConfig *config = cfgId && cJSON_IsNumber(cfgId) ? configById(cfgId->valueint) : nullptr;
        if (!config || !cJSON_IsString(name) || strlen(name->valuestring) >= sizeof(config->name)) sendError(id, "INVALID_CONFIG", "Configuration ou nom invalide");
        else { strncpy(config->name, name->valuestring, sizeof(config->name) - 1); cJSON *response = configJson(*config, config->config_id == g_configs->active_config_id); cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("config_renamed")); respond(response, id); }
        cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "delete_config")) {
        const cJSON *cfgId = field(root, "config_id"); const uint32_t target = cfgId && cJSON_IsNumber(cfgId) ? cfgId->valueint : 0;
        if (g_configs->config_count <= 1 || target == g_configs->active_config_id) sendError(id, "ACTIVE_CONFIG", "Sélectionnez une autre configuration active");
        else { size_t index = 0; while (index < g_configs->config_count && g_configs->configs[index].config_id != target) ++index; if (index == g_configs->config_count) sendError(id, "CONFIG_NOT_FOUND", "Configuration inconnue"); else { for (; index + 1 < g_configs->config_count; ++index) g_configs->configs[index] = g_configs->configs[index + 1]; --g_configs->config_count; cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "config_deleted"); respond(response, id); } }
        cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "set_active_config")) {
        const cJSON *cfgId = field(root, "config_id"); UiNamedConfig *config = cfgId && cJSON_IsNumber(cfgId) ? configById(cfgId->valueint) : nullptr;
        if (!config) sendError(id, "CONFIG_NOT_FOUND", "Configuration inconnue"); else { g_configs->active_config_id = config->config_id; cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "active_config"); cJSON_AddNumberToObject(response, "config_id", config->config_id); respond(response, id); }
        cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "save_config") || !strcmp(cmd, "update_config") || !strcmp(cmd, "import_config")) {
        const cJSON *configObject = field(root, "config");
        UiNamedConfig parsed{};
        UiNamedConfig *target = parseConfig(configObject, parsed) ? configById(parsed.config_id) : nullptr;
        const bool canImport = !strcmp(cmd, "import_config") && g_configs->config_count < CONTROL_UI_CONFIG_MAX_CONFIGS;
        if ((!target && !canImport) || !validateEndpoints(parsed, true)) {
            ESP_LOGW(TAG, "config_validate id=%lu result=invalid", static_cast<unsigned long>(parsed.config_id));
            sendError(id, "INVALID_CONFIG", "Configuration invalide ou endpoint inconnu");
            cJSON_Delete(root);
            return;
        }
        if (!target) {
            target = &g_configs->configs[g_configs->config_count++];
            *target = UiNamedConfig{};
            target->config_id = parsed.config_id;
        }
        parsed.revision = target->revision + 1; parsed.modified_at = static_cast<uint64_t>(esp_timer_get_time() / 1000000ULL); *target = parsed;
        if (!strcmp(cmd, "save_config")) {
            ESP_LOGI(TAG, "config_validate id=%lu result=valid", static_cast<unsigned long>(target->config_id));
            const esp_err_t saveErr = control_ui_config_collection_save(*g_configs);
            if (saveErr != ESP_OK) { ESP_LOGW(TAG, "config_save id=%lu bytes=0 result=%s", static_cast<unsigned long>(target->config_id), esp_err_to_name(saveErr)); sendError(id, "CONFIG_SAVE_FAILED", esp_err_to_name(saveErr)); cJSON_Delete(root); return; }
            const bool verifiedOk = control_ui_config_collection_load(*g_verified_configs) == ESP_OK && g_verified_configs->active_config_id == g_configs->active_config_id;
            ESP_LOGI(TAG, "config_save id=%lu bytes=%u result=ok", static_cast<unsigned long>(target->config_id), static_cast<unsigned>(sizeof(UiNamedConfig)));
            ESP_LOGI(TAG, "config_verify id=%lu result=%s", static_cast<unsigned long>(target->config_id), verifiedOk ? "ok" : "failed");
            if (!verifiedOk) { sendError(id, "CONFIG_VERIFY_FAILED", "Lecture de vérification échouée"); cJSON_Delete(root); return; }
            g_config_update_id = g_configs->active_config_id;
            g_config_update = true;
            cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "config_saved"); cJSON_AddNumberToObject(response, "config_id", target->config_id); cJSON_AddNumberToObject(response, "bytes", sizeof(UiNamedConfig)); cJSON_AddBoolToObject(response, "verified", true); respond(response, id);
        } else { cJSON *response = configJson(*target, target->config_id == g_configs->active_config_id); cJSON_ReplaceItemInObject(response, "type", cJSON_CreateString("config_updated")); respond(response, id); }
        cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "reload_config")) {
        g_configs_loaded = false; ensureConfigs(); cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "config_reloaded"); cJSON_AddNumberToObject(response, "config_id", g_configs->active_config_id); respond(response, id); g_config_update_id = g_configs->active_config_id; g_config_update = true; cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "reset_ui_config")) {
        const cJSON *confirm = field(root, "confirm");
        if (!cJSON_IsString(confirm) || strcmp(confirm->valuestring, "RESET_UI_ONLY")) sendError(id, "CONFIRM_REQUIRED", "Confirmation requise");
        else { const esp_err_t err = control_ui_config_clear(); if (err != ESP_OK) sendError(id, "RESET_FAILED", esp_err_to_name(err)); else { g_configs_loaded = false; g_config_update_id = 0; g_config_update = true; cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "ui_config_reset"); cJSON_AddStringToObject(response, "preserved", "Matter fabric, DIRIGERA node, Wi-Fi, device cache, display and touch"); respond(response, id); } }
        cJSON_Delete(root); return;
    }
    if (!strcmp(cmd, "rescan_devices")) {
        const esp_err_t err = matter_controller_start_device_discovery();
        MatterDiscoveryResult scan;
        matter_controller_get_discovery_result(scan);
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "type", "rescan_started");
        cJSON_AddBoolToObject(response, "accepted", err == ESP_OK);
        cJSON_AddNumberToObject(response, "scan_id", scan.scan_id);
        cJSON_AddBoolToObject(response, "scanning", err == ESP_OK);
        if (err != ESP_OK) cJSON_AddStringToObject(response, "error", esp_err_to_name(err));
        respond(response, id);
        cJSON_Delete(root);
        return;
    }
    if (!strcmp(cmd, "device_on") || !strcmp(cmd, "device_off") || !strcmp(cmd, "device_toggle") || !strcmp(cmd, "set_brightness") || !strcmp(cmd, "device_identify") || !strcmp(cmd, "read_device_state")) {
        uint64_t node = 0; uint16_t endpoint = 0; static MatterPostCommissioningResult cache;
        if (!parseNode(field(root, "node_id"), node)) { MatterSavedNode saved; if (matter_controller_load_saved_node(saved) == ESP_OK) node = saved.node_id; }
        const cJSON *endpointValue = field(root, "endpoint"); if (cJSON_IsNumber(endpointValue)) endpoint = static_cast<uint16_t>(endpointValue->valueint);
        const MatterDeviceItem *device = findDevice(node, endpoint, cache);
        if (!matter_controller_is_ready() || !device || !device->has_onoff || (device->reachable_known && !device->reachable)) { sendError(id, "DEVICE_NOT_FOUND", "Endpoint absent, non OnOff ou inaccessible"); cJSON_Delete(root); return; }
        if (!strcmp(cmd, "read_device_state")) { bool state = false; const esp_err_t err = matter_controller_read_onoff(node, endpoint, state); cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "device_state"); cJSON_AddNumberToObject(response, "endpoint", endpoint); cJSON_AddBoolToObject(response, "success", err == ESP_OK); if (err == ESP_OK) cJSON_AddBoolToObject(response, "state", state); else cJSON_AddStringToObject(response, "error", esp_err_to_name(err)); respond(response, id); cJSON_Delete(root); return; }
        PendingRequest *pending = newPending(id);
        if (!pending) { sendError(id, "BUSY", "Trop de commandes en attente"); cJSON_Delete(root); return; }
        pending->node_id = node; pending->endpoint = endpoint;
        if (!strcmp(cmd, "device_identify")) {
            const cJSON *duration = field(root, "duration_ms"); if (duration && (!cJSON_IsNumber(duration) || duration->valueint < 1 || duration->valueint > MAX_IDENTIFY_MS)) { sendError(id, "INVALID_DURATION", "Durée invalide"); releasePending(*pending); cJSON_Delete(root); return; }
            if (duration) pending->identify_duration_ms = static_cast<uint32_t>(duration->valueint);
            ESP_LOGI(TAG, "identify endpoint=%u", endpoint); startIdentify(*pending); cJSON_Delete(root); return;
        }
        if (!strcmp(cmd, "set_brightness")) { const cJSON *value = field(root, "brightness"); if (!cJSON_IsNumber(value) || value->valueint < 0 || value->valueint > 254 || !device->has_level_control) { sendError(id, "INVALID_BRIGHTNESS", "LevelControl absent ou valeur invalide"); releasePending(*pending); cJSON_Delete(root); return; } pending->action = MatterUiAction::SetBrightness; pending->brightness = static_cast<uint8_t>(value->valueint); ESP_LOGI(TAG, "brightness endpoint=%u value=%u", endpoint, pending->brightness); }
        else pending->action = !strcmp(cmd, "device_on") ? MatterUiAction::SetOn : !strcmp(cmd, "device_off") ? MatterUiAction::SetOff : MatterUiAction::Toggle;
        ESP_LOGI(TAG, "device_command endpoint=%u action=%s", endpoint, cmd);
        if (!enqueueMatter(*pending, pending->action)) { sendCommandResult(*pending, false, ESP_ERR_TIMEOUT); cJSON_Delete(root); return; }
        cJSON *response = cJSON_CreateObject(); cJSON_AddStringToObject(response, "type", "command_queued"); cJSON_AddNumberToObject(response, "endpoint", endpoint); cJSON_AddStringToObject(response, "action", cmd); respond(response, id); cJSON_Delete(root); return;
    }
    sendError(id, "UNKNOWN_COMMAND", "Commande non autorisée");
    cJSON_Delete(root);
}

void consumeInput(const char *data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        const char ch = data[i];
        if (ch == '\n') {
            if (!g_line_discard && g_line_length > 0 && g_line_length <= MAX_FRAME - 1 && strncmp(g_line, "@@CFG:", 6) == 0) {
                ESP_LOGI(TAG, "rx_frame bytes=%u", static_cast<unsigned>(g_line_length));
                ConfigRequest request;
                request.length = static_cast<uint16_t>(g_line_length - 6);
                request.json = static_cast<char *>(malloc(request.length + 1));
                if (!request.json) {
                    ESP_LOGW(TAG, "request_alloc_failed bytes=%u", static_cast<unsigned>(request.length + 1));
                    logInitHeap("request");
                } else {
                    memcpy(request.json, g_line + 6, request.length);
                    request.json[request.length] = '\0';
                    cJSON *parsed = cJSON_ParseWithLength(request.json, request.length);
                    if (parsed && cJSON_IsObject(parsed)) {
                        const cJSON *idValue = field(parsed, "request_id");
                        const cJSON *cmdValue = field(parsed, "cmd");
                        request.request_id = cJSON_IsNumber(idValue) ? static_cast<uint32_t>(idValue->valuedouble) : 0;
                        const char *cmd = cJSON_IsString(cmdValue) ? cmdValue->valuestring : "invalid";
                        strncpy(request.cmd, cmd, sizeof(request.cmd) - 1);
                        request.cmd[sizeof(request.cmd) - 1] = '\0';
                        ESP_LOGI(TAG, "rx_request id=%lu cmd=%s", static_cast<unsigned long>(request.request_id), request.cmd);
                        ESP_LOGI("CONTROL_CFG", "request_received=%lu", static_cast<unsigned long>(request.request_id));
                        const BaseType_t queued = g_requests ? xQueueSend(g_requests, &request, 0) : pdFAIL;
                        ESP_LOGI(TAG, "enqueue_request id=%lu result=%s", static_cast<unsigned long>(request.request_id), queued == pdPASS ? "ok" : "fail");
                        if (queued != pdPASS) {
                            ESP_LOGE(TAG, "command_failed id=%lu cmd=%s step=request_queue error=queue_full code=%d",
                                     static_cast<unsigned long>(request.request_id), request.cmd, -1);
                            free(request.json);
                        }
                    } else if (!parsed) {
                        ESP_LOGW(TAG, "invalid_request reason=malformed_json");
                        free(request.json);
                    } else if (!cJSON_IsObject(parsed)) {
                        ESP_LOGW(TAG, "invalid_request reason=not_object");
                        free(request.json);
                    }
                    cJSON_Delete(parsed);
                }
            }
            g_line_length = 0; g_line_discard = false; continue;
        }
        if (ch == '\r') continue;
        if (g_line_length >= MAX_FRAME - 1) { g_line_discard = true; continue; }
        g_line[g_line_length++] = ch; g_line[g_line_length] = '\0';
    }
}

bool primaryInputReady() {
#if CONFIG_ESP_CONSOLE_USB_CDC
    esp_usb_console_poll_interrupts();
    return esp_usb_console_available_for_read() > 0;
#else
    return true;
#endif
}

void drainResponses() {
    ConfigResponse response;
    while (g_responses && xQueueReceive(g_responses, &response, 0) == pdTRUE) {
        ESP_LOGI(TAG, "serial_dequeue_response id=%lu bytes=%u",
                 static_cast<unsigned long>(response.request_id), static_cast<unsigned>(response.length));
        if (response.web_request) {
            if (xQueueSend(g_web_responses, &response, 0) == pdTRUE) continue;
            ESP_LOGW(TAG, "web_response_dropped id=%lu", static_cast<unsigned long>(response.request_id));
        } else sendFrame(response.json);
        ESP_LOGI(TAG, "tx_response id=%lu type=%s bytes=%u",
                 static_cast<unsigned long>(response.request_id), response.type,
                 static_cast<unsigned>(response.length));
        cJSON_free(response.json);
    }
}

void serialTask(void *) {
    ESP_LOGI(TAG, "serial_ready");
    ESP_LOGI(TAG, "serial_transport=%s", transportName(g_transport));
    for (;;) {
        ConfigRequest request;
        if (xQueueReceive(g_requests, &request, pdMS_TO_TICKS(20)) == pdTRUE) {
            ESP_LOGI(TAG, "worker_dequeue id=%lu cmd=%s", static_cast<unsigned long>(request.request_id), request.cmd);
            g_dispatch_request_id = request.request_id;
            g_dispatch_web_request = request.web_request;
            strncpy(g_dispatch_cmd, request.cmd, sizeof(g_dispatch_cmd) - 1);
            g_dispatch_cmd[sizeof(g_dispatch_cmd) - 1] = '\0';
            const int64_t requestStartedUs = esp_timer_get_time();
            ESP_LOGI("CONTROL_CFG", "request_started=%lu", static_cast<unsigned long>(request.request_id));
            ESP_LOGI(TAG, "dispatch_start id=%lu cmd=%s", static_cast<unsigned long>(request.request_id), request.cmd);
            handleRequest(request);
            ESP_LOGI("CONTROL_CFG", "request_completed=%lu duration_ms=%u",
                     static_cast<unsigned long>(request.request_id),
                     static_cast<unsigned>((esp_timer_get_time() - requestStartedUs) / 1000));
            g_dispatch_request_id = 0;
            g_dispatch_web_request = 0;
            g_dispatch_cmd[0] = '\0';
            free(request.json);
        }
        MatterUiResult result;
        while (xQueueReceive(g_results, &result, 0) == pdTRUE) handleResult(result);
        drainResponses();
        progressIdentify();
        char data[64];
        if (primaryInputReady()) {
            const ssize_t count = read(g_input_fd, data, sizeof(data));
            if (count > 0) {
                ESP_LOGI(TAG, "rx_bytes count=%d", static_cast<int>(count));
                consumeInput(data, static_cast<size_t>(count));
            } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                static bool readErrorLogged = false;
                if (!readErrorLogged) {
                    ESP_LOGW(TAG, "input_read_failed errno=%d", errno);
                    readErrorLogged = true;
                }
            }
        }
        drainResponses();
    }
}
}

esp_err_t control_config_serial_init() {
    if (g_task) return ESP_OK;
    // ponytail: one lock serializes protocol frames with ESP-IDF logs on the shared console.
    g_console_mutex = xSemaphoreCreateMutexStatic(&g_console_mutex_storage);
    g_previous_log_vprintf = esp_log_set_vprintf(serializedLogVprintf);
    ESP_LOGI(TAG, "serial_init_start");
    configureJsonAllocator();
    ESP_LOGI(TAG, "json_allocator=psram");
    g_transport = configuredTransport();
    ESP_LOGI(TAG, "transport=%s", transportName(g_transport));
    ESP_LOGI(TAG, "console_primary=%s", primaryConsolePath());
    ESP_LOGI(TAG, "console_secondary=%s", secondaryConsolePath());
    ESP_LOGI(TAG, "rx_source=primary");
    g_requests = xQueueCreate(REQUEST_QUEUE_LENGTH, sizeof(ConfigRequest));
    ESP_LOGI(TAG, "init_step=queue result=%s code=%d", g_requests ? "ok" : "failed", g_requests ? ESP_OK : ESP_ERR_NO_MEM);
    if (!g_requests) return initFailure("queue", "queue_create_failed", ESP_ERR_NO_MEM);
    g_results = xQueueCreate(RESULT_QUEUE_LENGTH, sizeof(MatterUiResult));
    ESP_LOGI(TAG, "init_step=result_queue result=%s code=%d", g_results ? "ok" : "failed", g_results ? ESP_OK : ESP_ERR_NO_MEM);
    if (!g_results) return initFailure("result_queue", "result_queue_create_failed", ESP_ERR_NO_MEM);
    g_responses = xQueueCreate(RESPONSE_QUEUE_LENGTH, sizeof(ConfigResponse));
    ESP_LOGI(TAG, "init_step=response_queue result=%s code=%d", g_responses ? "ok" : "failed", g_responses ? ESP_OK : ESP_ERR_NO_MEM);
    if (!g_responses) return initFailure("response_queue", "response_queue_create_failed", ESP_ERR_NO_MEM);
    g_web_responses = xQueueCreate(RESPONSE_QUEUE_LENGTH, sizeof(ConfigResponse));
    if (!g_web_responses) return initFailure("web_response_queue", "queue_create_failed", ESP_ERR_NO_MEM);
    // Configuration copies are CPU-only; reserve internal RAM for DMA and the network.
    g_configs = static_cast<UiConfigCollection *>(configJsonAllocate(sizeof(UiConfigCollection)));
    if (!g_configs) return initFailure("config_buffers", "config_buffer_alloc_failed", ESP_ERR_NO_MEM);
    new (g_configs) UiConfigCollection{};
    g_verified_configs = static_cast<UiConfigCollection *>(configJsonAllocate(sizeof(UiConfigCollection)));
    if (!g_verified_configs) return initFailure("config_buffers", "verify_buffer_alloc_failed", ESP_ERR_NO_MEM);
    new (g_verified_configs) UiConfigCollection{};
    // Keep the receive frame out of the internal RAM needed by display DMA and TCP.
    g_line = static_cast<char *>(configJsonAllocate(MAX_FRAME));
    if (!g_line) return initFailure("parser_buffers", "parser_buffer_alloc_failed", ESP_ERR_NO_MEM);
    memset(g_line, 0, MAX_FRAME);
    ESP_LOGI(TAG, "init_step=parser_buffers result=ok code=0 bytes=%u", static_cast<unsigned>(MAX_FRAME));
    memset(g_pending, 0, sizeof(g_pending));
    ESP_LOGI(TAG, "init_step=pending_table result=ok code=0 entries=%u", static_cast<unsigned>(MAX_PENDING));

    int transportError = 0;
    const esp_err_t transportResult = initTransport(transportError);
    ESP_LOGI(TAG, "init_step=transport result=%s code=%d", transportResult == ESP_OK ? "ok" : "failed", transportResult == ESP_OK ? 0 : transportError);
    if (transportResult != ESP_OK) return initFailure("transport", "transport_init_failed", transportError != 0 ? transportError : transportResult);

    TaskHandle_t task = nullptr;
    const BaseType_t taskResult = xTaskCreatePinnedToCoreWithCaps(
        serialTask, "control_cfg", SERIAL_TASK_STACK_SIZE, nullptr, 3, &task, 1,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "init_step=task result=%s code=%ld", taskResult == pdPASS ? "ok" : "failed", static_cast<long>(taskResult == pdPASS ? 0 : taskResult));
    if (taskResult != pdPASS) return initFailure("task", "task_create_failed", static_cast<int>(taskResult));
    g_task = task;
    return ESP_OK;
}

esp_err_t control_config_serial_web_request(const char *json, size_t length, char **response) {
    if (!response) return ESP_ERR_INVALID_ARG;
    *response = nullptr;
    if (!g_task || !g_requests || !g_web_responses) return ESP_ERR_INVALID_STATE;
    if (!json || length == 0 || length > MAX_FRAME - 7) return ESP_ERR_INVALID_SIZE;
    ConfigRequest request;
    cJSON *parsed = cJSON_ParseWithLength(json, length);
    const cJSON *cmd = field(parsed, "cmd");
    const bool valid = cJSON_IsObject(parsed) && cJSON_IsString(cmd) &&
        strlen(cmd->valuestring) < sizeof(request.cmd) && getRequestId(parsed, request.request_id);
    if (valid) strncpy(request.cmd, cmd->valuestring, sizeof(request.cmd) - 1);
    cJSON_Delete(parsed);
    if (!valid) return ESP_ERR_INVALID_ARG;
    static uint32_t nextRequest = 0;
    if (++nextRequest == 0) ++nextRequest;
    request.web_request = nextRequest;
    request.length = static_cast<uint16_t>(length);
    request.json = static_cast<char *>(heap_caps_malloc(length + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!request.json) return ESP_ERR_NO_MEM;
    memcpy(request.json, json, length);
    request.json[length] = '\0';
    if (xQueueSend(g_requests, &request, 0) != pdTRUE) { free(request.json); return ESP_ERR_NO_MEM; }
    const int64_t deadline = esp_timer_get_time() + 60000000;
    while (esp_timer_get_time() < deadline) {
        ConfigResponse reply;
        if (xQueueReceive(g_web_responses, &reply, pdMS_TO_TICKS(100)) != pdTRUE) continue;
        if (reply.web_request == request.web_request && strcmp(reply.type, "command_queued") != 0) {
            *response = reply.json;
            return ESP_OK;
        }
        cJSON_free(reply.json);
    }
    return ESP_ERR_TIMEOUT;
}

void control_config_serial_handle_matter_result(const MatterUiResult &result) {
    if (g_results && result.command.ui_device_id == CONTROL_CONFIG_UI_DEVICE_MARKER) (void)xQueueSend(g_results, &result, 0);
}

bool control_config_serial_take_config_update() {
    const bool changed = g_config_update;
    g_config_update = false;
    return changed;
}

uint32_t control_config_serial_take_active_config_id() {
    const uint32_t id = g_config_update_id;
    g_config_update_id = 0;
    return id;
}

#else

esp_err_t control_config_serial_init() { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t control_config_serial_web_request(const char *, size_t, char **) { return ESP_ERR_NOT_SUPPORTED; }
void control_config_serial_handle_matter_result(const MatterUiResult &) {}
bool control_config_serial_take_config_update() { return false; }
uint32_t control_config_serial_take_active_config_id() { return 0; }

#endif
