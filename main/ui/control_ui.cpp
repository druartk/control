#include "ui/control_ui.h"

#include <cstdio>
#include <cctype>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace {
constexpr const char *TAG = "CONTROL_UI";
constexpr UiRect HOME_ALL_OFF{32, 96, 896, 78};
constexpr UiRect HOME_ROOM_ROWS[] = {
    {32, 195, 896, 70},
    {32, 280, 896, 70},
    {32, 365, 896, 70},
};
constexpr UiRect FOOTER_HOME{32, 480, 430, 45};
constexpr UiRect FOOTER_ROOMS{498, 480, 430, 45};
constexpr UiRect ROOMS_BACK{32, 90, 200, 60};
constexpr UiRect ROOMS_ROWS[] = {
    {32, 170, 896, 78},
    {32, 265, 896, 78},
    {32, 360, 896, 78},
};
constexpr UiRect DETAIL_BACK{32, 90, 190, 58};
constexpr UiRect DETAIL_ALL_ON{245, 90, 320, 58};
constexpr UiRect DETAIL_ALL_OFF{590, 90, 338, 58};
constexpr int DETAIL_DEVICE_TOP = 175;
constexpr int DETAIL_DEVICE_HEIGHT = 58;

const char *screenName(ControlUiScreen screen) {
    switch (screen) {
        case ControlUiScreen::HOME: return "home";
        case ControlUiScreen::ROOMS: return "rooms";
        case ControlUiScreen::ROOM_DETAIL: return "room_detail";
    }
    return "unknown";
}

std::string countText(size_t active, size_t total) {
    return std::to_string(active) + " / " + std::to_string(total);
}

struct MatterUiBinding {
    uint32_t ui_device_id;
    const char *room_name;
    const char *device_name;
};

constexpr MatterUiBinding MATTER_UI_BINDINGS[] = {
    {101, "Salon", "Plafonnier"},
    {102, "Salon", "Lampe canap"},
    {103, "Salon", "Lampe TV"},
    {201, "Cuisine", "Plafonnier"},
    {202, "Cuisine", "Plan de travail"},
    {301, "Chambre", "Plafonnier"},
    {302, "Chambre", "Lampe de chevet"},
};

