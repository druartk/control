#include "touch/touch_manager.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include "board/board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "CONTROL_TOUCH";

// Goodix GT911 register map, matching the official SensorLib GT911 driver.
constexpr uint16_t GT911_REG_PRODUCT_ID = 0x8140;       // Four ASCII bytes.
constexpr uint16_t GT911_REG_MODULE_SWITCH_1 = 0x804D;  // Bits 1:0: IRQ mode.
constexpr uint16_t GT911_REG_COORD_STATUS = 0x814E;     // Ready bit + point count.
constexpr uint16_t GT911_REG_FIRST_POINT = 0x814F;      // Track ID, X, Y, size.
constexpr uint8_t GT911_DATA_READY = 0x80;
constexpr uint8_t GT911_TOUCH_COUNT_MASK = 0x0F;
constexpr uint8_t GT911_MAX_TOUCHES = 5;
constexpr size_t GT911_PRODUCT_ID_LENGTH = 4;
constexpr size_t GT911_POINT_BLOCK_LENGTH = 8;

constexpr int64_t POLL_INTERVAL_US = 30000;   // 33.3 Hz, independent of IRQ.
constexpr int64_t RELEASE_TIMEOUT_US = 90000; // Tolerate two missing updates.
constexpr int MOVE_DEBOUNCE_PIXELS = 1;

constexpr std::array<const char *, 5> CALIBRATION_POINTS{
    "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT", "CENTER"
};

const char *irqModeName(uint8_t moduleSwitch) {
    switch (moduleSwitch & 0x03) {
        case 0: return "rising-edge";
        case 1: return "falling-edge";
        case 2: return "low-level";
        case 3: return "high-level";
        default: return "unknown";
    }
}
}

const char *touchEventName(TouchEventType type) {
    switch (type) {
        case TouchEventType::NONE: return "NONE";
        case TouchEventType::PRESS: return "PRESS";
        case TouchEventType::MOVE: return "MOVE";
        case TouchEventType::RELEASE: return "RELEASE";
    }
    return "UNKNOWN";
}

const char *touchStateName(TouchInternalState state) {
    switch (state) {
        case TouchInternalState::IDLE: return "IDLE";
        case TouchInternalState::PRESSED: return "PRESSED";
        case TouchInternalState::MOVING: return "MOVING";
        case TouchInternalState::RELEASED: return "RELEASED";
    }
    return "UNKNOWN";
}

