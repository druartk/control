#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "display/display_manager.h"
#include "touch/touch_manager.h"
#include "ui/control_ui_model.h"
#include "matter/matter_controller.h"
#include "storage/ui_config.h"

enum class ControlUiScreen {
    HOME,
    ROOMS,
    ROOM_DETAIL,
    SETTINGS,
    DEVICE_SCAN,
    DEVICE_SCAN_RESULT,
    DEVICE_ASSIGN_ROOM,
    DEVICE_RENAME,
    ROOM_CREATE,
    ONBOARDING_SUMMARY,
    CLEAR_CONFIG_CONFIRM,
};

class ControlUi {
public:
    explicit ControlUi(DisplayManager &display) : display_(display) {}

    void init();
    void showHome();
    void showRooms();
    void showRoom(size_t roomIndex);
    void setContext(bool wifiConnected, const char *clockText);
    void bindMatterDevices(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices);
    void bindPersistedDevices(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices,
                              const UiConfig &config);
    bool updateDiscovery(const MatterDiscoveryResult &result);
    void beginOnboarding();
    bool onboardingActive() const { return onboarding_; }
    void completeMatterCommand(const MatterUiResult &result);
    bool handleTouch(const TouchEvent &event);
    bool render(bool forceFullRefresh);

private:
    enum class TouchZone : uint8_t {
        NONE,
        HOME_ALL_OFF,
        HOME_ROOM_0,
        HOME_ROOM_1,
        HOME_ROOM_2,
        FOOTER_HOME,
        FOOTER_ROOMS,
        ROOMS_BACK,
        ROOMS_ROOM_0,
        ROOMS_ROOM_1,
        ROOMS_ROOM_2,
        ROOMS_ROOM_3,
        ROOMS_ROOM_4,
        ROOMS_ROOM_5,
        DETAIL_BACK,
        DETAIL_ALL_ON,
        DETAIL_ALL_OFF,
        DETAIL_DEVICE_0,
        DETAIL_DEVICE_1,
        DETAIL_DEVICE_2,
        DETAIL_DEVICE_3,
        DETAIL_DEVICE_4,
        HOME_SETTINGS,
        SETTINGS_BACK,
        SETTINGS_CONFIGURE,
        SETTINGS_RESCAN,
        SETTINGS_REASSIGN,
        SETTINGS_CLEAR,
        CLEAR_CONFIRM_YES,
        CLEAR_CONFIRM_NO,
        SCAN_RESCAN,
        SCAN_CONFIGURE,
        SCAN_BACK,
        SCAN_NEXT,
        ASSIGN_ROOM_0,
        ASSIGN_ROOM_1,
        ASSIGN_ROOM_2,
        ASSIGN_ROOM_3,
        ASSIGN_ROOM_4,
        ASSIGN_ROOM_5,
        ASSIGN_NEW_ROOM,
        ASSIGN_RENAME,
        ASSIGN_SKIP,
        ASSIGN_CONFIRM,
        RENAME_0,
        RENAME_1,
        RENAME_2,
        RENAME_BACK,
        ROOM_CREATE_0,
        ROOM_CREATE_1,
        ROOM_CREATE_2,
        ROOM_CREATE_BACK,
        SUMMARY_SAVE,
        SUMMARY_BACK,
    };

    void renderHome();
    void renderRooms();
    void renderRoomDetail();
    void renderSettings();
    void renderDeviceScan();
    void renderScanResult();
    void renderAssignRoom();
    void renderRenameDevice();
    void renderRoomCreate();
    void renderSummary();
    void renderClearConfirm();
    TouchZone zoneAt(int x, int y) const;
    void activate(TouchZone zone);
    bool queueDevice(UiDevice &device, bool on, uint32_t batchId = 0);
    void queueRoom(UiRoom &room, bool on, uint32_t batchId);
    void markScreen(ControlUiScreen screen);
    const char *screenName() const;
    const char *roomName(size_t roomIndex) const;
    size_t activeCount(const UiRoom &room) const;
    void rebuildFromConfig(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices,
                           const UiConfig &config);
    size_t candidateCount() const;
    const MatterDiscoveredDevice *candidateAt(size_t index) const;
    void chooseRoom(size_t roomIndex);
    void saveOnboarding();
    void resetRealModel();
    bool lockRender();
    void unlockRender();

    DisplayManager &display_;
    UiRoom *rooms_ = nullptr;
    UiRoom realRooms_[CONTROL_UI_CONFIG_MAX_ROOMS]{};
    UiDevice realDevices_[CONTROL_UI_CONFIG_MAX_BINDINGS]{};
    char realRoomNames_[CONTROL_UI_CONFIG_MAX_ROOMS][32]{};
    char realDeviceNames_[CONTROL_UI_CONFIG_MAX_BINDINGS][48]{};
    size_t roomCount_ = 0;
    size_t realDeviceCount_ = 0;
    uint64_t nodeId_ = 0;
    UiConfig config_{};
    UiConfig workingConfig_{};
    MatterDiscoveryResult discovery_{};
    bool hasConfig_ = false;
    bool missingBinding_ = false;
    bool onboarding_ = false;
    size_t selectedDevice_ = 0;
    size_t selectedRoomChoice_ = 0;
    char pendingCustomName_[48] = {};
    ControlUiScreen screen_ = ControlUiScreen::HOME;
    size_t selectedRoom_ = 0;
    bool dirty_ = true;
    bool wifiConnected_ = false;
    char clockText_[8] = "--:--";
    TouchZone pressedZone_ = TouchZone::NONE;
    ControlUiScreen pressedScreen_ = ControlUiScreen::HOME;
    bool pressActive_ = false;
    bool pressMoved_ = false;
    uint32_t nextBatchId_ = 1;
    uint32_t activeBatchId_ = 0;
    size_t activeBatchExpected_ = 0;
    size_t activeBatchSuccess_ = 0;
    size_t activeBatchFailed_ = 0;
    char activeBatchRoom_[32] = {};
    SemaphoreHandle_t renderMutex_ = nullptr;
    StaticSemaphore_t renderMutexStorage_{};
};