std::string lower(std::string value) {
    for (char &c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool containsInsensitive(const std::string &value, const char *needle) {
    return lower(value).find(lower(needle ? needle : "")) != std::string::npos;
}

const MatterUiBinding *bindingFor(uint32_t uiDeviceId) {
    for (const auto &binding : MATTER_UI_BINDINGS) {
        if (binding.ui_device_id == uiDeviceId) return &binding;
    }
    return nullptr;
}
}

void ControlUi::init() {
    if (renderMutex_ == nullptr) renderMutex_ = xSemaphoreCreateMutexStatic(&renderMutexStorage_);
    rooms_ = control_ui_mock_rooms();
    control_ui_mock_reset();
    screen_ = ControlUiScreen::HOME;
    selectedRoom_ = 0;
    pressedZone_ = TouchZone::NONE;
    pressActive_ = false;
    pressMoved_ = false;
    dirty_ = true;
    ESP_LOGI(TAG, "init");
    ESP_LOGI(TAG, "screen=home");
}

void ControlUi::setContext(bool wifiConnected, const char *clockText) {
    wifiConnected_ = wifiConnected;
    std::strncpy(clockText_, clockText ? clockText : "--:--", sizeof(clockText_) - 1);
    clockText_[sizeof(clockText_) - 1] = '\0';
}

void ControlUi::bindMatterDevices(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices) {
    for (size_t roomIndex = 0; roomIndex < CONTROL_UI_ROOM_COUNT; ++roomIndex) {
        UiRoom &room = rooms_[roomIndex];
        for (size_t deviceIndex = 0; deviceIndex < room.device_count; ++deviceIndex) {
            UiDevice &uiDevice = room.devices[deviceIndex];
            uiDevice.matter_bound = false;
            uiDevice.matter_node_id = 0;
            uiDevice.matter_endpoint_id = 0;
            uiDevice.supports_onoff = false;
            uiDevice.command_pending = false;
            uiDevice.command_failed = false;
            uiDevice.pending_batch_id = 0;

            const MatterUiBinding *binding = bindingFor(uiDevice.id);
            if (!binding) continue;
            size_t match = 0;
            size_t matchCount = 0;
            for (size_t i = 0; i < devices.size(); ++i) {
                if (!devices[i].has_onoff ||
                    !containsInsensitive(devices[i].name, binding->room_name) ||
                    !containsInsensitive(devices[i].name, binding->device_name)) continue;
                match = i;
                ++matchCount;
            }
            if (matchCount != 1) {
                if (matchCount > 1) {
                    ESP_LOGW(TAG, "unbound_device device=%u reason=ambiguous_cache_match count=%u",
                             static_cast<unsigned>(uiDevice.id), static_cast<unsigned>(matchCount));
                }
                continue;
            }
            const MatterDeviceItem &matterDevice = devices[match];
            uiDevice.matter_bound = nodeId != 0;
            uiDevice.matter_node_id = nodeId;
            uiDevice.matter_endpoint_id = matterDevice.endpoint_id;
            uiDevice.supports_onoff = matterDevice.has_onoff;
            if (matterDevice.onoff_state_known) uiDevice.is_on = matterDevice.onoff_state;
            ESP_LOGI(TAG, "binding device=%u node=0x%llx endpoint=%u name=\"%s\"",
                     static_cast<unsigned>(uiDevice.id),
                     static_cast<unsigned long long>(nodeId),
                     static_cast<unsigned>(matterDevice.endpoint_id), matterDevice.name.c_str());
        }
    }
    dirty_ = true;
}

void ControlUi::completeMatterCommand(const MatterUiResult &result) {
    for (size_t roomIndex = 0; roomIndex < CONTROL_UI_ROOM_COUNT; ++roomIndex) {
        UiRoom &room = rooms_[roomIndex];
        for (size_t deviceIndex = 0; deviceIndex < room.device_count; ++deviceIndex) {
            UiDevice &device = room.devices[deviceIndex];
            if (device.id != result.command.ui_device_id ||
                device.matter_endpoint_id != result.command.endpoint_id ||
                !device.command_pending) continue;
            device.command_pending = false;
            device.command_failed = !result.success;
            if (result.success) {
                device.is_on = result.command.action == MatterUiAction::SetOn;
            }
            if (device.pending_batch_id == activeBatchId_ && activeBatchId_ != 0) {
                result.success ? ++activeBatchSuccess_ : ++activeBatchFailed_;
                if (activeBatchSuccess_ + activeBatchFailed_ == activeBatchExpected_) {
                    ESP_LOGI(TAG, "room_command_complete room=%s success=%u failed=%u",
                             activeBatchRoom_, static_cast<unsigned>(activeBatchSuccess_),
                             static_cast<unsigned>(activeBatchFailed_));
                    activeBatchId_ = 0;
                }
            }
            dirty_ = true;
            return;
        }
    }
}

void ControlUi::showHome() {
    markScreen(ControlUiScreen::HOME);
    ESP_LOGI(TAG, "screen=home");
}

void ControlUi::showRooms() {
    markScreen(ControlUiScreen::ROOMS);
    ESP_LOGI(TAG, "screen=rooms");
}

void ControlUi::showRoom(size_t roomIndex) {
    if (roomIndex >= CONTROL_UI_ROOM_COUNT) return;
    selectedRoom_ = roomIndex;
    markScreen(ControlUiScreen::ROOM_DETAIL);
    ESP_LOGI(TAG, "screen=room_detail room=%s", roomName(selectedRoom_));
}

void ControlUi::markScreen(ControlUiScreen screen) {
    screen_ = screen;
    dirty_ = true;
    pressedZone_ = TouchZone::NONE;
    pressActive_ = false;
    pressMoved_ = false;
}

size_t ControlUi::activeCount(const UiRoom &room) const {
    size_t count = 0;
    for (size_t i = 0; i < room.device_count; ++i) {
        if (room.devices[i].is_on) ++count;
    }
    return count;
}

const char *ControlUi::roomName(size_t roomIndex) const {
    return rooms_ && roomIndex < CONTROL_UI_ROOM_COUNT ? rooms_[roomIndex].name : "";
}

const char *ControlUi::screenName() const {
    return ::screenName(screen_);
}

bool ControlUi::lockRender() {
    return renderMutex_ != nullptr && xSemaphoreTake(renderMutex_, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void ControlUi::unlockRender() {
    if (renderMutex_ != nullptr) xSemaphoreGive(renderMutex_);
}

bool ControlUi::render(bool forceFullRefresh) {
    if (!dirty_ && !forceFullRefresh) return true;
    if (!display_.available() || !rooms_ || !lockRender()) return false;
    const int64_t started = esp_timer_get_time();
    ESP_LOGI(TAG, "render_start screen=%s", screenName());
    display_.beginFrame();
    switch (screen_) {
        case ControlUiScreen::HOME: renderHome(); break;
        case ControlUiScreen::ROOMS: renderRooms(); break;
        case ControlUiScreen::ROOM_DETAIL: renderRoomDetail(); break;
    }
    const bool committed = display_.commitFull(screenName());
    if (committed) {
        dirty_ = false;
        ESP_LOGI(TAG, "render_done screen=%s duration_ms=%u", screenName(),
                 static_cast<unsigned>((esp_timer_get_time() - started) / 1000));
    }
    unlockRender();
    return committed;
}

void ControlUi::renderHome() {
    display_.title("Maison");
    display_.text(680, 45, clockText_);
    display_.text(790, 45, wifiConnected_ ? "Wi-Fi OK" : "Wi-Fi --");
    display_.button(HOME_ALL_OFF, "Tout eteindre", true);
    for (size_t i = 0; i < CONTROL_UI_ROOM_COUNT; ++i) {
        const UiRoom &room = rooms_[i];
        display_.rectangle(HOME_ROOM_ROWS[i]);
        display_.text(55, HOME_ROOM_ROWS[i].y + 29, room.name);
        display_.text(55, HOME_ROOM_ROWS[i].y + 55,
                      countText(activeCount(room), room.device_count));
        display_.text(875, HOME_ROOM_ROWS[i].y + 43, ">");
    }
    display_.button(FOOTER_HOME, "Accueil");
    display_.button(FOOTER_ROOMS, "Pieces");
}

void ControlUi::renderRooms() {
    display_.title("Pieces");
    display_.button(ROOMS_BACK, "< Retour");
    for (size_t i = 0; i < CONTROL_UI_ROOM_COUNT; ++i) {
        const UiRoom &room = rooms_[i];
        display_.rectangle(ROOMS_ROWS[i]);
        display_.text(55, ROOMS_ROWS[i].y + 31, room.name);
        display_.text(55, ROOMS_ROWS[i].y + 59,
                      countText(activeCount(room), room.device_count));
        display_.text(875, ROOMS_ROWS[i].y + 45, ">");
    }
    display_.button(FOOTER_HOME, "Accueil");
    display_.button(FOOTER_ROOMS, "Pieces", true);
}

void ControlUi::renderRoomDetail() {
    const UiRoom &room = rooms_[selectedRoom_];
    display_.title(std::string("< ") + room.name);
    display_.button(DETAIL_BACK, "Retour");
    display_.button(DETAIL_ALL_ON, "Tout allumer");
    display_.button(DETAIL_ALL_OFF, "Tout eteindre");
    for (size_t i = 0; i < room.device_count && i < 3; ++i) {
        const UiDevice &device = room.devices[i];
        const UiRect row{32, DETAIL_DEVICE_TOP + static_cast<int>(i) * DETAIL_DEVICE_HEIGHT,
                         896, DETAIL_DEVICE_HEIGHT - 6};
        display_.rectangle(row);
        display_.text(55, row.y + 34, device.name);
        display_.text(680, row.y + 34,
                      device.command_pending ? "En cours..." :
                      (device.command_failed ? "Echec" : (device.is_on ? "Allume" : "Eteint")));
        display_.text(825, row.y + 34, std::to_string(device.brightness) + "%");
    }
}

bool ControlUi::queueDevice(UiDevice &device, bool on, uint32_t batchId) {
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
    if (!device.matter_bound || !device.supports_onoff) {
        device.command_failed = true;
        ESP_LOGW("CONTROL_MATTER_UI", "unbound_device device=%u", static_cast<unsigned>(device.id));
        dirty_ = true;
        return false;
    }
    if (device.command_pending) return false;
    MatterUiCommand command;
    command.node_id = device.matter_node_id;
    command.endpoint_id = device.matter_endpoint_id;
    command.action = on ? MatterUiAction::SetOn : MatterUiAction::SetOff;
    command.ui_device_id = device.id;
    command.batch_id = batchId;
    const esp_err_t err = matter_controller_ui_enqueue(command);
    if (err != ESP_OK) {
        device.command_failed = true;
        ESP_LOGW("CONTROL_MATTER_UI", "enqueue_failed device=%u endpoint=%u err=%s",
                 static_cast<unsigned>(device.id), static_cast<unsigned>(device.matter_endpoint_id),
                 esp_err_to_name(err));
        dirty_ = true;
        return false;
    }
    device.command_pending = true;
    device.command_failed = false;
    device.pending_batch_id = batchId;
    dirty_ = true;
    return true;
#else
    device.is_on = on;
    dirty_ = true;
    return true;
#endif
}

void ControlUi::queueRoom(UiRoom &room, bool on, uint32_t batchId) {
    size_t queued = 0;
    size_t skipped = 0;
    for (size_t i = 0; i < room.device_count; ++i) {
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
        if (!room.devices[i].matter_bound || !room.devices[i].supports_onoff) {
            ++skipped;
            ESP_LOGI("CONTROL_MATTER_UI", "unbound_device device=%u", static_cast<unsigned>(room.devices[i].id));
            continue;
        }
#endif
        if (queueDevice(room.devices[i], on, batchId)) ++queued;
    }
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
    ESP_LOGI("CONTROL_MATTER_UI", "room_command room=%s target=%s total=%u queued=%u skipped=%u",
             room.name, on ? "on" : "off", static_cast<unsigned>(room.device_count),
             static_cast<unsigned>(queued), static_cast<unsigned>(skipped));
    if (queued == 0) {
        activeBatchId_ = 0;
        return;
    }
    activeBatchExpected_ = queued;
    activeBatchSuccess_ = 0;
    activeBatchFailed_ = 0;
    activeBatchId_ = batchId;
    std::strncpy(activeBatchRoom_, room.name, sizeof(activeBatchRoom_) - 1);
    activeBatchRoom_[sizeof(activeBatchRoom_) - 1] = '\0';
#endif
}

ControlUi::TouchZone ControlUi::zoneAt(int x, int y) const {
    switch (screen_) {
        case ControlUiScreen::HOME:
            if (HOME_ALL_OFF.contains(x, y)) return TouchZone::HOME_ALL_OFF;
            for (size_t i = 0; i < CONTROL_UI_ROOM_COUNT; ++i) {
                if (HOME_ROOM_ROWS[i].contains(x, y)) {
                    return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::HOME_ROOM_0) + i);
                }
            }
            if (FOOTER_HOME.contains(x, y)) return TouchZone::FOOTER_HOME;
            if (FOOTER_ROOMS.contains(x, y)) return TouchZone::FOOTER_ROOMS;
            break;
        case ControlUiScreen::ROOMS:
            if (ROOMS_BACK.contains(x, y)) return TouchZone::ROOMS_BACK;
            for (size_t i = 0; i < CONTROL_UI_ROOM_COUNT; ++i) {
                if (ROOMS_ROWS[i].contains(x, y)) {
                    return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::ROOMS_ROOM_0) + i);
                }
            }
            if (FOOTER_HOME.contains(x, y)) return TouchZone::FOOTER_HOME;
            if (FOOTER_ROOMS.contains(x, y)) return TouchZone::FOOTER_ROOMS;
            break;
        case ControlUiScreen::ROOM_DETAIL:
            if (DETAIL_BACK.contains(x, y)) return TouchZone::DETAIL_BACK;
            if (DETAIL_ALL_ON.contains(x, y)) return TouchZone::DETAIL_ALL_ON;
            if (DETAIL_ALL_OFF.contains(x, y)) return TouchZone::DETAIL_ALL_OFF;
            for (size_t i = 0; i < rooms_[selectedRoom_].device_count && i < 3; ++i) {
                const UiRect row{32, DETAIL_DEVICE_TOP + static_cast<int>(i) * DETAIL_DEVICE_HEIGHT,
                                 896, DETAIL_DEVICE_HEIGHT - 6};
                if (row.contains(x, y)) {
                    return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::DETAIL_DEVICE_0) + i);
                }
            }
            break;
    }
    return TouchZone::NONE;
}