bool TouchManager::initialize() {
    // Official LILYGO wake sequence: IRQ high briefly; T_RST is not routed.
    gpio_config_t intWake{};
    intWake.pin_bit_mask = 1ULL << CONTROL_BOARD_TOUCH_IRQ;
    intWake.mode = GPIO_MODE_OUTPUT;
    intWake.pull_up_en = GPIO_PULLUP_DISABLE;
    intWake.pull_down_en = GPIO_PULLDOWN_DISABLE;
    esp_err_t err = gpio_config(&intWake);
    if (err != ESP_OK ||
        (err = gpio_set_level(CONTROL_BOARD_TOUCH_IRQ, CONTROL_BOARD_TOUCH_WAKE_LEVEL)) != ESP_OK) {
        status_ = "Échec réveil GT911";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    i2c_master_bus_config_t busConfig{};
    busConfig.i2c_port = I2C_NUM_0;
    busConfig.sda_io_num = CONTROL_BOARD_TOUCH_SDA;
    busConfig.scl_io_num = CONTROL_BOARD_TOUCH_SCL;
    busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
    busConfig.glitch_ignore_cnt = 7;
    busConfig.flags.enable_internal_pullup = true;
    err = i2c_new_master_bus(&busConfig, &bus_);
    if (err != ESP_OK) {
        status_ = "Échec bus I2C";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        return false;
    }

    for (uint8_t candidate : {static_cast<uint8_t>(0x14), static_cast<uint8_t>(0x5D)}) {
        if (i2c_master_probe(bus_, candidate, 100) == ESP_OK) {
            detectedAddress_ = candidate;
            break;
        }
    }
    if (detectedAddress_ == 0) {
        status_ = "GT911 introuvable";
        ESP_LOGE(TAG, "%s aux adresses 0x14/0x5D", status_.c_str());
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
        gpio_set_direction(CONTROL_BOARD_TOUCH_IRQ, GPIO_MODE_INPUT);
        return false;
    }

    i2c_device_config_t deviceConfig{};
    deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    deviceConfig.device_address = detectedAddress_;
    deviceConfig.scl_speed_hz = 400000;
    err = i2c_master_bus_add_device(bus_, &deviceConfig, &device_);
    if (err != ESP_OK) {
        status_ = "Échec ajout GT911";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
        gpio_set_direction(CONTROL_BOARD_TOUCH_IRQ, GPIO_MODE_INPUT);
        return false;
    }

    uint8_t productBytes[GT911_PRODUCT_ID_LENGTH]{};
    err = readRegister(GT911_REG_PRODUCT_ID, productBytes, sizeof(productBytes));
    if (err != ESP_OK) {
        status_ = "GT911 détecté, Product ID illisible";
        return false;
    }
    for (uint8_t value : productBytes) {
        if (value >= 0x20 && value <= 0x7E) productId_.push_back(static_cast<char>(value));
    }
    if (productId_.empty()) productId_ = "inconnu";

    // GPIO47 has an external 4.7 kOhm pull-up in the V2.4 schematic.
    gpio_config_t irqConfig{};
    irqConfig.pin_bit_mask = 1ULL << CONTROL_BOARD_TOUCH_IRQ;
    irqConfig.mode = GPIO_MODE_INPUT;
    irqConfig.pull_up_en = GPIO_PULLUP_DISABLE;
    irqConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
    irqConfig.intr_type = GPIO_INTR_ANYEDGE;
    err = gpio_config(&irqConfig);
    if (err != ESP_OK) {
        status_ = "Échec configuration IRQ GT911";
        ESP_LOGE(TAG, "%s: %s", status_.c_str(), esp_err_to_name(err));
        return false;
    }
    lastIrqLevel_ = gpio_get_level(CONTROL_BOARD_TOUCH_IRQ);

    esp_err_t isrErr = gpio_install_isr_service(0);
    if (isrErr == ESP_OK || isrErr == ESP_ERR_INVALID_STATE) {
        isrErr = gpio_isr_handler_add(CONTROL_BOARD_TOUCH_IRQ, irqHandler, this);
    }
    if (isrErr != ESP_OK) {
        ESP_LOGW(TAG, "IRQ hint indisponible: %s; polling reste actif", esp_err_to_name(isrErr));
    }

    uint8_t moduleSwitch = 0;
    if (readRegister(GT911_REG_MODULE_SWITCH_1, &moduleSwitch, 1) == ESP_OK) {
        ESP_LOGI(TAG, "GT911 IRQ mode register=0x%02X (%s); GPIO edge=ANYEDGE advisory",
                 moduleSwitch, irqModeName(moduleSwitch));
    }

    // Discard a possible stale boot packet. Every later ready packet is also cleared.
    if (writeRegister(GT911_REG_COORD_STATUS, 0) != ESP_OK) {
        status_ = "GT911 détecté, statut non acquittable";
        return false;
    }

    controllerDetected_ = true;
    available_ = true;
    updatePipelineStatus();
    ESP_LOGI(TAG, "GT911 initialisé à l'adresse 0x%02X, product=%s, SDA=%d SCL=%d IRQ=%d",
             detectedAddress_, productId_.c_str(), CONTROL_BOARD_TOUCH_SDA,
             CONTROL_BOARD_TOUCH_SCL, CONTROL_BOARD_TOUCH_IRQ);
    ESP_LOGI(TAG, "IRQ47 initial=%d pull=external-4.7k/internal-disabled edge=ANYEDGE",
             lastIrqLevel_);
    ESP_LOGI(TAG, "mode=Polling with IRQ hint interval=%lldms",
             static_cast<long long>(POLL_INTERVAL_US / 1000));
    ESP_LOGI(TAG, "pipeline: Controller detected=yes; Coordinates readable=pending; "
                  "Events working=pending; UI dispatch working=pending");
    return true;
}

esp_err_t TouchManager::readRegister(uint16_t reg, uint8_t *data, size_t length) {
    const uint8_t address[2] = {
        static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)
    };
    const esp_err_t err = i2c_master_transmit_receive(
        device_, address, sizeof(address), data, length, 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C read reg=0x%04X len=%u failed: %s",
                 reg, static_cast<unsigned>(length), esp_err_to_name(err));
    }
    return err;
}

esp_err_t TouchManager::writeRegister(uint16_t reg, uint8_t value) {
    const uint8_t data[3] = {
        static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), value
    };
    const esp_err_t err = i2c_master_transmit(device_, data, sizeof(data), 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg=0x%04X value=0x%02X failed: %s",
                 reg, value, esp_err_to_name(err));
    }
    return err;
}

