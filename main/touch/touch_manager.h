#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "driver/i2c_master.h"
#include "esp_attr.h"

enum class TouchEventType { NONE, PRESS, MOVE, RELEASE };
enum class TouchInternalState { IDLE, PRESSED, MOVING, RELEASED };

const char *touchEventName(TouchEventType type);
const char *touchStateName(TouchInternalState state);

struct TouchEvent {
    TouchEventType type = TouchEventType::NONE;
    uint8_t status = 0;
    uint8_t count = 0;
    int rawX = -1;
    int rawY = -1;
    int x = -1;
    int y = -1;
    int irqLevel = -1;
};

struct TouchDiagnostics {
    uint8_t address = 0;
    std::string productId;
    std::string mode = "Polling with IRQ hint";
    int rawX = -1;
    int rawY = -1;
    int transformedX = -1;
    int transformedY = -1;
    TouchEventType latestEvent = TouchEventType::NONE;
    TouchInternalState internalState = TouchInternalState::IDLE;
    int irqLevel = -1;
    bool irqEdgeObserved = false;
    bool controllerDetected = false;
    bool coordinatesReadable = false;
    bool eventsWorking = false;
    bool uiDispatchWorking = false;
};

class TouchManager {
public:
    bool initialize();
    TouchEvent poll();
    bool available() const { return available_; }
    const char *statusText() const { return status_.c_str(); }
    TouchDiagnostics diagnostics() const;
    void markUiDispatched();
    void setCalibrationMode(bool enabled);

private:
    enum class SampleKind { NO_DATA, POINT, RELEASE, INVALID, IO_ERROR };

    struct TouchSample {
        SampleKind kind = SampleKind::NO_DATA;
        uint8_t status = 0;
        uint8_t count = 0;
        int rawX = -1;
        int rawY = -1;
        int x = -1;
        int y = -1;
    };

    esp_err_t readRegister(uint16_t reg, uint8_t *data, size_t length);
    esp_err_t writeRegister(uint16_t reg, uint8_t value);
    TouchSample readSample();
    void transformCoordinates(int rawX, int rawY, int &x, int &y) const;
    TouchEvent makeEvent(TouchEventType type, const TouchSample *sample = nullptr);
    void updatePipelineStatus();
    static void IRAM_ATTR irqHandler(void *argument);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;
    bool available_ = false;
    bool contactActive_ = false;
    bool controllerDetected_ = false;
    bool coordinatesReadable_ = false;
    bool eventsWorking_ = false;
    bool uiDispatchWorking_ = false;
    bool irqEdgeLogged_ = false;
    bool calibrationMode_ = false;
    uint8_t detectedAddress_ = 0;
    uint8_t calibrationPoint_ = 0;
    uint8_t lastStatus_ = 0;
    int lastRawX_ = -1;
    int lastRawY_ = -1;
    int lastX_ = -1;
    int lastY_ = -1;
    int lastIrqLevel_ = -1;
    int64_t nextPollUs_ = 0;
    int64_t lastPointUs_ = 0;
    volatile bool irqEdgeObserved_ = false;
    TouchEventType latestEvent_ = TouchEventType::NONE;
    TouchInternalState internalState_ = TouchInternalState::IDLE;
    std::string productId_;
    std::string status_ = "Non initialisé";
};
