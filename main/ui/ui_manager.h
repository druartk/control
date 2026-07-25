#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "app_state.h"
#include "display/display_manager.h"
#include "ikea/ikea_discovery.h"
#include "matter/matter_controller.h"
#include "weather/weather_manager.h"
#include "wifi/wifi_manager.h"

enum class UiActionType {
    NONE,
    CONFIGURE_WIFI,
    BACK,
    REFRESH,
    PAGE_PREVIOUS,
    PAGE_NEXT,
    SELECT_NETWORK,
    KEY_CHARACTER,
    KEY_BACKSPACE,
    KEY_CLEAR,
    KEY_SHIFT,
    KEY_SYMBOLS,
    TOGGLE_PASSWORD,
    CANCEL,
    CONNECT,
    RETRY,
    ERASE_CREDENTIALS,
    CONTINUE_MATTER,
    SEARCH_MATTER,
    SHOW_INSTRUCTIONS,
    BACK_TO_WIFI,
    SEARCH_IKEA,
    RETRY_IKEA,
    MATTER_CODE_VALIDATE,
    MATTER_CODE_EDIT,
    MATTER_PAIR_INITIAL,
    MATTER_PAIR_MULTI_ADMIN,
    MATTER_CONTINUE_ANYWAY,
    MATTER_USE_SAVED,
    MATTER_PAIR_CODE,
    MATTER_NETWORK_DIAGNOSTIC,
    MATTER_DEVICE_SELECT,
    MATTER_ON,
    MATTER_OFF,
    MATTER_FORGET,
    MATTER_FORGET_CONFIRM,
    MATTER_RESTART,
    OPEN_HOME,
    OPEN_WEATHER,
    OPEN_ROOM,
    ROOM_ON,
    ROOM_OFF,
    ROOM_DETAIL,
    DIAGNOSTIC,
    WEATHER_REFRESH,
    DISCOVER_DEVICES,
    PAIR_MATTER,
    RESET_MATTER,
};

struct UiAction {
    UiActionType type = UiActionType::NONE;
    int index = -1;
    char character = 0;
};

struct RoomGroup {
    char name[32] = {};
    uint16_t endpoint_ids[16] = {};
    uint8_t endpoint_count = 0;
    uint8_t lights_on_count = 0;
    uint8_t controllable_count = 0;
};

struct UiViewModel {
    AppState state = AppState::BOOT;
    std::string displayStatus;
    std::string touchStatus;
    std::string wifiStatus;
    std::string storageStatus;
    std::vector<WifiNetwork> networks;
    size_t networkPage = 0;
    std::string selectedSsid;
    std::string password;
    bool passwordVisible = false;
    int keyboardMode = 0; // 0 lower, 1 upper, 2 symbols
    bool openNetworkConfirmation = false;
    std::string failureReason;
    WifiConnectionInfo connection;
    IkeaDiscoveryResult ikea;
    uint8_t touchAddress = 0;
    std::string touchProductId;
    std::string touchMode;
    int touchRawX = -1;
    int touchRawY = -1;
    int touchX = -1;
    int touchY = -1;
    std::string touchLatestEvent = "NONE";
    std::string touchInternalState = "IDLE";
    int touchIrqLevel = -1;
    bool touchIrqEdgeObserved = false;
    bool touchControllerDetected = false;
    bool touchCoordinatesReadable = false;
    bool touchEventsWorking = false;
    bool touchUiDispatchWorking = false;
    bool showMatterInstructions = false;
    std::string matterCodeRaw;
    std::string matterCodeError;
    bool matterCodeFormatValid = false;
    bool matterAttestationVerified = false;
    bool matterAttestationBypassed = false;
    bool matterDevInsecureMode = false;
    std::string matterFailureStage;
    std::string matterFailureReason;
    MatterSavedState matterSavedState = MatterSavedState::None;
    std::string matterSavedStateReason;
    MatterSavedNode matterSavedNode;
    MatterPostCommissioningResult matterPost;
    size_t matterDevicePage = 0;
    int selectedMatterDevice = -1;
    WeatherCurrent weatherCurrent;
    std::vector<WeatherForecastDay> weatherForecast;
    bool weatherRefreshing = false;
    bool wifiConfigured = false;
    bool matterEarlyInitFailed = false;
    std::vector<RoomGroup> rooms;
    int selectedRoom = -1;
    size_t roomPage = 0;
};

class UiManager {
public:
    explicit UiManager(DisplayManager &display) : display_(display) {}
    bool render(const UiViewModel &model);
    bool refreshPasswordField(const UiViewModel &model);
    bool refreshPasswordVisibilityButton(const UiViewModel &model);
    bool refreshKeyboard(const UiViewModel &model);
    bool refreshMatterCodeField(const UiViewModel &model);
    UiAction hitTest(const UiViewModel &model, int x, int y) const;

private:
    void renderDiagnostics(const UiViewModel &model);
    void renderNetworks(const UiViewModel &model);
    void renderPassword(const UiViewModel &model);
    void renderPasswordField(const UiViewModel &model);
    void renderPasswordVisibilityButton(const UiViewModel &model);
    void renderKeyboard(const UiViewModel &model);
    void renderMatterCodeEntry(const UiViewModel &model);
    void renderMatterCodeField(const UiViewModel &model);
    void renderWeather(const UiViewModel &model);
    void renderHomeControl(const UiViewModel &model);
    void renderRoomDetail(const UiViewModel &model);
    void renderSettings(const UiViewModel &model);
    UiAction hitTestInternal(const UiViewModel &model, int x, int y) const;
    static std::string signalBars(int rssi);
    static std::string maskedPassword(size_t length);
    static std::string formattedMatterCode(const std::string &raw);

    DisplayManager &display_;
};
