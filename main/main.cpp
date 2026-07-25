#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <string>
#include <cstring>
#include <cctype>
#include "app_state.h"
#include "board/board_config.h"
#include "display/display_manager.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ikea/ikea_discovery.h"
#include "matter/matter_controller.h"
#include "matter/matter_onboarding.h"
#include "matter/matter_pairing_code.h"
#include "storage/settings_store.h"
#include "system/control_heap.h"
#include "system/control_time.h"
#include "touch/touch_manager.h"
#include "ui/ui_manager.h"
#include "wifi/wifi_manager.h"
#include "weather/weather_manager.h"
#include "sdkconfig.h"

#ifndef CONFIG_CONTROL_PASSWORD_PARTIALS_BEFORE_FULL_REFRESH
#define CONFIG_CONTROL_PASSWORD_PARTIALS_BEFORE_FULL_REFRESH 20
#endif

namespace {
constexpr const char *TAG = "CONTROL_APP";
constexpr int64_t CONNECTION_TIMEOUT_US = 20LL * 1000 * 1000;
constexpr size_t TOUCH_QUEUE_LENGTH = 8;
constexpr size_t MATTER_DEVICES_PER_PAGE = 4;
constexpr uint32_t MATTER_COMMISSIONING_WORKER_STACK_SIZE = 12288;
constexpr UBaseType_t UI_TASK_PRIORITY = 1;
constexpr UBaseType_t TOUCH_TASK_PRIORITY = 4;
constexpr UBaseType_t MATTER_COMMISSIONING_WORKER_PRIORITY = 8;
constexpr uint32_t MATTER_NETWORK_READY_TIMEOUT_MS = 10000;
constexpr uint32_t MATTER_SNTP_READY_TIMEOUT_MS = 10000;

const char *wifiAuthName(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK: return "WAPI";
        case WIFI_AUTH_OWE: return "OWE";
        default: return "UNKNOWN";
    }
}

#if !CONFIG_CONTROL_RECOVERY_BUILD
void formatMatterNodeId(uint64_t nodeId, char *buf, size_t size) {
    if (!buf || size == 0) return;
    snprintf(buf, size, "0x%08lx%08lx",
             static_cast<unsigned long>(static_cast<uint32_t>(nodeId >> 32)),
             static_cast<unsigned long>(static_cast<uint32_t>(nodeId & 0xffffffffULL)));
}
#endif

const char *keyboardModeName(int mode) {
    switch (mode) {
        case 1: return "upper";
        case 2: return "symbols";
        default: return "lower";
    }
}

const char *ikeaClassificationName(IkeaDiscoveryClassification classification) {
    switch (classification) {
        case IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT: return "IKEA_FOUND_CONFIDENT";
        case IkeaDiscoveryClassification::KNOWN_IP_REACHABLE_UNCONFIRMED: return "KNOWN_IP_REACHABLE_UNCONFIRMED";
        case IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED: return "MATTER_DEVICE_FOUND_UNCONFIRMED";
        case IkeaDiscoveryClassification::LOCAL_MDNS_FOUND_UNCONFIRMED: return "LOCAL_MDNS_FOUND_UNCONFIRMED";
        case IkeaDiscoveryClassification::NOT_FOUND: return "NOT_FOUND";
        case IkeaDiscoveryClassification::DISCOVERY_ERROR: return "DISCOVERY_ERROR";
        default: return "NONE";
    }
}

class ControlApplication {
public:
    ControlApplication() : ui_(display_) {}

    bool initialize() {
        ESP_LOGI(TAG, "Control %s — build %s %s — ESP-IDF %s", CONTROL_FW_VERSION,
                 __DATE__, __TIME__, esp_get_idf_version());
#if CONFIG_CONTROL_RECOVERY_BUILD
        ESP_LOGI(TAG, "recovery_build=yes");
#else
        ESP_LOGI(TAG, "recovery_build=no");
#endif
        ESP_LOGI(TAG, "Carte cible: %s %s — PCB %s — révision physique confirmée",
                 CONTROL_BOARD_NAME, CONTROL_BOARD_REVISION, CONTROL_BOARD_PCB_DATE);
        ESP_LOGI(TAG, "Module attendu: %s, flash 16 Mio QIO, PSRAM 8 Mio OPI",
                 CONTROL_BOARD_MODULE);
        ESP_LOGI(TAG, "CONFIG_ESP_MAIN_TASK_STACK_SIZE=%d bytes", CONFIG_ESP_MAIN_TASK_STACK_SIZE);
        control_heap_log("boot_start");
        transitionState(AppState::BOOT);
        ESP_LOGI(TAG, "boot_phase=nvs");
        if (!storage_.initialize()) {
            ESP_LOGE(TAG, "recovery_boot_stopped reason=nvs_init_failed");
            return false;
        }
        control_heap_log("after_nvs");
        const StoredSettings saved = storage_.load();
        savedSsid_ = saved.ssid;
        savedPassword_ = saved.password;
        control_heap_log("after_minimal_wifi_credentials_load");
        ESP_LOGI("CONTROL_WIFI", "load_wifi_credentials found=%s",
                 saved.hasWifiCredentials() ? "yes" : "no");
        selectedSsid_ = savedSsid_;
        password_ = savedPassword_;
        ESP_LOGI(TAG, "boot_phase=wifi_init");
        const bool wifiOk = wifi_.initialize();
        ESP_LOGI("CONTROL_HEAP", "task_stack_config name=matter_worker size=%u priority=%u",
                 static_cast<unsigned>(MATTER_COMMISSIONING_WORKER_STACK_SIZE),
                 static_cast<unsigned>(MATTER_COMMISSIONING_WORKER_PRIORITY));
        ESP_LOGI("CONTROL_HEAP", "task_stack_config name=matter_discovery size=%u priority=%u", 16384U, 7U);
        ESP_LOGI("CONTROL_HEAP", "task_stack_config name=matter_command size=%u priority=%u", 12288U, 7U);
        ESP_LOGI("CONTROL_HEAP", "task_stack_config name=ui size=%d priority=%u",
                 CONFIG_ESP_MAIN_TASK_STACK_SIZE, static_cast<unsigned>(UI_TASK_PRIORITY));
        ESP_LOGI("CONTROL_HEAP", "task_stack_config name=display size=inline priority=%u",
                 static_cast<unsigned>(UI_TASK_PRIORITY));
        ESP_LOGI("CONTROL_HEAP", "task_stack_config name=touch size=%d priority=%u",
                 CONFIG_CONTROL_TOUCH_TASK_STACK_SIZE, static_cast<unsigned>(TOUCH_TASK_PRIORITY));

#if CONFIG_CONTROL_RECOVERY_BUILD
        ESP_LOGI(TAG, "recovery_matter=disabled");
        ESP_LOGI(TAG, "recovery_wifi_connect=deferred_until_ui");
        ESP_LOGI(TAG, "boot_headless_result wifi=deferred matter=disabled");
#else
        loadSavedMatterNode();
        const bool savedWifiBootPath = wifiOk && !savedSsid_.empty();
        const bool matterPlatformBootPath = savedWifiBootPath;
        esp_err_t wifiHeadlessErr = savedWifiBootPath ? ESP_ERR_TIMEOUT : ESP_ERR_INVALID_STATE;
        bool matterBootReady = false;
        const char *wifiHeadlessResult = savedWifiBootPath ? "timeout" : "skipped";
        const char *matterHeadlessResult = matterPlatformBootPath ? "pending" : "skipped";
        if (savedWifiBootPath) {
            ESP_LOGI(TAG, "boot_phase=wifi_connect");
            control_heap_log("before_wifi_start");
            wifiHeadlessErr = connectSavedWifiHeadless();
            if (wifiHeadlessErr == ESP_OK) {
                wifiHeadlessResult = "connected";
                control_heap_log("after_wifi_got_ip");
                if (matterPlatformBootPath) {
                    ESP_LOGI(TAG, "boot_phase=matter_early_init");
                    esp_err_t networkReady = wifi_.waitMatterNetworkReady(MATTER_NETWORK_READY_TIMEOUT_MS);
                    if (networkReady != ESP_OK) {
                        ESP_LOGW("CONTROL_MATTER", "early_init_network_failed err=%s", esp_err_to_name(networkReady));
                        matterHeadlessResult = "network_not_ready";
                    } else {
                        control_heap_log("before_sntp");
                        (void)control_time_start_sntp_once();
                        (void)control_time_wait_synced(MATTER_SNTP_READY_TIMEOUT_MS);
                        control_heap_log("before_matter_platform_start");
                        ESP_LOGI("CONTROL_MATTER", "early_init_start");
                        matterBootReady = runEarlyMatterInit("boot_saved_wifi", false);
                        matterHeadlessResult = matterBootReady ? "ready" : "failed";
                    }
                }
            } else {
                wifiHeadlessResult = wifiHeadlessErr == ESP_ERR_TIMEOUT ? "timeout" : "failed";
                ESP_LOGI("CONTROL_MATTER", "early_init_skipped reason=wifi_not_connected");
            }
            if (matterPlatformBootPath && !matterBootReady) matterEarlyInitFailed_ = true;
        } else if (savedSsid_.empty()) {
            ESP_LOGI("CONTROL_MATTER", "early_init_skipped reason=no_wifi_credentials");
        } else {
            ESP_LOGI("CONTROL_MATTER", "early_init_skipped reason=wifi_init_failed");
        }
        ESP_LOGI(TAG, "boot_headless_result wifi=%s matter=%s", wifiHeadlessResult, matterHeadlessResult);
#endif

#if CONFIG_CONTROL_RECOVERY_BUILD
        ESP_LOGI(TAG, "weather_init deferred reason=recovery_wifi");
#else
        control_heap_log("before_weather_init");
        (void)weather_init();
        (void)weather_load_cache();
        weather_ensure_placeholder();
        control_heap_log("after_weather_init");
#endif
        ESP_LOGI(TAG, "boot_phase=ui_init");
        control_heap_log("before_ui_init");
        const bool displayOk = display_.initialize();
        control_heap_log("after_display_init");
        const bool touchOk = touch_.initialize();
        control_heap_log("after_touch_init");
        control_heap_log("after_ui_init");
#if !CONFIG_CONTROL_RECOVERY_BUILD
        matter_.initializeControllerPlaceholder();
        matter_controller_log_boot_status();
#endif

        ESP_LOGI(TAG, "Diagnostic: écran=%s tactile=%s Wi-Fi=%s NVS=%s firmware=%s",
                 displayOk ? "OK" : display_.statusText(), touchOk ? "OK" : touch_.statusText(),
                 wifiOk ? "OK" : wifi_.statusText(), storage_.statusText(), CONTROL_FW_VERSION);
#if CONFIG_CONTROL_RECOVERY_BUILD
        state_ = !savedSsid_.empty() ? AppState::WEATHER_HOME : AppState::HARDWARE_TEST;
#else
        if (matterBootReady && matter_controller_is_ready()) {
            MatterPostCommissioningResult cached;
            (void)matter_controller_load_device_cache(cached);
            matterPostResult_ = cached;
            state_ = AppState::WEATHER_HOME;
        } else {
            state_ = !savedSsid_.empty() ? AppState::WEATHER_HOME : AppState::HARDWARE_TEST;
        }
#endif
        (void)saved;
        touchQueue_ = xQueueCreate(TOUCH_QUEUE_LENGTH, sizeof(TouchEvent));
        if (touchQueue_ == nullptr) {
            ESP_LOGE(TAG, "Impossible de créer la file tactile");
            return false;
        }
        ESP_LOGI(TAG, "touch_event_size=%u queue_item_size=%u framebuffer=%p bytes=%u",
                 static_cast<unsigned>(sizeof(TouchEvent)),
                 static_cast<unsigned>(sizeof(TouchEvent)), display_.framebufferAddress(),
                 static_cast<unsigned>(DisplayManager::WIDTH * DisplayManager::HEIGHT / 2));
        return true;
    }

