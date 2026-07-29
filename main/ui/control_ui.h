#pragma once

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "display/display_manager.h"
#include "touch/touch_manager.h"
#include "ui/control_ui_model.h"
#include "matter/matter_controller.h"

enum class ControlUiScreen {
    HOME,
    ROOMS,
    ROOM_DETAIL,
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
        DETAIL_BACK,
        DETAIL_ALL_ON,
        DETAIL_ALL_OFF,
        DETAIL_DEVICE_0,
        DETAIL_DEVICE_1,
        DETAIL_DEVICE_2,
    };

    void renderHome();
    void renderRooms();
    void renderRoomDetail();
    TouchZone zoneAt(int x, int y) const;
    void activate(TouchZone zone);
    bool queueDevice(UiDevice &device, bool on, uint32_t batchId = 0);
    void queueRoom(UiRoom &room, bool on, uint32_t batchId);
    void markScreen(ControlUiScreen screen);
    const char *screenName() const;
    const char *roomName(size_t roomIndex) const;
    size_t activeCount(const UiRoom &room) const;
    bool lockRender();
    void unlockRender();

    DisplayManager &display_;
    UiRoom *rooms_ = nullptr;
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