TouchManager::TouchSample TouchManager::readSample() {
    TouchSample sample;
    if (readRegister(GT911_REG_COORD_STATUS, &sample.status, 1) != ESP_OK) {
        sample.kind = SampleKind::IO_ERROR;
        return sample;
    }
    lastStatus_ = sample.status;
    if ((sample.status & GT911_DATA_READY) == 0) {
        sample.kind = SampleKind::NO_DATA;
        return sample;
    }

    sample.count = sample.status & GT911_TOUCH_COUNT_MASK;
    if (sample.count > GT911_MAX_TOUCHES) {
        ESP_LOGW(TAG, "Invalid GT911 status=0x%02X count=%u", sample.status, sample.count);
        writeRegister(GT911_REG_COORD_STATUS, 0);
        sample.kind = SampleKind::INVALID;
        return sample;
    }
    if (sample.count == 0) {
        sample.kind = writeRegister(GT911_REG_COORD_STATUS, 0) == ESP_OK
            ? SampleKind::RELEASE : SampleKind::IO_ERROR;
        return sample;
    }

    uint8_t point[GT911_POINT_BLOCK_LENGTH]{};
    const esp_err_t pointErr = readRegister(GT911_REG_FIRST_POINT, point, sizeof(point));
    const esp_err_t clearErr = writeRegister(GT911_REG_COORD_STATUS, 0);
    if (pointErr != ESP_OK || clearErr != ESP_OK) {
        sample.kind = SampleKind::IO_ERROR;
        return sample;
    }

    // Byte 0 is track ID. X and Y are unsigned little-endian 16-bit values.
    sample.rawX = point[1] | (static_cast<int>(point[2]) << 8);
    sample.rawY = point[3] | (static_cast<int>(point[4]) << 8);
    transformCoordinates(sample.rawX, sample.rawY, sample.x, sample.y);
    sample.kind = SampleKind::POINT;
    return sample;
}

void TouchManager::transformCoordinates(int rawX, int rawY, int &x, int &y) const {
    int nativeX = std::clamp(rawX, 0, CONTROL_BOARD_TOUCH_NATIVE_MAX_X);
    int nativeY = std::clamp(rawY, 0, CONTROL_BOARD_TOUCH_NATIVE_MAX_Y);

#if CONTROL_BOARD_TOUCH_SWAP_XY
    x = nativeY;
    y = nativeX;
#else
    x = nativeX;
    y = nativeY;
#endif

#if CONTROL_BOARD_TOUCH_INVERT_X
    x = CONTROL_BOARD_DISPLAY_WIDTH - 1 - x;
#endif
#if CONTROL_BOARD_TOUCH_INVERT_Y
    y = CONTROL_BOARD_DISPLAY_HEIGHT - 1 - y;
#endif
    x = std::clamp(x, 0, CONTROL_BOARD_DISPLAY_WIDTH - 1);
    y = std::clamp(y, 0, CONTROL_BOARD_DISPLAY_HEIGHT - 1);
}

TouchEvent TouchManager::makeEvent(TouchEventType type, const TouchSample *sample) {
    TouchEvent event;
    event.type = type;
    event.irqLevel = gpio_get_level(CONTROL_BOARD_TOUCH_IRQ);
    lastIrqLevel_ = event.irqLevel;
    if (sample != nullptr) {
        event.status = sample->status;
        event.count = sample->count;
        event.rawX = sample->rawX;
        event.rawY = sample->rawY;
        event.x = sample->x;
        event.y = sample->y;
    }

    latestEvent_ = type;
    if (!eventsWorking_) {
        eventsWorking_ = true;
        updatePipelineStatus();
        ESP_LOGI(TAG, "pipeline: Events working=yes");
    }

    if (type == TouchEventType::PRESS) {
        ESP_LOGI(TAG, "PRESS status=0x%02X count=%u raw=(%d,%d) transformed=(%d,%d) irq=%d",
                 event.status, event.count, event.rawX, event.rawY, event.x, event.y,
                 event.irqLevel);
        if (calibrationMode_ && calibrationPoint_ < CALIBRATION_POINTS.size()) {
            ESP_LOGI(TAG, "CALIBRATION point=%s raw=(%d,%d) transformed=(%d,%d)",
                     CALIBRATION_POINTS[calibrationPoint_], event.rawX, event.rawY,
                     event.x, event.y);
            ++calibrationPoint_;
        }
    } else if (type == TouchEventType::MOVE) {
#if CONFIG_CONTROL_TOUCH_DEBUG
        ESP_LOGI(TAG, "MOVE raw=(%d,%d) transformed=(%d,%d) irq=%d",
                 event.rawX, event.rawY, event.x, event.y, event.irqLevel);
#endif
    } else if (type == TouchEventType::RELEASE) {
        ESP_LOGI(TAG, "RELEASE");
    }
    return event;
}