    void renderInitialScreen() {
        if (state_ == AppState::WEATHER_HOME || state_ == AppState::WIFI_CONNECTED || state_ == AppState::MATTER_INTRO) {
            ESP_LOGI(TAG, "boot_phase=weather_home_render");
            ESP_LOGI(TAG, "boot_screen=WEATHER_HOME");
            ESP_LOGI("CONTROL_DISPLAY", "render_start state=WEATHER_HOME");
            renderUi("screen_enter");
        } else {
            renderHardwareTest();
        }
        if (!savedSsid_.empty() && !wifi_.isConnected()) {
            autoConnectAtUs_ = esp_timer_get_time() + 1500LL * 1000;
            ESP_LOGI(TAG, "Connexion Wi-Fi automatique planifiée pour SSID '%s'", savedSsid_.c_str());
        }
        ESP_LOGI(TAG, "idle_after_first_render");
    }

    void runRecoveryIdleLoop() {
        while (true) {
            processWifi();
#if !CONFIG_CONTROL_RECOVERY_BUILD
            processIkea();
            processMatter();
#endif
            processWeather();
            processTimeouts();
            processPendingRender();
            processTouchEvents();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    void runTouchTask() {
        while (true) {
            if (matterCommissioningUiQuiet_.load()) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            const TouchEvent event = touch_.poll();
            if (event.type != TouchEventType::NONE && touchQueue_ != nullptr) {
                (void)xQueueSend(touchQueue_, &event, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    bool startTouchTask() {
        BaseType_t touchResult = xTaskCreatePinnedToCore(
            [](void *arg) { static_cast<ControlApplication *>(arg)->runTouchTask(); },
            "control_touch_task", CONFIG_CONTROL_TOUCH_TASK_STACK_SIZE, this,
            TOUCH_TASK_PRIORITY, &touchTaskHandle_, 1);
        if (touchResult != pdPASS) {
            ESP_LOGE("CONTROL_APP", "Création tâche tactile échouée");
            touchTaskHandle_ = nullptr;
            return false;
        }
        return true;
    }

private:
    void renderHardwareTest() {
        const bool ok = renderUi("screen_enter");
        if (!ok) {
            ESP_LOGE(TAG, "Rendu matériel complet échoué; diagnostics série conservés");
        }
    }

    UiViewModel viewModel() const {
        UiViewModel m;
        m.state = state_;
        m.displayStatus = display_.statusText();
        m.touchStatus = touch_.statusText();
        m.wifiStatus = wifi_.statusText();
        m.storageStatus = storage_.statusText();
        m.networks = wifi_.networks();
        m.networkPage = networkPage_;
        m.selectedSsid = selectedSsid_;
        m.password = password_;
        m.passwordVisible = passwordVisible_;
        m.keyboardMode = keyboardMode_;
        m.openNetworkConfirmation = openNetworkConfirmation_;
        m.failureReason = failureReason_;
        m.connection = wifi_.connectionInfo();
        m.ikea = lastIkeaResult_;
        const TouchDiagnostics touchDiagnostics = touch_.diagnostics();
        m.touchAddress = touchDiagnostics.address;
        m.touchProductId = touchDiagnostics.productId;
        m.touchMode = touchDiagnostics.mode;
        m.touchRawX = touchDiagnostics.rawX;
        m.touchRawY = touchDiagnostics.rawY;
        m.touchX = touchDiagnostics.transformedX;
        m.touchY = touchDiagnostics.transformedY;
        m.touchLatestEvent = touchEventName(touchDiagnostics.latestEvent);
        m.touchInternalState = touchStateName(touchDiagnostics.internalState);
        m.touchIrqLevel = touchDiagnostics.irqLevel;
        m.touchIrqEdgeObserved = touchDiagnostics.irqEdgeObserved;
        m.touchControllerDetected = touchDiagnostics.controllerDetected;
        m.touchCoordinatesReadable = touchDiagnostics.coordinatesReadable;
        m.touchEventsWorking = touchDiagnostics.eventsWorking;
        m.touchUiDispatchWorking = touchDiagnostics.uiDispatchWorking;
        m.showMatterInstructions = showMatterInstructions_;
        m.matterCodeRaw = matterPairingInput_.raw_input;
        m.matterCodeError = matterCodeError_;
        m.matterCodeFormatValid = matterPairingInput_.format_valid;
        m.matterAttestationVerified = matterCommissioningResult_.attestation_verified;
        m.matterAttestationBypassed = matterCommissioningResult_.attestation_bypassed;
        m.matterDevInsecureMode = matterCommissioningResult_.dev_insecure_mode;
        m.matterFailureStage = matterCommissioningResult_.failure_stage;
        m.matterFailureReason = matterCommissioningResult_.failure_reason;
        m.matterSavedState = matterSavedState_;
        m.matterSavedStateReason = matterSavedStateReason_;
        m.matterSavedNode = matterSavedNode_;
        m.matterPost = matterPostResult_;
        m.matterDevicePage = matterDevicePage_;
        m.selectedMatterDevice = selectedMatterDevice_;
        m.weatherCurrent = weather_get_current();
        size_t weatherCount = 0;
        const WeatherForecastDay *weatherDays = weather_get_forecast(&weatherCount);
        if (weatherDays) m.weatherForecast.assign(weatherDays, weatherDays + weatherCount);
        m.weatherRefreshing = weather_refresh_in_progress();
        m.wifiConfigured = !savedSsid_.empty();
        m.matterEarlyInitFailed = matterEarlyInitFailed_;
        m.rooms = rooms_;
        m.selectedRoom = selectedRoom_;
        return m;
    }

    void transition(AppState next) {
        if (state_ != next) ESP_LOGI(TAG, "Transition %s -> %s", appStateName(state_), appStateName(next));
        state_ = next;
        renderUi("screen_enter");
    }

    void transitionState(AppState next) {
        if (state_ != next) ESP_LOGI(TAG, "Transition %s -> %s", appStateName(state_), appStateName(next));
        state_ = next;
    }

    bool renderUi(const char *reason) {
        if (matterCommissioningUiQuiet_.load()) {
            ESP_LOGI("CONTROL_UI", "render suppressed reason=%s state=%s quiet=matter_commissioning",
                     reason ? reason : "unspecified", appStateName(state_));
            return false;
        }
        ESP_LOGI("CONTROL_UI", "render reason=%s state=%s",
                 reason ? reason : "unspecified", appStateName(state_));
        const bool ok = ui_.render(viewModel());
        if (!ok && display_.busy()) {
            pendingRender_ = true;
            pendingRenderState_ = state_;
            pendingRenderReason_ = reason ? reason : "unspecified";
            ESP_LOGI("CONTROL_UI", "render_deferred reason=display_busy state=%s",
                     appStateName(state_));
        } else if (ok) {
            pendingRender_ = false;
        }
        return ok;
    }

    bool renderUiEvenIfMatterQuiet(const char *reason) {
        ESP_LOGI("CONTROL_UI", "render reason=%s state=%s quiet_override=yes",
                 reason ? reason : "unspecified", appStateName(state_));
        const bool ok = ui_.render(viewModel());
        if (!ok && display_.busy()) {
            pendingRender_ = true;
            pendingRenderState_ = state_;
            pendingRenderReason_ = reason ? reason : "unspecified";
            ESP_LOGI("CONTROL_UI", "render_deferred reason=display_busy state=%s",
                     appStateName(state_));
        } else if (ok) {
            pendingRender_ = false;
        }
        return ok;
    }

    void logTaskRuntimeDiagnostics(const char *phase) {
        logTaskRuntimeDiagnosticsForHandle("ui", xTaskGetCurrentTaskHandle(), phase);
        logTaskRuntimeDiagnosticsForHandle("touch", touchTaskHandle_, phase);
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
        logTaskRuntimeDiagnosticsForHandle("matter", matterCommissioningTask_, phase);
#else
        logTaskRuntimeDiagnosticsForHandle("matter", nullptr, phase);
#endif
        logTaskRuntimeDiagnosticsForName("display", "control_display_task", phase);
    }

    void logTaskRuntimeDiagnosticsForName(const char *label, const char *taskName, const char *phase) {
        (void)taskName;
        logTaskRuntimeDiagnosticsForHandle(label, nullptr, phase);
    }

    void logTaskRuntimeDiagnosticsForHandle(const char *label, TaskHandle_t handle, const char *phase) {
        if (!handle) {
            ESP_LOGI("CONTROL_TASKS", "task_diag phase=%s label=%s present=no",
                     phase ? phase : "-", label ? label : "-");
            return;
        }
        ESP_LOGI("CONTROL_TASKS", "task_diag phase=%s label=%s priority=%u stack_high_water=%u",
                 phase ? phase : "-",
                 label ? label : "-",
                 static_cast<unsigned>(uxTaskPriorityGet(handle)),
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(handle)));
    }

    esp_err_t connectSavedWifiHeadless() {
        if (savedSsid_.empty()) {
            ESP_LOGI("CONTROL_WIFI", "headless_connect_skipped reason=no_saved_credentials");
            return ESP_ERR_INVALID_STATE;
        }
        ESP_LOGI("CONTROL_WIFI", "auto_connect start ssid=\"%s\"", savedSsid_.c_str());
        ESP_LOGI("CONTROL_WIFI", "headless_connect_start ssid=\"%s\"", savedSsid_.c_str());
        connectionStartedUs_ = esp_timer_get_time();
        if (!wifi_.connect(savedSsid_, savedPassword_)) {
            ESP_LOGW("CONTROL_WIFI", "headless_connect_start_failed");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "boot_phase=wifi_wait");
        const esp_err_t err = wifi_.waitGotIp(15000);
        if (err == ESP_OK) {
            ESP_LOGI("CONTROL_WIFI", "headless_connect_result=connected");
            wifi_.releaseTransientBuffers();
        } else if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGW("CONTROL_WIFI", "headless_connect_result=timeout");
            wifi_.cancel();
        } else {
            ESP_LOGW("CONTROL_WIFI", "headless_connect_result=failed err=%s", esp_err_to_name(err));
        }
        return err;
    }

    bool runEarlyMatterInit(const char *reason, bool showFailure) {
#if CONFIG_CONTROL_RECOVERY_BUILD
        (void)reason;
        (void)showFailure;
        return false;
#elif CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
        if (matter_controller_is_ready()) {
            ESP_LOGI("CONTROL_MATTER", "early_init_skipped reason=already_ready request=%s",
                     reason ? reason : "-");
            return true;
        }
        if (!wifi_.isConnected()) {
            ESP_LOGW("CONTROL_MATTER", "early_init_skipped reason=wifi_not_connected request=%s",
                     reason ? reason : "-");
            if (showFailure) {
                matterPostResult_ = {};
                matterPostResult_.status = MatterPostCommissioningStatus::CONNECTION_FAILED;
                matterPostResult_.error = ESP_ERR_INVALID_STATE;
                matterPostResult_.errorText = "Wi-Fi non connecté";
                transition(AppState::MATTER_INIT_FAILED);
            }
            return false;
        }
        ESP_LOGI("CONTROL_MATTER", "early_init_request reason=%s", reason ? reason : "-");
        wifi_.releaseTransientBuffers();
        const esp_err_t networkReady = wifi_.waitMatterNetworkReady(MATTER_NETWORK_READY_TIMEOUT_MS);
        if (networkReady != ESP_OK) {
            ESP_LOGW("CONTROL_MATTER", "early_init_skipped reason=network_not_ready err=%s request=%s",
                     esp_err_to_name(networkReady), reason ? reason : "-");
            if (showFailure) {
                matterPostResult_ = {};
                matterPostResult_.status = MatterPostCommissioningStatus::CONNECTION_FAILED;
                matterPostResult_.error = networkReady;
                matterPostResult_.errorText = "Wi-Fi/IPv6 non prêt pour Matter";
                transition(AppState::MATTER_INIT_FAILED);
            }
            return false;
        }
        control_heap_log("before_early_matter_init");
        esp_err_t err = matter_controller_early_init();
        if (err == ESP_OK) {
            control_heap_log("after_early_matter_init");
            return true;
        }
        MatterCommissioningResult result;
        (void)matter_controller_get_last_result(result);
        ESP_LOGW("CONTROL_MATTER", "early_init_failed err=%s request=%s",
                 esp_err_to_name(err), reason ? reason : "-");
        matterPostResult_ = {};
        matterPostResult_.status = MatterPostCommissioningStatus::CONNECTION_FAILED;
        matterPostResult_.error = err;
        matterPostResult_.errorText = err == ESP_ERR_NO_MEM
            ? "Mémoire interne insuffisante"
            : (result.errorText.empty() ? "Initialisation Matter impossible" : result.errorText);
        if (showFailure) {
            transition(AppState::MATTER_INIT_FAILED);
        }
        return false;
#else
        (void)reason;
        (void)showFailure;
        return false;
#endif
    }

    void startScan() {
        autoConnectAtUs_ = 0;
        networkPage_ = 0;
        transition(AppState::WIFI_SCANNING); // Visible busy screen before scan work starts.
        if (!wifi_.startScan()) {
            failureReason_ = "Impossible de démarrer le scan Wi-Fi";
            transition(AppState::WIFI_CONNECTION_FAILED);
        }
    }

    void startConnection() {
        autoConnectAtUs_ = 0;
        transition(AppState::WIFI_CONNECTING);
        connectionStartedUs_ = esp_timer_get_time();
        ESP_LOGI("CONTROL_WIFI", "connect_requested ssid=\"%s\" password_len=%u",
                 selectedSsid_.c_str(), static_cast<unsigned>(password_.size()));
        if (!wifi_.connect(selectedSsid_, password_)) {
            failureReason_ = "Impossible de démarrer la connexion";
            transition(AppState::WIFI_CONNECTION_FAILED);
        }
    }

    void processWifi() {
        const WifiStatusEvent event = wifi_.poll();
        if (event.type == WifiEventType::SCAN_COMPLETE && state_ == AppState::WIFI_SCANNING) {
            ESP_LOGI(TAG, "wifi event SCAN_DONE consumed");
            transition(AppState::WIFI_NETWORK_SELECTION);
        } else if (event.type == WifiEventType::CONNECTED &&
                   (state_ == AppState::WIFI_CONNECTING || state_ == AppState::WEATHER_HOME)) {
            ESP_LOGI("CONTROL_WIFI", "got_ip save_credentials ssid=\"%s\" password_len=%u",
                     selectedSsid_.c_str(), static_cast<unsigned>(password_.size()));
            storage_.saveWifi(selectedSsid_, password_);
            savedSsid_ = selectedSsid_;
            savedPassword_ = password_;
            ESP_LOGI(TAG, "Connexion réussie à '%s'", selectedSsid_.c_str());
            wifi_.logNetworkDiagnostics();
#if CONFIG_CONTROL_RECOVERY_BUILD
            (void)weather_init();
            (void)weather_load_cache();
            weather_ensure_placeholder();
#else
            (void)runEarlyMatterInit("wifi_configured", false);
#endif
            (void)weather_refresh_async();
            if (state_ == AppState::WIFI_CONNECTING) transition(AppState::WIFI_CONNECTED);
        } else if (event.type == WifiEventType::FAILED &&
                   (state_ == AppState::WIFI_CONNECTING || state_ == AppState::WEATHER_HOME)) {
            failureReason_ = event.reason;
            if (state_ == AppState::WIFI_CONNECTING) transition(AppState::WIFI_CONNECTION_FAILED);
        }
    }

    void processIkea() {
        const IkeaDiscoveryEvent event = ikea_.poll();
        if (event.type != IkeaDiscoveryEventType::COMPLETE || state_ != AppState::IKEA_DISCOVERY) return;
        lastIkeaResult_ = event.result;
        ESP_LOGI(TAG, "ikea event consumed result=%s", ikeaClassificationName(event.result.classification));
        switch (event.result.classification) {
            case IkeaDiscoveryClassification::IKEA_FOUND_CONFIDENT:
            case IkeaDiscoveryClassification::KNOWN_IP_REACHABLE_UNCONFIRMED:
            case IkeaDiscoveryClassification::MATTER_DEVICE_FOUND_UNCONFIRMED:
            case IkeaDiscoveryClassification::LOCAL_MDNS_FOUND_UNCONFIRMED:
                transition(AppState::IKEA_FOUND);
                break;
            case IkeaDiscoveryClassification::NOT_FOUND:
                transition(AppState::IKEA_NOT_FOUND);
                break;
            case IkeaDiscoveryClassification::DISCOVERY_ERROR:
            default:
                transition(AppState::IKEA_DISCOVERY_FAILED);
                break;
        }
    }

    void processMatter() {
#if CONFIG_CONTROL_RECOVERY_BUILD
        return;
#else
        MatterPostCommissioningResult post;
        (void)matter_controller_get_post_commissioning_result(post);
        if (post.status != lastMatterPostStatus_) {
            ESP_LOGI("CONTROL_MATTER", "post_commissioning_status old=%s new=%s",
                     matter_post_commissioning_status_name(lastMatterPostStatus_),
                     matter_post_commissioning_status_name(post.status));
            lastMatterPostStatus_ = post.status;
            matterPostResult_ = post;
            matterSavedNode_ = post.savedNode;
            switch (post.status) {
                case MatterPostCommissioningStatus::DEVICE_DISCOVERY:
                    if (state_ != AppState::MATTER_DEVICE_DISCOVERY) transition(AppState::MATTER_DEVICE_DISCOVERY);
                    break;
                case MatterPostCommissioningStatus::DEVICE_LIST:
                    normalizeMatterSelection();
                    transition(AppState::MATTER_DEVICE_LIST);
                    break;
                case MatterPostCommissioningStatus::COMMAND_IN_PROGRESS:
                    if (batchCommandActive_) {
                        ESP_LOGI("CONTROL_UI", "refresh_policy suppressed reason=matter_command_batch");
                    } else {
                        transition(AppState::MATTER_COMMAND_IN_PROGRESS);
                    }
                    break;
                case MatterPostCommissioningStatus::COMMAND_SUCCESS: {
                    normalizeMatterSelection();
                    if (batchCommandActive_ && startNextRoomCommand()) break;
                    const bool finishedBatch = batchCommandActive_;
                    batchCommandActive_ = false;
                    transition(finishedBatch ? AppState::HOME_CONTROL : AppState::MATTER_COMMAND_SUCCESS);
                    break;
                }
                case MatterPostCommissioningStatus::COMMAND_FAILED:
                    batchCommandActive_ = false;
                    transition(AppState::MATTER_COMMAND_FAILED);
                    break;
                case MatterPostCommissioningStatus::CONNECTION_FAILED:
                    transition(AppState::MATTER_CONNECTION_FAILED);
                    break;
                case MatterPostCommissioningStatus::DISCOVERY_FAILED:
                    transition(AppState::MATTER_DISCOVERY_FAILED);
                    break;
                case MatterPostCommissioningStatus::SAVED_NODE_INVALID:
                    transition(AppState::MATTER_SAVED_INVALID);
                    break;
                case MatterPostCommissioningStatus::IDLE:
                    break;
            }
        } else if (post.status == MatterPostCommissioningStatus::DEVICE_LIST ||
                   post.status == MatterPostCommissioningStatus::COMMAND_SUCCESS ||
                   post.status == MatterPostCommissioningStatus::COMMAND_FAILED) {
            matterPostResult_ = post;
            matterSavedNode_ = post.savedNode;
        }
        if (state_ != AppState::MATTER_COMMISSIONING) return;
        MatterCommissioningResult result;
        (void)matter_controller_get_last_result(result);
        if (result.status == MatterCommissioningStatus::COMMISSIONING ||
            result.status == MatterCommissioningStatus::INITIALIZING ||
            result.status == MatterCommissioningStatus::READY ||
            result.status == MatterCommissioningStatus::IDLE) {
            return;
        }
        matterCommissioningResult_ = result;
        if (result.status == MatterCommissioningStatus::COMMISSIONED) {
            matterCommissioningUiQuiet_.store(false);
            logTaskRuntimeDiagnostics("after_commissioning_success");
            char nodeBuf[24];
            formatMatterNodeId(result.nodeId, nodeBuf, sizeof(nodeBuf));
            ESP_LOGI("CONTROL_MATTER", "commissioning_success node_id=%s", nodeBuf);
            resetMatterCode();
            loadSavedMatterNode();
            transition(AppState::MATTER_COMMISSIONING_SUCCESS);
        } else {
            matterCommissioningUiQuiet_.store(false);
            logTaskRuntimeDiagnostics("after_commissioning_failure");
            matterCodeError_ = result.errorText.empty()
                ? std::string("Commissioning Matter indisponible ou échoué.")
                : result.errorText;
            ESP_LOGW("CONTROL_MATTER", "commissioning_failed err=%s text=%s",
                     esp_err_to_name(result.error), matterCodeError_.c_str());
            transition(AppState::MATTER_COMMISSIONING_FAILED);
        }
#endif
    }

    void processTimeouts() {
        if (autoConnectAtUs_ != 0 &&
            (state_ == AppState::HARDWARE_TEST || state_ == AppState::WEATHER_HOME) &&
            esp_timer_get_time() >= autoConnectAtUs_) {
            autoConnectAtUs_ = 0;
            if (state_ == AppState::WEATHER_HOME) {
                connectionStartedUs_ = esp_timer_get_time();
                ESP_LOGI("CONTROL_WIFI", "auto_connect_background ssid=\"%s\"", savedSsid_.c_str());
                (void)wifi_.connect(savedSsid_, savedPassword_);
            } else {
                startConnection();
            }
        }
        if (state_ == AppState::WIFI_CONNECTING &&
            esp_timer_get_time() - connectionStartedUs_ > CONNECTION_TIMEOUT_US) {
            wifi_.cancel();
            failureReason_ = "Délai de connexion dépassé";
            transition(AppState::WIFI_CONNECTION_FAILED);
        }
    }

    void processWeather() {
        if (!weather_poll_refresh_complete()) return;
        ESP_LOGI("CONTROL_UI", "refresh_policy full reason=weather_refresh_complete");
        if (state_ == AppState::WEATHER_HOME) renderUi("weather_refresh_complete");
    }

    void processPendingRender() {
        if (!pendingRender_ || display_.busy()) return;
        ESP_LOGI("CONTROL_UI", "render_pending_start state=%s reason=%s",
                 appStateName(pendingRenderState_),
                 pendingRenderReason_ ? pendingRenderReason_ : "unspecified");
        pendingRender_ = false;
        const bool ok = ui_.render(viewModel());
        if (!ok && display_.busy()) {
            pendingRender_ = true;
            pendingRenderState_ = state_;
            ESP_LOGI("CONTROL_UI", "render_deferred reason=display_busy state=%s",
                     appStateName(state_));
        }
    }

    static bool containsInsensitive(const std::string &value, const char *needle) {
        std::string lower = value;
        for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string key = needle ? needle : "";
        for (char &c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return lower.find(key) != std::string::npos;
    }

    void buildRoomGroups() {
        rooms_.clear();
        for (const auto &device : matterPostResult_.devices) {
            if (!device.has_onoff && !device.has_switch) continue;
            std::string room;
            const char *source = "fallback";
            if (containsInsensitive(device.name, "salon")) room = "Salon";
            else if (containsInsensitive(device.name, "cuisine")) room = "Cuisine";
            else if (containsInsensitive(device.name, "chambre")) room = "Chambre";
            else if (containsInsensitive(device.name, "bureau")) room = "Bureau";
            else if (containsInsensitive(device.name, "entrée") || containsInsensitive(device.name, "entree")) room = "Entrée";
            else if (device.has_switch && !device.has_onoff) room = "Interrupteurs";
            else if (device.has_onoff) room = "Autres lumières";
            else room = "Autres";
            if (source[0] == 'f' && device.name.find("Salon") != std::string::npos) source = "name_keyword";
            if (room != "Interrupteurs" && room != "Autres lumières" && room != "Autres") source = "name_keyword";
            size_t index = 0;
            for (; index < rooms_.size(); ++index) if (room == rooms_[index].name) break;
            if (index == rooms_.size()) {
                RoomGroup group;
                std::strncpy(group.name, room.c_str(), sizeof(group.name) - 1);
                rooms_.push_back(group);
            }
            RoomGroup &group = rooms_[index];
            if (group.endpoint_count < 16) group.endpoint_ids[group.endpoint_count++] = device.endpoint_id;
            if (device.has_onoff) {
                ++group.controllable_count;
                if (device.onoff_state_known && device.onoff_state) ++group.lights_on_count;
            }
            ESP_LOGI("CONTROL_HOME", "room_infer endpoint=%u name=\"%s\" room=\"%s\" source=%s",
                     device.endpoint_id, device.name.c_str(), group.name, source);
        }
        ESP_LOGI("CONTROL_HOME", "room_group_build count=%u", static_cast<unsigned>(rooms_.size()));
    }

    void processTouchEvents() {
        if (touchQueue_ == nullptr) return;
        TouchEvent event;
        if (xQueueReceive(touchQueue_, &event, 0) != pdTRUE) return;

        ESP_LOGI("CONTROL_TOUCH", "queued event=%s at x=%d y=%d",
                 touchEventName(event.type), event.x, event.y);
        if (matterCommissioningUiQuiet_.load()) {
            ESP_LOGI("CONTROL_TOUCH", "event ignored quiet=matter_commissioning");
            return;
        }
#if CONFIG_CONTROL_TOUCH_UI_DISPATCH
        if (event.type != TouchEventType::PRESS) return;
        const UiAction action = ui_.hitTest(viewModel(), event.x, event.y);
        if (action.type != UiActionType::NONE) {
            handleAction(action);
        }
#endif
    }

    void handleAction(const UiAction &a) {
        switch (a.type) {
            case UiActionType::OPEN_HOME:
                if (state_ == AppState::WEATHER_HOME) {
                    if (matterEarlyInitFailed_) {
                        transition(AppState::MATTER_INIT_FAILED);
                        break;
                    }
                    loadSavedMatterNode();
                    if (matterSavedState_ == MatterSavedState::Valid) {
                        MatterPostCommissioningResult cached;
                        (void)matter_controller_load_device_cache(cached);
                        matterPostResult_ = cached;
                        buildRoomGroups();
                        transition(AppState::HOME_CONTROL);
                    } else {
                        transition(AppState::MATTER_INTRO);
                    }
                } else if (state_ == AppState::ROOM_DETAIL || state_ == AppState::SETTINGS_DIAGNOSTIC) {
                    transition(AppState::HOME_CONTROL);
                }
                break;
            case UiActionType::OPEN_ROOM:
                selectedRoom_ = a.index;
                transition(AppState::ROOM_DETAIL);
                break;
            case UiActionType::OPEN_WEATHER:
                transition(AppState::WEATHER_HOME);
                break;
            case UiActionType::WEATHER_REFRESH:
                if (wifi_.isConnected() && !weather_refresh_in_progress()) (void)weather_refresh_async();
                if (state_ != AppState::WEATHER_HOME) transition(AppState::WEATHER_HOME);
                else renderUi("weather_refresh_requested");
                break;
            case UiActionType::DIAGNOSTIC:
                transition(AppState::SETTINGS_DIAGNOSTIC);
                break;
            case UiActionType::DISCOVER_DEVICES:
                if (state_ == AppState::SETTINGS_DIAGNOSTIC) startMatterDiscovery();
                else if (matterSavedState_ == MatterSavedState::Valid) startMatterDiscovery();
                break;
            case UiActionType::PAIR_MATTER:
                resetMatterCode();
                transition(AppState::MATTER_CODE_ENTRY);
                break;
            case UiActionType::RESET_MATTER:
                forgetMatter();
                break;
            case UiActionType::ROOM_DETAIL:
                selectedRoom_ = a.index;
                transition(AppState::ROOM_DETAIL);
                break;
            case UiActionType::ROOM_ON:
                startRoomBatch(a.index, true);
                break;
            case UiActionType::ROOM_OFF:
                startRoomBatch(a.index, false);
                break;
            case UiActionType::CONFIGURE_WIFI: startScan(); break;
            case UiActionType::BACK:
                if (state_ == AppState::MATTER_NOT_IMPLEMENTED ||
                    state_ == AppState::MATTER_NO_SAVED_NODE ||
                    state_ == AppState::MATTER_CONNECTION_FAILED ||
                    state_ == AppState::MATTER_DISCOVERY_FAILED ||
                    state_ == AppState::MATTER_INIT_FAILED ||
                    state_ == AppState::MATTER_SAVED_INVALID ||
                    state_ == AppState::MATTER_FORGET_CONFIRM ||
                    state_ == AppState::MATTER_FORGOT) {
                    openMatterHome();
                } else if (state_ == AppState::MATTER_DEVICE_LIST) {
                    openMatterHome();
                } else if (state_ == AppState::MATTER_COMMAND_SUCCESS ||
                           state_ == AppState::MATTER_COMMAND_FAILED) {
                    transition(AppState::MATTER_DEVICE_LIST);
                }
                else { wifi_.cancel(); transition(AppState::HARDWARE_TEST); }
                break;
            case UiActionType::REFRESH:
                if (state_ == AppState::MATTER_DEVICE_LIST || state_ == AppState::MATTER_INTRO) startMatterDiscovery();
                else startScan();
                break;
            case UiActionType::PAGE_PREVIOUS:
                if (state_ == AppState::MATTER_DEVICE_LIST) {
                    if (matterDevicePage_ > 0) --matterDevicePage_;
                    normalizeMatterSelection();
                } else if (networkPage_ > 0) --networkPage_;
                renderUi("screen_enter");
                break;
            case UiActionType::PAGE_NEXT: {
                if (state_ == AppState::MATTER_DEVICE_LIST) {
                    const size_t pages = std::max<size_t>(
                        1, (matterPostResult_.devices.size() + MATTER_DEVICES_PER_PAGE - 1) / MATTER_DEVICES_PER_PAGE);
                    if (matterDevicePage_ + 1 < pages) ++matterDevicePage_;
                    normalizeMatterSelection();
                } else {
                    const size_t pages = std::max<size_t>(1, (wifi_.networks().size() + 4) / 5);
                    if (networkPage_ + 1 < pages) ++networkPage_;
                }
                renderUi("screen_enter"); break;
            }
            case UiActionType::SELECT_NETWORK:
                if (a.index >= 0 && a.index < static_cast<int>(wifi_.networks().size())) {
                    const auto &network = wifi_.networks()[a.index];
                    selectedSsid_ = network.ssid;
                    password_.clear();
                    passwordVisible_ = false;
                    passwordKeyPressesSinceRefresh_ = 0;
                    passwordPartialsSinceFullRefresh_ = 0;
                    passwordFullRefreshPending_ = false;
                    keyboardMode_ = 0;
                    openNetworkConfirmation_ = network.auth == WIFI_AUTH_OPEN;
                    ESP_LOGI("CONTROL_WIFI", "selected ssid=\"%s\" auth=%s rssi=%d",
                             network.ssid.c_str(),
                             wifiAuthName(network.auth),
                             network.rssi);
                    transition(AppState::WIFI_PASSWORD_ENTRY);
                }
                break;
            case UiActionType::KEY_CHARACTER:
                if (state_ == AppState::MATTER_CODE_ENTRY) {
                    appendMatterCodeChar(a.character);
                } else {
                    if (password_.size() < 63) password_ += a.character;
                    ESP_LOGI("CONTROL_UI", "wifi_key len=%u",
                             static_cast<unsigned>(password_.size()));
                }
                break;
            case UiActionType::KEY_BACKSPACE:
                if (state_ == AppState::MATTER_CODE_ENTRY) {
                    backspaceMatterCode();
                } else {
                    if (!password_.empty()) password_.pop_back();
                    ESP_LOGI("CONTROL_UI", "wifi_backspace len=%u",
                             static_cast<unsigned>(password_.size()));
                }
                break;
            case UiActionType::KEY_CLEAR:
                if (state_ == AppState::MATTER_CODE_ENTRY) {
                    clearMatterCode();
                } else {
                    password_.clear();
                    ESP_LOGI("CONTROL_UI", "wifi_clear len=0");
                }
                break;
            case UiActionType::KEY_SHIFT:
                updateKeyboardMode(keyboardMode_ == 1 ? 0 : 1);
                break;
            case UiActionType::KEY_SYMBOLS:
                updateKeyboardMode(keyboardMode_ == 2 ? 0 : 2);
                break;
            case UiActionType::TOGGLE_PASSWORD:
                passwordVisible_ = !passwordVisible_;
                renderUi("explicit_refresh");
                break;
            case UiActionType::CANCEL:
                wifi_.cancel(); transition(AppState::WIFI_NETWORK_SELECTION); break;
            case UiActionType::CONNECT: startConnection(); break;
            case UiActionType::RETRY:
                if (state_ == AppState::MATTER_CONNECTION_FAILED ||
                    state_ == AppState::MATTER_DISCOVERY_FAILED) {
                    startMatterDiscovery();
                } else if (state_ == AppState::MATTER_INIT_FAILED) {
                    MatterCommissioningResult result;
                    (void)matter_controller_get_last_result(result);
                    matterPostResult_.status = MatterPostCommissioningStatus::CONNECTION_FAILED;
                    matterPostResult_.error = result.error == ESP_OK ? ESP_FAIL : result.error;
                    matterPostResult_.errorText = "Redémarrer ou réinitialiser Matter avant de réessayer";
                    ESP_LOGW("CONTROL_MATTER", "retry_rejected reason=previous_init_failure_requires_reboot_or_cleanup");
                    renderUi("matter_retry_rejected");
                } else if (state_ == AppState::MATTER_COMMAND_FAILED) {
                    retryMatterCommand();
                } else {
                    startConnection();
                }
                break;
            case UiActionType::ERASE_CREDENTIALS:
                storage_.eraseWifi(); savedSsid_.clear(); savedPassword_.clear(); password_.clear(); startScan(); break;
            case UiActionType::CONTINUE_MATTER:
                if (state_ == AppState::WIFI_CONNECTED) {
                    openMatterHome();
                } else if (state_ == AppState::IKEA_FOUND || state_ == AppState::MATTER_NOT_IMPLEMENTED) {
                    resetMatterCode();
                    transition(AppState::MATTER_CODE_ENTRY);
                } else if (state_ == AppState::MATTER_CODE_CONFIRM) {
                    transition(AppState::MATTER_PAIRING_MODE);
                } else if (state_ == AppState::MATTER_MDNS_PRESCAN_EMPTY) {
                    continueAfterMatterCodeConfirm();
                } else if (state_ == AppState::MATTER_COMMISSIONING_FAILED) {
                    continueAfterMatterCodeConfirm();
                } else {
                    storage_.setInitialSetupComplete(true); showMatterInstructions_ = false; transition(AppState::MATTER_NOT_IMPLEMENTED);
                }
                break;
            case UiActionType::SEARCH_MATTER:
                matter_.requestEnvironmentSearch(); transition(AppState::MATTER_NOT_IMPLEMENTED); break;
            case UiActionType::SHOW_INSTRUCTIONS:
                matter_.requestInstructions(); showMatterInstructions_ = true; renderUi("screen_enter"); break;
            case UiActionType::BACK_TO_WIFI:
                ikea_.cancel(); transition(AppState::WIFI_CONNECTED); break;
            case UiActionType::SEARCH_IKEA:
            case UiActionType::RETRY_IKEA:
                transition(AppState::IKEA_DISCOVERY);
                lastIkeaResult_ = {};
                wifi_.logNetworkDiagnostics();
                if (!ikea_.start(wifi_.connectionInfo())) transition(AppState::IKEA_DISCOVERY_FAILED);
                break;
            case UiActionType::MATTER_CODE_VALIDATE:
                validateMatterCode();
                break;
            case UiActionType::MATTER_CODE_EDIT:
                transition(AppState::MATTER_CODE_ENTRY);
                break;
            case UiActionType::MATTER_PAIR_INITIAL:
                matterMultiAdminMode_ = false;
                matter_controller_set_pending_commissioning_mode(false);
                ESP_LOGI("CONTROL_MATTER", "commissioning_mode=initial");
                continueAfterMatterCodeConfirm();
                break;
            case UiActionType::MATTER_PAIR_MULTI_ADMIN:
                matterMultiAdminMode_ = true;
                matter_controller_set_pending_commissioning_mode(true);
                ESP_LOGI("CONTROL_MATTER", "commissioning_mode=multi_admin");
                continueAfterMatterCodeConfirm();
                break;
            case UiActionType::MATTER_CONTINUE_ANYWAY:
                ESP_LOGI("CONTROL_MATTER", "chip_discovery=pairing_code continuation=explicit");
                continueAfterMatterCodeConfirm();
                break;
            case UiActionType::MATTER_USE_SAVED:
                if (matter_controller_has_saved_node()) {
                    startMatterDiscovery();
                } else {
                    loadSavedMatterNode();
                    transition(AppState::MATTER_NO_SAVED_NODE);
                }
                break;
            case UiActionType::MATTER_PAIR_CODE:
                if (state_ == AppState::MATTER_SAVED_INVALID ||
                    state_ == AppState::MATTER_CONNECTION_FAILED ||
                    state_ == AppState::MATTER_COMMISSIONING_FAILED) {
                    const esp_err_t resetErr = matter_controller_reset_local_storage_preserve_wifi();
                    ESP_LOGI("CONTROL_MATTER", "repair_reset_before_pair result=%s", esp_err_to_name(resetErr));
                    loadSavedMatterNode();
                    matterPostResult_ = {};
                }
                resetMatterCode();
                transition(AppState::MATTER_CODE_ENTRY);
                break;
            case UiActionType::MATTER_NETWORK_DIAGNOSTIC:
                transition(AppState::IKEA_DISCOVERY);
                lastIkeaResult_ = {};
                wifi_.logNetworkDiagnostics();
                if (!ikea_.start(wifi_.connectionInfo())) transition(AppState::IKEA_DISCOVERY_FAILED);
                break;
            case UiActionType::MATTER_DEVICE_SELECT:
                if (a.index >= 0 && a.index < static_cast<int>(matterPostResult_.devices.size())) {
                    selectedMatterDevice_ = a.index;
                    renderUi("screen_enter");
                }
                break;
            case UiActionType::MATTER_ON:
                startMatterCommand(true);
                break;
            case UiActionType::MATTER_OFF:
                startMatterCommand(false);
                break;
            case UiActionType::MATTER_FORGET:
                transition(AppState::MATTER_FORGET_CONFIRM);
                break;
            case UiActionType::MATTER_FORGET_CONFIRM:
                forgetMatter();
                break;
            case UiActionType::MATTER_RESTART:
                ESP_LOGW("CONTROL_MATTER", "restart_requested_after_matter_forget");
                esp_restart();
                break;
            case UiActionType::NONE: break;
        }
    }

    void loadSavedMatterNode() {
#if CONFIG_CONTROL_RECOVERY_BUILD
        matterSavedState_ = MatterSavedState::None;
        matterSavedStateReason_.clear();
        matterSavedNode_ = {};
        return;
#else
        MatterSavedNode node;
        std::string reason;
        matterSavedState_ = matter_saved_state_load_and_validate(node, reason);
        matterSavedStateReason_ = reason;
        if (matterSavedState_ == MatterSavedState::Valid || matterSavedState_ == MatterSavedState::Invalid) {
            matterSavedNode_ = node;
        } else {
            matterSavedNode_ = {};
        }
#endif
    }

    void openMatterHome() {
        loadSavedMatterNode();
        MatterPostCommissioningResult post;
        (void)matter_controller_get_post_commissioning_result(post);
        matterPostResult_ = post;
        if (matterSavedState_ == MatterSavedState::Invalid) {
            matterPostResult_.status = MatterPostCommissioningStatus::SAVED_NODE_INVALID;
            matterPostResult_.savedNode = matterSavedNode_;
            matterPostResult_.error = ESP_ERR_INVALID_STATE;
            matterPostResult_.errorText = matterSavedStateReason_ == "remote_node_id_zero"
                ? "Node ID distant manquant"
                : "Réinitialisation nécessaire";
            transition(AppState::MATTER_SAVED_INVALID);
        } else {
            if (matterSavedState_ == MatterSavedState::Valid) {
                MatterPostCommissioningResult cached;
                if (matter_controller_load_device_cache(cached) == ESP_OK) {
                    matterPostResult_ = cached;
                }
            }
            transition(AppState::MATTER_INTRO);
        }
    }

    void startMatterDiscovery() {
        ESP_LOGI("CONTROL_MATTER", "use_saved_node requested");
        loadSavedMatterNode();
        if (matterSavedState_ != MatterSavedState::Valid) {
            matterPostResult_ = {};
            matterPostResult_.savedNode = matterSavedNode_;
            matterPostResult_.status = matterSavedState_ == MatterSavedState::Invalid
                ? MatterPostCommissioningStatus::SAVED_NODE_INVALID
                : MatterPostCommissioningStatus::DISCOVERY_FAILED;
            matterPostResult_.error = matterSavedState_ == MatterSavedState::Invalid ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
            matterPostResult_.errorText = matterSavedState_ == MatterSavedState::Invalid
                ? (matterSavedStateReason_ == "remote_node_id_zero" ? "Node ID distant manquant" : "Réinitialisation nécessaire")
                : "Aucun appairage Matter valide";
            transition(matterSavedState_ == MatterSavedState::Invalid ? AppState::MATTER_SAVED_INVALID : AppState::MATTER_NO_SAVED_NODE);
            return;
        }
        ESP_LOGI("CONTROL_MATTER", "controller_ready=%s", matter_controller_is_ready() ? "yes" : "no");
        if (!matter_controller_is_ready() && !runEarlyMatterInit("use_saved_node", true)) {
            return;
        }
        wifi_.releaseTransientBuffers();
        control_heap_log("before_matter_use");
        control_heap_log("before_matter_use_saved");
        if (wifi_.isConnected()) {
            (void)control_time_start_sntp_once();
        }
        matterDevicePage_ = 0;
        selectedMatterDevice_ = -1;
        matterPostResult_ = {};
        matterPostResult_.savedNode = matterSavedNode_;
        lastMatterPostStatus_ = MatterPostCommissioningStatus::IDLE;
        transition(AppState::MATTER_DEVICE_DISCOVERY);
        ESP_LOGI("CONTROL_MATTER", "discovery_start");
        const esp_err_t err = matter_controller_start_device_discovery();
        if (err == ESP_ERR_INVALID_STATE) {
            (void)matter_controller_get_post_commissioning_result(matterPostResult_);
            matterPostResult_.status = MatterPostCommissioningStatus::SAVED_NODE_INVALID;
            matterPostResult_.error = err;
            matterPostResult_.errorText = "Matter fabric corrompue";
            transition(AppState::MATTER_SAVED_INVALID);
        } else if (err != ESP_OK) {
            matterPostResult_.status = MatterPostCommissioningStatus::DISCOVERY_FAILED;
            matterPostResult_.error = err;
            matterPostResult_.errorText = err == ESP_ERR_NOT_FOUND
                ? "Aucun appareil Matter enregistré"
                : "Lecture appareils échouée";
            transition(AppState::MATTER_DISCOVERY_FAILED);
        }
    }

    void forgetMatter() {
        const esp_err_t err = matter_controller_reset_local_storage_complete_preserve_wifi();
        ESP_LOGI("CONTROL_MATTER", "reset_matter_local_complete_ui result=%s", esp_err_to_name(err));
        loadSavedMatterNode();
        matterPostResult_ = {};
        matterPostResult_.savedNode = matterSavedNode_;
        matterDevicePage_ = 0;
        selectedMatterDevice_ = -1;
        lastMatterPostStatus_ = MatterPostCommissioningStatus::IDLE;
        resetMatterCode();
        transition(AppState::MATTER_FORGOT);
    }

    void normalizeMatterSelection() {
        const size_t count = matterPostResult_.devices.size();
        const size_t pages = std::max<size_t>(1, (count + MATTER_DEVICES_PER_PAGE - 1) / MATTER_DEVICES_PER_PAGE);
        if (matterDevicePage_ >= pages) matterDevicePage_ = pages - 1;
        if (count == 0) {
            selectedMatterDevice_ = -1;
            return;
        }
        if (selectedMatterDevice_ < 0 || selectedMatterDevice_ >= static_cast<int>(count)) {
            selectedMatterDevice_ = static_cast<int>(matterDevicePage_ * MATTER_DEVICES_PER_PAGE);
            if (selectedMatterDevice_ >= static_cast<int>(count)) selectedMatterDevice_ = static_cast<int>(count - 1);
        }
    }

    void startMatterCommand(bool on) {
        if (matterPostResult_.devices.empty()) {
            ESP_LOGW("CONTROL_MATTER", "command_rejected reason=no_devices");
            return;
        }
        normalizeMatterSelection();
        if (selectedMatterDevice_ < 0 ||
            selectedMatterDevice_ >= static_cast<int>(matterPostResult_.devices.size())) {
            ESP_LOGW("CONTROL_MATTER", "command_rejected reason=no_selection");
            return;
        }
        const MatterDeviceItem &device = matterPostResult_.devices[selectedMatterDevice_];
        if (!device.has_onoff) {
            matterPostResult_.status = MatterPostCommissioningStatus::COMMAND_FAILED;
            matterPostResult_.activeEndpoint = device.endpoint_id;
            matterPostResult_.error = ESP_ERR_NOT_SUPPORTED;
            matterPostResult_.errorText = "Endpoint non contrôlable On/Off";
            ESP_LOGW("CONTROL_MATTER", "command_rejected endpoint=%u reason=no_onoff_cluster type=%s",
                     device.endpoint_id, device.type_label.c_str());
            transition(AppState::MATTER_COMMAND_FAILED);
            return;
        }
        lastMatterCommandOn_ = on;
        lastMatterCommandEndpoint_ = device.endpoint_id;
        lastMatterPostStatus_ = MatterPostCommissioningStatus::IDLE;
        matterPostResult_.status = MatterPostCommissioningStatus::COMMAND_IN_PROGRESS;
        matterPostResult_.activeEndpoint = device.endpoint_id;
        transition(AppState::MATTER_COMMAND_IN_PROGRESS);
        const esp_err_t err = on
            ? matter_controller_send_on(matterSavedNode_.node_id, device.endpoint_id)
            : matter_controller_send_off(matterSavedNode_.node_id, device.endpoint_id);
        if (err != ESP_OK) {
            matterPostResult_.status = MatterPostCommissioningStatus::COMMAND_FAILED;
            matterPostResult_.error = err;
            matterPostResult_.errorText = "Commande échouée";
            ESP_LOGW("CONTROL_MATTER", "command_start_failed endpoint=%u err=%s",
                     device.endpoint_id, esp_err_to_name(err));
            transition(AppState::MATTER_COMMAND_FAILED);
        }
    }

    bool startNextRoomCommand() {
        if (!batchCommandActive_ || batchCommandPosition_ >= batchCommandEndpoints_.size()) return false;
        const uint16_t endpoint = batchCommandEndpoints_[batchCommandPosition_++];
        lastMatterCommandEndpoint_ = endpoint;
        lastMatterCommandOn_ = batchCommandOn_;
        ESP_LOGI("CONTROL_HOME", "room_batch endpoint=%u action=%s", endpoint, batchCommandOn_ ? "on" : "off");
        const esp_err_t err = batchCommandOn_
            ? matter_controller_send_on(matterSavedNode_.node_id, endpoint)
            : matter_controller_send_off(matterSavedNode_.node_id, endpoint);
        if (err != ESP_OK) {
            batchCommandActive_ = false;
            matterPostResult_.error = err;
            matterPostResult_.errorText = "Commande de pièce échouée";
            transition(AppState::MATTER_COMMAND_FAILED);
            return false;
        }
        return true;
    }

    void startRoomBatch(int roomIndex, bool on) {
        if (roomIndex < 0 || roomIndex >= static_cast<int>(rooms_.size())) return;
        const RoomGroup &room = rooms_[roomIndex];
        batchCommandEndpoints_.clear();
        for (uint8_t i = 0; i < room.endpoint_count; ++i) {
            for (const auto &device : matterPostResult_.devices) {
                if (device.endpoint_id != room.endpoint_ids[i]) continue;
                if (!device.has_onoff) {
                    ESP_LOGI("CONTROL_HOME", "endpoint_skip endpoint=%u reason=no_onoff_cluster", device.endpoint_id);
                } else {
                    batchCommandEndpoints_.push_back(device.endpoint_id);
                }
                break;
            }
        }
        ESP_LOGI("CONTROL_HOME", "room_action room=\"%s\" action=%s endpoints=%u",
                 room.name, on ? "on" : "off", static_cast<unsigned>(batchCommandEndpoints_.size()));
        if (batchCommandEndpoints_.empty()) {
            ESP_LOGI("CONTROL_UI", "batch_render_pending=yes");
            renderUi("batch_empty");
            return;
        }
        batchCommandOn_ = on;
        batchCommandPosition_ = 0;
        batchCommandActive_ = true;
        lastMatterPostStatus_ = MatterPostCommissioningStatus::IDLE;
        matterPostResult_.status = MatterPostCommissioningStatus::COMMAND_IN_PROGRESS;
        ESP_LOGI("CONTROL_UI", "batch_render_pending=yes");
        (void)startNextRoomCommand();
    }

    void retryMatterCommand() {
        if (lastMatterCommandEndpoint_ == 0) {
            transition(AppState::MATTER_DEVICE_LIST);
            return;
        }
        for (size_t i = 0; i < matterPostResult_.devices.size(); ++i) {
            if (matterPostResult_.devices[i].endpoint_id == lastMatterCommandEndpoint_) {
                selectedMatterDevice_ = static_cast<int>(i);
                startMatterCommand(lastMatterCommandOn_);
                return;
            }
        }
        transition(AppState::MATTER_DEVICE_LIST);
    }

    void resetMatterCode() {
        matterPairingInput_ = {};
        matterCodeError_.clear();
    }

    void appendMatterCodeChar(char c) {
        if (matterPairingInput_.raw_input.size() >= 19) return;
        if ((c >= '0' && c <= '9') || c == '-' || c == ' ') {
            matterPairingInput_.raw_input.push_back(c);
            matterPairingInput_.format_valid = false;
            matterPairingInput_.normalized_digits.clear();
            matterCodeError_.clear();
            if (c >= '0' && c <= '9') {
                ESP_LOGI("CONTROL_UI", "matter_key digit=%c len=%u", c,
                         static_cast<unsigned>(matterPairingInput_.raw_input.size()));
            } else {
                ESP_LOGI("CONTROL_UI", "matter_key separator len=%u",
                         static_cast<unsigned>(matterPairingInput_.raw_input.size()));
            }
        }
    }

    void backspaceMatterCode() {
        if (!matterPairingInput_.raw_input.empty()) matterPairingInput_.raw_input.pop_back();
        matterPairingInput_.format_valid = false;
        matterPairingInput_.normalized_digits.clear();
        matterCodeError_.clear();
        ESP_LOGI("CONTROL_UI", "matter_backspace len=%u",
                 static_cast<unsigned>(matterPairingInput_.raw_input.size()));
    }

    void clearMatterCode() {
        resetMatterCode();
        ESP_LOGI("CONTROL_UI", "matter_clear");
    }

    void validateMatterCode() {
        ESP_LOGI("CONTROL_UI", "matter_validate len=%u",
                 static_cast<unsigned>(matterPairingInput_.raw_input.size()));
        std::string normalized;
        std::string error;
        if (!normalizeMatterCode(matterPairingInput_.raw_input, normalized, error)) {
            matterPairingInput_.normalized_digits.clear();
            matterPairingInput_.format_valid = false;
            matterCodeError_ = error;
            transition(AppState::MATTER_CODE_INVALID);
            return;
        }
        matterPairingInput_.normalized_digits = normalized;
        matterPairingInput_.format_valid = true;
        matterCodeError_.clear();
        transition(AppState::MATTER_CODE_CONFIRM);
    }

    void continueAfterMatterCodeConfirm() {
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
        if (!matterPairingInput_.format_valid || matterPairingInput_.normalized_digits.empty()) {
            validateMatterCode();
            if (!matterPairingInput_.format_valid) return;
        }
        matterCommissioningResult_ = {};
        wifi_.releaseTransientBuffers();
        control_heap_log("before_matter_pairing");
        const esp_err_t networkReady = wifi_.waitMatterNetworkReady(MATTER_NETWORK_READY_TIMEOUT_MS);
        if (networkReady != ESP_OK) {
            ESP_LOGW("CONTROL_MATTER", "commissioning_preflight_failed reason=network_not_ready err=%s",
                     esp_err_to_name(networkReady));
            matterCommissioningResult_.status = MatterCommissioningStatus::FAILED;
            matterCommissioningResult_.error = networkReady;
            matterCommissioningResult_.failure_stage = "network_preflight";
            matterCommissioningResult_.failure_reason = "network_not_ready";
            matterCommissioningResult_.errorText = "Wi-Fi/IPv6 non prêt pour Matter";
            matterCodeError_ = matterCommissioningResult_.errorText;
            transition(AppState::MATTER_COMMISSIONING_FAILED);
            return;
        }
        const esp_err_t timeReady = control_time_wait_synced(MATTER_SNTP_READY_TIMEOUT_MS);
        if (timeReady != ESP_OK) {
            ESP_LOGW("CONTROL_MATTER", "commissioning_preflight_warning reason=sntp_timeout_or_invalid err=%s",
                     esp_err_to_name(timeReady));
        } else {
            ESP_LOGI("CONTROL_MATTER", "commissioning_preflight time_valid_utc=yes");
        }
        ESP_LOGI("CONTROL_MATTER", "pairing_requires_controller_ready");
        ESP_LOGI("CONTROL_MATTER", "controller_ready=%s", matter_controller_is_ready() ? "yes" : "no");
        if (!matter_controller_is_ready() && !runEarlyMatterInit("pairing", true)) {
            return;
        }
        ESP_LOGI("CONTROL_MATTER", "commissioning_mode=%s", matterMultiAdminMode_ ? "multi_admin" : "initial");
        if (matterMultiAdminMode_) {
            ESP_LOGI("CONTROL_MATTER", "multi_admin_instruction=use_new_sharing_code_from_existing_fabric");
        }
        ESP_LOGI("CONTROL_NETWORK", "discovery_owner=chip pairing_api=pairing_code");
        transitionState(AppState::MATTER_COMMISSIONING);
        const bool progressRendered = renderUiEvenIfMatterQuiet("commissioning_progress_barrier");
        ESP_LOGI("CONTROL_MATTER", "commissioning_progress_screen_rendered=%s",
                 progressRendered ? "yes" : "no");
        if (display_.available() && !progressRendered) {
            matterCommissioningResult_.status = MatterCommissioningStatus::FAILED;
            matterCommissioningResult_.error = ESP_ERR_INVALID_STATE;
            matterCommissioningResult_.failure_stage = "progress_render";
            matterCommissioningResult_.failure_reason = "display_render_failed";
            matterCommissioningResult_.errorText = "Affichage progression Matter impossible";
            matterCodeError_ = matterCommissioningResult_.errorText;
            transition(AppState::MATTER_COMMISSIONING_FAILED);
            return;
        }
        matterCommissioningUiQuiet_.store(true);
        logTaskRuntimeDiagnostics("before_commissioning_worker");
        const esp_err_t err = startMatterCommissioningWorker();
        if (err != ESP_OK) {
            matterCommissioningUiQuiet_.store(false);
            MatterCommissioningResult result;
            (void)matter_controller_get_last_result(result);
            matterCommissioningResult_ = result;
            matterCodeError_ = result.errorText.empty()
                ? std::string("Commissioning Matter non disponible.")
                : result.errorText;
            transition(AppState::MATTER_COMMISSIONING_FAILED);
        }
#else
        (void)matterPairingInput_;
        ESP_LOGW("CONTROL_MATTER",
                 "commissioning disabled in build; CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL is not set");
        transition(AppState::MATTER_COMMISSIONING_PLACEHOLDER);
#endif
    }

#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    esp_err_t startMatterCommissioningWorker() {
        if (matterCommissioningTask_ != nullptr) {
            ESP_LOGW("CONTROL_MATTER", "commissioning worker already running");
            return ESP_ERR_INVALID_STATE;
        }
        const uint64_t preferredNodeId = matter_controller_get_preferred_remote_node_id();
        matterWorkerCode_ = matterPairingInput_.normalized_digits;
        matterWorkerNodeId_ = preferredNodeId;
        const BaseType_t created = xTaskCreate(
            [](void *arg) {
                auto *self = static_cast<ControlApplication *>(arg);
                ESP_LOGI("CONTROL_MATTER", "worker_stack_size=%u task=commissioning",
                         static_cast<unsigned>(MATTER_COMMISSIONING_WORKER_STACK_SIZE));
                ESP_LOGI("CONTROL_TASKS", "task_diag phase=commissioning_worker_start label=matter priority=%u stack_high_water=%u",
                         static_cast<unsigned>(uxTaskPriorityGet(nullptr)),
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
                const esp_err_t err = matter_controller_commission_with_manual_code(
                    self->matterWorkerCode_, self->matterWorkerNodeId_);
                if (err != ESP_OK) {
                    ESP_LOGW("CONTROL_MATTER", "commissioning_worker_start_failed err=%s", esp_err_to_name(err));
                }
                ESP_LOGI("CONTROL_MATTER", "worker_stack_high_water=%u task=commissioning",
                         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
                self->matterCommissioningTask_ = nullptr;
                vTaskDelete(nullptr);
            },
            "control_matter_task",
            MATTER_COMMISSIONING_WORKER_STACK_SIZE,
            this,
            MATTER_COMMISSIONING_WORKER_PRIORITY,
            &matterCommissioningTask_);
        if (created != pdPASS) {
            matterCommissioningTask_ = nullptr;
            ESP_LOGE("CONTROL_MATTER", "commissioning_worker_create_failed");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI("CONTROL_MATTER", "commissioning_worker_started");
        return ESP_OK;
    }
#endif

    void resetPasswordPartialCounters() {
        passwordKeyPressesSinceRefresh_ = 0;
        passwordPartialsSinceFullRefresh_ = 0;
        passwordFullRefreshPending_ = false;
    }

    void recordPasswordScreenPartial(const char *area) {
        ++passwordPartialsSinceFullRefresh_;
        ESP_LOGI("CONTROL_DISPLAY", "%s partial count=%d",
                 area ? area : "password_screen", passwordPartialsSinceFullRefresh_);
        if (passwordPartialsSinceFullRefresh_ >= CONFIG_CONTROL_PASSWORD_PARTIALS_BEFORE_FULL_REFRESH) {
            passwordFullRefreshPending_ = true;
            ESP_LOGI("CONTROL_DISPLAY", "password screen full refresh scheduled for ghosting cleanup");
        }
    }

    void runPendingPasswordFullRefresh() {
        if (passwordFullRefreshPending_ && state_ == AppState::WIFI_PASSWORD_ENTRY) {
            passwordFullRefreshPending_ = false;
            passwordPartialsSinceFullRefresh_ = 0;
            renderUi("ghosting_cleanup");
        }
    }

    void updateKeyboardMode(int nextMode) {
        if (state_ != AppState::WIFI_PASSWORD_ENTRY || openNetworkConfirmation_) return;
        if (keyboardMode_ == nextMode) return;
        const int previous = keyboardMode_;
        keyboardMode_ = nextMode;
        ESP_LOGI("CONTROL_UI", "keyboard_mode old=%s new=%s",
                 keyboardModeName(previous), keyboardModeName(keyboardMode_));
#if CONFIG_CONTROL_PASSWORD_KEYBOARD_PARTIAL_REFRESH
        if (ui_.refreshKeyboard(viewModel())) {
            recordPasswordScreenPartial("keyboard");
        }
        runPendingPasswordFullRefresh();
#else
        ESP_LOGI("CONTROL_UI", "keyboard partial refresh disabled; full password screen refresh for mode change");
        renderUi("keyboard_mode_change");
#endif
    }

    void refreshPasswordFieldAfterKey(bool force) {
        if (state_ != AppState::WIFI_PASSWORD_ENTRY || openNetworkConfirmation_) return;
#if CONFIG_CONTROL_PASSWORD_FIELD_PARTIAL_REFRESH
        if (!force) {
            ++passwordKeyPressesSinceRefresh_;
            if (passwordKeyPressesSinceRefresh_ < CONFIG_CONTROL_PASSWORD_REFRESH_EVERY_N_KEYS) {
                ESP_LOGI("CONTROL_UI", "password_field_refresh_deferred count=%d threshold=%d",
                         passwordKeyPressesSinceRefresh_, CONFIG_CONTROL_PASSWORD_REFRESH_EVERY_N_KEYS);
                return;
            }
        }
        passwordKeyPressesSinceRefresh_ = 0;
        if (ui_.refreshPasswordField(viewModel())) {
            recordPasswordScreenPartial("password");
        }
        runPendingPasswordFullRefresh();
#else
        (void)force;
#endif
    }

    AppState state_ = AppState::BOOT;
    DisplayManager display_;
    TouchManager touch_;
    WifiManager wifi_;
    SettingsStore storage_;
    MatterOnboarding matter_;
    IkeaDiscovery ikea_;
    UiManager ui_;
    std::string selectedSsid_;
    std::string password_;
    std::string savedSsid_;
    std::string savedPassword_;
    std::string failureReason_;
    size_t networkPage_ = 0;
    bool passwordVisible_ = false;
    bool openNetworkConfirmation_ = false;
    int keyboardMode_ = 0;
    int passwordKeyPressesSinceRefresh_ = 0;
    int passwordPartialsSinceFullRefresh_ = 0;
    bool passwordFullRefreshPending_ = false;
    bool showMatterInstructions_ = false;
    IkeaDiscoveryResult lastIkeaResult_;
    MatterPairingInput matterPairingInput_;
    MatterCommissioningResult matterCommissioningResult_;
    MatterSavedState matterSavedState_ = MatterSavedState::None;
    std::string matterSavedStateReason_;
    MatterSavedNode matterSavedNode_;
    MatterPostCommissioningResult matterPostResult_;
    MatterPostCommissioningStatus lastMatterPostStatus_ = MatterPostCommissioningStatus::IDLE;
    size_t matterDevicePage_ = 0;
    int selectedMatterDevice_ = -1;
    std::vector<RoomGroup> rooms_;
    int selectedRoom_ = -1;
    std::vector<uint16_t> batchCommandEndpoints_;
    size_t batchCommandPosition_ = 0;
    bool batchCommandOn_ = false;
    bool batchCommandActive_ = false;
    uint16_t lastMatterCommandEndpoint_ = 0;
    bool lastMatterCommandOn_ = false;
    std::string matterCodeError_;
    bool matterEarlyInitFailed_ = false;
    bool matterMultiAdminMode_ = false;
    bool pendingRender_ = false;
    AppState pendingRenderState_ = AppState::BOOT;
    const char *pendingRenderReason_ = "unspecified";
#if CONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL
    TaskHandle_t matterCommissioningTask_ = nullptr;
    std::string matterWorkerCode_;
    uint64_t matterWorkerNodeId_ = 0;
#endif
    int64_t connectionStartedUs_ = 0;
    int64_t autoConnectAtUs_ = 0;
    QueueHandle_t touchQueue_ = nullptr;
    TaskHandle_t touchTaskHandle_ = nullptr;
    std::atomic<bool> matterCommissioningUiQuiet_{false};
};
}

extern "C" void app_main(void) {
    static ControlApplication application;
    if (!application.initialize()) {
        ESP_LOGE("CONTROL_APP", "Initialisation incomplète; arrêt de la tâche main");
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI("CONTROL_TASKS", "main_stack_high_water_before_render=%u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    application.renderInitialScreen();
    ESP_LOGI("CONTROL_TASKS", "main_stack_high_water_after_render=%u",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    (void)application.startTouchTask();
    application.runRecoveryIdleLoop();
}
