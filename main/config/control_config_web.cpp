#include "config/control_config_web.h"
#include "sdkconfig.h"

#if CONFIG_CONTROL_WEB_SERIAL_CONFIGURATOR
#include <cstdio>
#include <cstring>
#include "cJSON.h"
#include "config/control_config_serial.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace {
constexpr const char *TAG = "CONTROL_CONFIG_WEB";
constexpr size_t MAX_JSON = 8192 - 7;
char accessCode[17] = {};
httpd_handle_t server = nullptr;

#define WEB_ASSET(name) \
    extern const char name##_start[] asm("_binary_control_web_" #name "_start"); \
    extern const char name##_end[] asm("_binary_control_web_" #name "_end")
WEB_ASSET(index_html);
WEB_ASSET(app_js);
WEB_ASSET(model_js);
WEB_ASSET(protocol_js);
WEB_ASSET(wifi_js);
WEB_ASSET(style_css);
#undef WEB_ASSET

struct Asset { const char *uri; const char *type; const char *start; const char *end; };
const Asset assets[] = {
    {"/", "text/html; charset=utf-8", index_html_start, index_html_end},
    {"/index.html", "text/html; charset=utf-8", index_html_start, index_html_end},
    {"/app.js", "text/javascript; charset=utf-8", app_js_start, app_js_end},
    {"/model.js", "text/javascript; charset=utf-8", model_js_start, model_js_end},
    {"/protocol.js", "text/javascript; charset=utf-8", protocol_js_start, protocol_js_end},
    {"/wifi.js", "text/javascript; charset=utf-8", wifi_js_start, wifi_js_end},
    {"/style.css", "text/css; charset=utf-8", style_css_start, style_css_end},
};

esp_err_t loadAccessCode() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open("control_web", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    size_t size = sizeof(accessCode);
    err = nvs_get_str(handle, "access_code", accessCode, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t random[8];
        esp_fill_random(random, sizeof(random));
        for (size_t i = 0; i < sizeof(random); ++i) snprintf(accessCode + i * 2, 3, "%02x", random[i]);
        err = nvs_set_str(handle, "access_code", accessCode);
        if (err == ESP_OK) err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err == ESP_OK && (strlen(accessCode) != 16 || strspn(accessCode, "0123456789abcdef") != 16)) err = ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) accessCode[0] = '\0';
    return err;
}

bool authorized(httpd_req_t *request) {
    char authorization[24];
    if (!accessCode[0] || httpd_req_get_hdr_value_len(request, "Authorization") != 23 ||
        httpd_req_get_hdr_value_str(request, "Authorization", authorization, sizeof(authorization)) != ESP_OK ||
        strncmp(authorization, "Bearer ", 7) != 0) return false;
    unsigned mismatch = 0;
    for (size_t i = 0; i < 16; ++i) mismatch |= authorization[i + 7] ^ accessCode[i];
    return mismatch == 0;
}

esp_err_t serveAsset(httpd_req_t *request) {
    const auto *asset = static_cast<const Asset *>(request->user_ctx);
    httpd_resp_set_type(request, asset->type);
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; object-src 'none'");
    return httpd_resp_send(request, asset->start, asset->end - asset->start - 1);
}

esp_err_t handleJson(httpd_req_t *request) {
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (!authorized(request)) {
        vTaskDelay(pdMS_TO_TICKS(250));
        httpd_resp_set_status(request, "401 Unauthorized");
        return httpd_resp_send(request, "Code d'accès incorrect", HTTPD_RESP_USE_STRLEN);
    }
    char contentType[48];
    if (httpd_req_get_hdr_value_str(request, "Content-Type", contentType, sizeof(contentType)) != ESP_OK ||
        (strcmp(contentType, "application/json") != 0 && strcmp(contentType, "application/json; charset=utf-8") != 0)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "JSON requis");
    }
    if (request->content_len == 0 || request->content_len > MAX_JSON) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Taille JSON invalide");
    }
    auto *body = static_cast<char *>(heap_caps_malloc(request->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) {
        httpd_resp_set_status(request, "503 Service Unavailable");
        return httpd_resp_send(request, "Mémoire insuffisante", HTTPD_RESP_USE_STRLEN);
    }
    size_t received = 0;
    while (received < request->content_len) {
        const int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count <= 0) {
            heap_caps_free(body);
            if (count == HTTPD_SOCK_ERR_TIMEOUT) httpd_resp_send_408(request);
            return ESP_FAIL;
        }
        received += count;
    }
    body[received] = '\0';
    char *response = nullptr;
    const esp_err_t err = control_config_serial_web_request(body, received, &response);
    heap_caps_free(body);
    if (err != ESP_OK) {
        httpd_resp_set_status(request, err == ESP_ERR_INVALID_ARG || err == ESP_ERR_INVALID_SIZE ?
                              "400 Bad Request" : "503 Service Unavailable");
        return httpd_resp_send(request, esp_err_to_name(err), HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(request, "application/json");
    const esp_err_t sent = httpd_resp_send(request, response, HTTPD_RESP_USE_STRLEN);
    cJSON_free(response);
    return sent;
}
}

esp_err_t control_config_web_init() {
    if (server) return ESP_OK;
    esp_err_t err = loadAccessCode();
    if (err != ESP_OK) return err;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    // Keep internal RAM available for Matter cryptography and display DMA.
    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    config.max_uri_handlers = 8;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    err = httpd_start(&server, &config);
    if (err != ESP_OK) { server = nullptr; return err; }
    for (const auto &asset : assets) {
        httpd_uri_t route{};
        route.uri = asset.uri;
        route.method = HTTP_GET;
        route.handler = serveAsset;
        route.user_ctx = const_cast<Asset *>(&asset);
        err = httpd_register_uri_handler(server, &route);
        if (err != ESP_OK) break;
    }
    if (err == ESP_OK) {
        httpd_uri_t route{};
        route.uri = "/api/config";
        route.method = HTTP_POST;
        route.handler = handleJson;
        err = httpd_register_uri_handler(server, &route);
    }
    if (err != ESP_OK) { httpd_stop(server); server = nullptr; return err; }
    ESP_LOGI(TAG, "server_ready port=80 authentication=bearer");
    return ESP_OK;
}

std::string control_config_web_url() {
    if (!server) return {};
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info{};
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) return {};
    char url[32];
    snprintf(url, sizeof(url), "http://" IPSTR, IP2STR(&info.ip));
    return url;
}

const char *control_config_web_access_code() { return accessCode; }
#else
esp_err_t control_config_web_init() { return ESP_ERR_NOT_SUPPORTED; }
std::string control_config_web_url() { return {}; }
const char *control_config_web_access_code() { return ""; }
#endif