bool ControlUi::handleTouch(const TouchEvent &event) {
    if (event.type == TouchEventType::NONE) return false;
    if (event.type == TouchEventType::PRESS) {
        pressedZone_ = zoneAt(event.x, event.y);
        pressedScreen_ = screen_;
        pressActive_ = pressedZone_ != TouchZone::NONE;
        pressMoved_ = false;
        return true;
    }
    if (event.type == TouchEventType::MOVE) {
        if (pressActive_ && zoneAt(event.x, event.y) != pressedZone_) pressMoved_ = true;
        return true;
    }
    if (event.type == TouchEventType::RELEASE) {
        const bool activateZone = pressActive_ && !pressMoved_ && pressedScreen_ == screen_;
        const TouchZone zone = pressedZone_;
        pressActive_ = false;
        pressedZone_ = TouchZone::NONE;
        if (activateZone) activate(zone);
        return true;
    }
    return false;
}

void ControlUi::activate(TouchZone zone) {
    switch (zone) {
        case TouchZone::HOME_ALL_OFF:
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
            for (size_t i = 0; i < CONTROL_UI_ROOM_COUNT; ++i) {
                const uint32_t batchId = nextBatchId_++;
                queueRoom(rooms_[i], false, batchId);
            }
#else
            for (size_t i = 0; i < CONTROL_UI_ROOM_COUNT; ++i)
                for (size_t j = 0; j < rooms_[i].device_count; ++j) rooms_[i].devices[j].is_on = false;
#endif
            ESP_LOGI(TAG, "touch_action=all_off");
            dirty_ = true;
            break;
        case TouchZone::FOOTER_HOME:
            showHome();
            ESP_LOGI(TAG, "touch_action=home");
            break;
        case TouchZone::FOOTER_ROOMS:
            showRooms();
            ESP_LOGI(TAG, "touch_action=rooms");
            break;
        case TouchZone::HOME_ROOM_0:
        case TouchZone::HOME_ROOM_1:
        case TouchZone::HOME_ROOM_2:
            showRoom(static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::HOME_ROOM_0)));
            ESP_LOGI(TAG, "touch_action=room_detail");
            break;
        case TouchZone::ROOMS_BACK:
            showHome();
            ESP_LOGI(TAG, "touch_action=back");
            break;
        case TouchZone::ROOMS_ROOM_0:
        case TouchZone::ROOMS_ROOM_1:
        case TouchZone::ROOMS_ROOM_2:
            showRoom(static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::ROOMS_ROOM_0)));
            ESP_LOGI(TAG, "touch_action=room_detail");
            break;
        case TouchZone::DETAIL_BACK:
            showRooms();
            ESP_LOGI(TAG, "touch_action=back");
            break;
        case TouchZone::DETAIL_ALL_ON:
        case TouchZone::DETAIL_ALL_OFF: {
            const bool on = zone == TouchZone::DETAIL_ALL_ON;
            UiRoom &room = rooms_[selectedRoom_];
            queueRoom(room, on, nextBatchId_++);
            ESP_LOGI(TAG, "touch_action=room_all state=%s", on ? "on" : "off");
            dirty_ = true;
            break;
        }
        case TouchZone::DETAIL_DEVICE_0:
        case TouchZone::DETAIL_DEVICE_1:
        case TouchZone::DETAIL_DEVICE_2: {
            const size_t index = static_cast<size_t>(static_cast<uint8_t>(zone) -
                                                      static_cast<uint8_t>(TouchZone::DETAIL_DEVICE_0));
            UiRoom &room = rooms_[selectedRoom_];
            if (index < room.device_count) {
                const bool target = !room.devices[index].is_on;
                queueDevice(room.devices[index], target);
                ESP_LOGI(TAG, "device_toggle room=%s device=%s state=%s",
                         room.name, room.devices[index].name,
                         room.devices[index].is_on ? "on" : "off");
                dirty_ = true;
            }
            break;
        }
        case TouchZone::NONE:
        default:
            break;
    }
}