TouchEvent TouchManager::poll() {
    if (!available_) return {};
    const int64_t now = esp_timer_get_time();
    if (now < nextPollUs_) return {};
    nextPollUs_ = now + POLL_INTERVAL_US;

    if (internalState_ == TouchInternalState::RELEASED) {
        internalState_ = TouchInternalState::IDLE;
    }

    if (irqEdgeObserved_ && !irqEdgeLogged_) {
        irqEdgeLogged_ = true;
        ESP_LOGI(TAG, "IRQ47 edge observed=yes current=%d (advisory only)",
                 gpio_get_level(CONTROL_BOARD_TOUCH_IRQ));
    }

    const TouchSample sample = readSample();
    if (sample.kind == SampleKind::POINT) {
        lastPointUs_ = now;
        lastStatus_ = sample.status;
        if (!coordinatesReadable_) {
            coordinatesReadable_ = true;
            updatePipelineStatus();
            ESP_LOGI(TAG, "pipeline: Coordinates readable=yes");
        }

        const bool duplicate = contactActive_ &&
            std::abs(sample.x - lastX_) <= MOVE_DEBOUNCE_PIXELS &&
            std::abs(sample.y - lastY_) <= MOVE_DEBOUNCE_PIXELS;
        lastRawX_ = sample.rawX;
        lastRawY_ = sample.rawY;
        lastX_ = sample.x;
        lastY_ = sample.y;

        if (!contactActive_) {
            contactActive_ = true;
            internalState_ = TouchInternalState::PRESSED;
            return makeEvent(TouchEventType::PRESS, &sample);
        }
        if (!duplicate) {
            internalState_ = TouchInternalState::MOVING;
            return makeEvent(TouchEventType::MOVE, &sample);
        }
        return {};
    }

    const bool definitiveRelease = sample.kind == SampleKind::RELEASE;
    const bool staleContact = contactActive_ && lastPointUs_ != 0 &&
        now - lastPointUs_ >= RELEASE_TIMEOUT_US;
    if (contactActive_ && (definitiveRelease || staleContact)) {
        contactActive_ = false;
        internalState_ = TouchInternalState::RELEASED;
        // No stale coordinates are attached to RELEASE.
        return makeEvent(TouchEventType::RELEASE, nullptr);
    }
    return {};
}

TouchDiagnostics TouchManager::diagnostics() const {
    TouchDiagnostics result;
    result.address = detectedAddress_;
    result.productId = productId_;
    result.rawX = lastRawX_;
    result.rawY = lastRawY_;
    result.transformedX = lastX_;
    result.transformedY = lastY_;
    result.latestEvent = latestEvent_;
    result.internalState = internalState_;
    result.irqLevel = lastIrqLevel_;
    result.irqEdgeObserved = irqEdgeObserved_;
    result.controllerDetected = controllerDetected_;
    result.coordinatesReadable = coordinatesReadable_;
    result.eventsWorking = eventsWorking_;
    result.uiDispatchWorking = uiDispatchWorking_;
    return result;
}

void TouchManager::markUiDispatched() {
    if (uiDispatchWorking_) return;
    uiDispatchWorking_ = true;
    updatePipelineStatus();
    ESP_LOGI(TAG, "pipeline: UI dispatch working=yes");
}

void TouchManager::setCalibrationMode(bool enabled) {
    calibrationMode_ = enabled;
    calibrationPoint_ = 0;
    if (enabled) {
        ESP_LOGI(TAG, "Serial calibration enabled: touch TOP_LEFT, TOP_RIGHT, "
                      "BOTTOM_LEFT, BOTTOM_RIGHT, CENTER in this order");
    } else {
        ESP_LOGI(TAG, "Serial calibration disabled");
    }
}

void TouchManager::updatePipelineStatus() {
    if (!controllerDetected_) {
        status_ = "Controller not detected";
    } else if (!coordinatesReadable_) {
        status_ = "Controller detected; awaiting coordinates";
    } else if (!eventsWorking_) {
        status_ = "Coordinates readable; awaiting events";
    } else if (!uiDispatchWorking_) {
        status_ = "Events working; awaiting UI dispatch";
    } else {
        status_ = "UI dispatch working";
    }
}

void IRAM_ATTR TouchManager::irqHandler(void *argument) {
    auto *manager = static_cast<TouchManager *>(argument);
    manager->irqEdgeObserved_ = true;
}
