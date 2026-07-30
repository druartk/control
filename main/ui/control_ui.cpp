#include "ui/control_ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_CONTROL_UI_MOCK_DEVICES
#define CONFIG_CONTROL_UI_MOCK_DEVICES 0
#endif

namespace {
constexpr const char *TAG = "CONTROL_UI";
constexpr const char *MATTER_TAG = "CONTROL_MATTER_UI";
constexpr UiRect HOME_ALL_OFF{32, 96, 896, 70};
constexpr UiRect HOME_SETTINGS{32, 42, 250, 42};
constexpr UiRect HOME_ROOM_ROWS[] = {{32, 180, 896, 70}, {32, 265, 896, 70}, {32, 350, 896, 70}};
constexpr UiRect FOOTER_HOME{32, 480, 430, 45};
constexpr UiRect FOOTER_ROOMS{498, 480, 430, 45};
constexpr UiRect ROOMS_BACK{32, 90, 200, 60};
constexpr UiRect ROOMS_ROWS[] = {
    {32, 155, 896, 52}, {32, 215, 896, 52}, {32, 275, 896, 52},
    {32, 335, 896, 52}, {32, 395, 896, 52}, {32, 455, 896, 40},
};
constexpr UiRect DETAIL_BACK{32, 90, 190, 58};
constexpr UiRect DETAIL_ALL_ON{245, 90, 320, 58};
constexpr UiRect DETAIL_ALL_OFF{590, 90, 338, 58};
constexpr int DETAIL_DEVICE_TOP = 175;
constexpr int DETAIL_DEVICE_HEIGHT = 58;
constexpr UiRect SETTINGS_BACK{32, 90, 190, 58};
constexpr UiRect SETTINGS_ROWS[] = {{32, 175, 896, 58}, {32, 245, 896, 58}, {32, 315, 896, 58}, {32, 385, 896, 58}};
constexpr UiRect SCAN_RESCAN{32, 445, 250, 58};
constexpr UiRect SCAN_CONFIGURE{300, 445, 300, 58};
constexpr UiRect SCAN_BACK{620, 445, 308, 58};
constexpr UiRect SCAN_RESULT_ROWS[] = {{32, 120, 896, 55}, {32, 182, 896, 55}, {32, 244, 896, 55}, {32, 306, 896, 55}, {32, 368, 896, 55}};
constexpr UiRect SCAN_RESULT_NEXT{32, 445, 180, 58};
constexpr UiRect ASSIGN_ROOM_ROWS[] = {{32, 195, 270, 55}, {320, 195, 270, 55}, {620, 195, 308, 55},
                                       {32, 260, 270, 55}, {320, 260, 270, 55}, {620, 260, 308, 55}};
constexpr UiRect ASSIGN_NEW_ROOM{32, 330, 270, 55};
constexpr UiRect ASSIGN_RENAME{32, 395, 270, 40};
constexpr UiRect ASSIGN_SKIP{320, 330, 270, 55};
constexpr UiRect ASSIGN_CONFIRM{620, 330, 308, 55};
constexpr UiRect ASSIGN_BACK{32, 445, 250, 58};
constexpr UiRect CREATE_ROWS[] = {{32, 190, 280, 60}, {340, 190, 280, 60}, {650, 190, 278, 60}};
constexpr UiRect CREATE_BACK{32, 445, 250, 58};
constexpr UiRect RENAME_ROWS[] = {{32, 190, 280, 60}, {340, 190, 280, 60}, {650, 190, 278, 60}};
constexpr UiRect RENAME_BACK{32, 445, 250, 58};
constexpr UiRect SUMMARY_SAVE{620, 445, 308, 58};
constexpr UiRect SUMMARY_BACK{32, 445, 250, 58};
constexpr UiRect CLEAR_YES{32, 300, 420, 65};
constexpr UiRect CLEAR_NO{508, 300, 420, 65};
constexpr const char *ROOM_CHOICES[] = {"Salon", "Cuisine", "Chambre", "Bureau", "Entrée", "Salle de bain"};
constexpr size_t ROOM_CHOICE_COUNT = sizeof(ROOM_CHOICES) / sizeof(ROOM_CHOICES[0]);

const char *screenName(ControlUiScreen screen) {
    switch (screen) {
        case ControlUiScreen::HOME: return "home";
        case ControlUiScreen::ROOMS: return "rooms";
        case ControlUiScreen::ROOM_DETAIL: return "room_detail";
        case ControlUiScreen::SETTINGS: return "settings";
        case ControlUiScreen::DEVICE_SCAN: return "device_scan";
        case ControlUiScreen::DEVICE_SCAN_RESULT: return "device_scan_result";
        case ControlUiScreen::DEVICE_ASSIGN_ROOM: return "device_assign_room";
        case ControlUiScreen::DEVICE_RENAME: return "device_rename";
        case ControlUiScreen::ROOM_CREATE: return "room_create";
        case ControlUiScreen::ONBOARDING_SUMMARY: return "onboarding_summary";
        case ControlUiScreen::CLEAR_CONFIG_CONFIRM: return "clear_config_confirm";
    }
    return "unknown";
}

std::string countText(size_t active, size_t total) {
    return std::to_string(active) + " / " + std::to_string(total);
}

void copyText(char *dst, size_t size, const char *src) {
    if (!dst || size == 0) return;
    std::strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = '\0';
}

const char *deviceState(const UiDevice &device) {
    if (device.command_pending) return "En cours...";
    if (device.command_failed) return "Echec";
    if (!device.state_known) return "Inconnu";
    return device.is_on ? "Allume" : "Eteint";
}
}

void ControlUi::resetRealModel() {
    std::memset(realRooms_, 0, sizeof(realRooms_));
    std::memset(realDevices_, 0, sizeof(realDevices_));
    std::memset(realRoomNames_, 0, sizeof(realRoomNames_));
    std::memset(realDeviceNames_, 0, sizeof(realDeviceNames_));
    roomCount_ = 0;
    realDeviceCount_ = 0;
    missingBinding_ = false;
    rooms_ = nullptr;
}

void ControlUi::init() {
    if (renderMutex_ == nullptr) renderMutex_ = xSemaphoreCreateMutexStatic(&renderMutexStorage_);
    resetRealModel();
#if CONFIG_CONTROL_UI_MOCK_DEVICES
    rooms_ = control_ui_mock_rooms();
    control_ui_mock_reset();
    roomCount_ = CONTROL_UI_ROOM_COUNT;
#endif
    screen_ = ControlUiScreen::HOME;
    selectedRoom_ = 0;
    selectedDevice_ = 0;
    pressedZone_ = TouchZone::NONE;
    pressActive_ = false;
    pressMoved_ = false;
    dirty_ = true;
    ESP_LOGI(TAG, "init mock=%s", CONFIG_CONTROL_UI_MOCK_DEVICES ? "yes" : "no");
}

void ControlUi::setContext(bool wifiConnected, const char *clockText) {
    wifiConnected_ = wifiConnected;
    copyText(clockText_, sizeof(clockText_), clockText ? clockText : "--:--");
}

void ControlUi::bindMatterDevices(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices) {
#if CONFIG_CONTROL_UI_MOCK_DEVICES
    (void)nodeId;
    (void)devices;
#else
    UiConfig empty;
    bindPersistedDevices(nodeId, devices, empty);
#endif
}

void ControlUi::bindPersistedDevices(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices,
                                     const UiConfig &config) {
    nodeId_ = nodeId;
    config_ = config;
    hasConfig_ = config.binding_count != 0;
    rebuildFromConfig(nodeId, devices, config);
    dirty_ = true;
}

void ControlUi::rebuildFromConfig(uint64_t nodeId, const std::vector<MatterDeviceItem> &devices,
                                  const UiConfig &config) {
    resetRealModel();
    nodeId_ = nodeId;
    for (size_t roomIndex = 0; roomIndex < config.room_count && roomIndex < CONTROL_UI_CONFIG_MAX_ROOMS; ++roomIndex) {
        copyText(realRoomNames_[roomCount_], sizeof(realRoomNames_[roomCount_]), config.rooms[roomIndex].name);
        realRooms_[roomCount_] = {config.rooms[roomIndex].room_id, realRoomNames_[roomCount_], nullptr, 0};
        ++roomCount_;
    }
    for (size_t roomIndex = 0; roomIndex < roomCount_; ++roomIndex) {
        UiRoom &room = realRooms_[roomIndex];
        room.devices = &realDevices_[realDeviceCount_];
        for (size_t bindingIndex = 0; bindingIndex < config.binding_count && realDeviceCount_ < CONTROL_UI_CONFIG_MAX_BINDINGS; ++bindingIndex) {
            const UiConfigBinding &binding = config.bindings[bindingIndex];
            if (!binding.configured || binding.node_id != nodeId || binding.room_id != room.id) continue;
            const MatterDeviceItem *found = nullptr;
            for (const MatterDeviceItem &device : devices) {
                if (device.endpoint_id == binding.endpoint_id && device.has_onoff) { found = &device; break; }
            }
            if (!found) continue;
            UiDevice &uiDevice = realDevices_[realDeviceCount_++];
            uiDevice.id = binding.endpoint_id;
            copyText(realDeviceNames_[realDeviceCount_ - 1], sizeof(realDeviceNames_[0]),
                     binding.custom_name[0] ? binding.custom_name : found->name.c_str());
            uiDevice.name = realDeviceNames_[realDeviceCount_ - 1];
            uiDevice.is_on = found->onoff_state_known && found->onoff_state;
            uiDevice.state_known = found->onoff_state_known;
            uiDevice.brightness = 100;
            uiDevice.matter_bound = true;
            uiDevice.matter_node_id = nodeId;
            uiDevice.matter_endpoint_id = found->endpoint_id;
            uiDevice.supports_onoff = found->has_onoff;
            uiDevice.supports_level_control = found->has_level_control;
            ++room.device_count;
            ESP_LOGI(MATTER_TAG, "binding_loaded room=\"%s\" device=\"%s\" node=0x%llx endpoint=%u",
                     room.name, uiDevice.name, static_cast<unsigned long long>(nodeId), found->endpoint_id);
        }
    }
    for (size_t i = 0; i < config.binding_count; ++i) {
        if (config.bindings[i].configured && config.bindings[i].node_id == nodeId) {
            bool loaded = false;
            for (size_t j = 0; j < realDeviceCount_; ++j) if (realDevices_[j].matter_endpoint_id == config.bindings[i].endpoint_id) loaded = true;
            if (!loaded) missingBinding_ = true;
        }
    }
    rooms_ = realRooms_;
}

size_t ControlUi::candidateCount() const {
    size_t count = 0;
    for (size_t i = 0; i < discovery_.endpoint_count; ++i) if (discovery_.devices[i].is_light_candidate) ++count;
    return count;
}

const MatterDiscoveredDevice *ControlUi::candidateAt(size_t index) const {
    for (size_t i = 0; i < discovery_.endpoint_count; ++i) {
        if (!discovery_.devices[i].is_light_candidate) continue;
        if (index-- == 0) return &discovery_.devices[i];
    }
    return nullptr;
}

bool ControlUi::updateDiscovery(const MatterDiscoveryResult &result) {
    const bool changed = discovery_.phase != result.phase || discovery_.endpoint_count != result.endpoint_count ||
                         discovery_.light_count != result.light_count || discovery_.error != result.error;
    discovery_ = result;
    if (!changed) return false;
    dirty_ = true;
    if (result.phase == MatterDiscoveryPhase::COMPLETE && onboarding_) {
        workingConfig_ = config_;
        selectedDevice_ = 0;
        markScreen(ControlUiScreen::DEVICE_SCAN_RESULT);
    } else if (result.phase == MatterDiscoveryPhase::FAILED && onboarding_) {
        markScreen(ControlUiScreen::DEVICE_SCAN);
    }
    return true;
}

void ControlUi::beginOnboarding() {
#if CONFIG_CONTROL_UI_DEVICE_ONBOARDING
    onboarding_ = true;
    workingConfig_ = config_;
    pendingCustomName_[0] = '\0';
    markScreen(ControlUiScreen::DEVICE_SCAN);
    const esp_err_t err = matter_controller_start_device_discovery();
    ESP_LOGI("CONTROL_MATTER_DISCOVERY", "requested result=%s", esp_err_to_name(err));
    if (err != ESP_OK) {
        discovery_.phase = MatterDiscoveryPhase::FAILED;
        discovery_.error = err;
    }
#endif
}

void ControlUi::completeMatterCommand(const MatterUiResult &result) {
    if (!rooms_) return;
    for (size_t roomIndex = 0; roomIndex < roomCount_; ++roomIndex) {
        UiRoom &room = rooms_[roomIndex];
        for (size_t deviceIndex = 0; deviceIndex < room.device_count; ++deviceIndex) {
            UiDevice &device = room.devices[deviceIndex];
            if (device.id != result.command.ui_device_id || device.matter_endpoint_id != result.command.endpoint_id ||
                !device.command_pending) continue;
            device.command_pending = false;
            device.command_failed = !result.success;
            if (result.success) {
                device.is_on = result.command.action == MatterUiAction::SetOn;
                device.state_known = true;
            }
            if (device.pending_batch_id == activeBatchId_ && activeBatchId_ != 0) {
                result.success ? ++activeBatchSuccess_ : ++activeBatchFailed_;
                if (activeBatchSuccess_ + activeBatchFailed_ == activeBatchExpected_) {
                    ESP_LOGI(MATTER_TAG, "room_command_complete room=%s success=%u failed=%u", activeBatchRoom_,
                             static_cast<unsigned>(activeBatchSuccess_), static_cast<unsigned>(activeBatchFailed_));
                    activeBatchId_ = 0;
                }
            }
            dirty_ = true;
            return;
        }
    }
}

void ControlUi::showHome() { markScreen(ControlUiScreen::HOME); ESP_LOGI(TAG, "screen=home"); }
void ControlUi::showRooms() { markScreen(ControlUiScreen::ROOMS); ESP_LOGI(TAG, "screen=rooms"); }
void ControlUi::showRoom(size_t roomIndex) {
    if (!rooms_ || roomIndex >= roomCount_) return;
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
    ESP_LOGI(TAG, "screen=%s", ::screenName(screen_));
}

size_t ControlUi::activeCount(const UiRoom &room) const {
    size_t count = 0;
    for (size_t i = 0; i < room.device_count; ++i) if (room.devices[i].is_on) ++count;
    return count;
}

const char *ControlUi::roomName(size_t roomIndex) const {
    return rooms_ && roomIndex < roomCount_ ? rooms_[roomIndex].name : "";
}

const char *ControlUi::screenName() const { return ::screenName(screen_); }
bool ControlUi::lockRender() { return renderMutex_ && xSemaphoreTake(renderMutex_, pdMS_TO_TICKS(1000)) == pdTRUE; }
void ControlUi::unlockRender() { if (renderMutex_) xSemaphoreGive(renderMutex_); }

bool ControlUi::render(bool forceFullRefresh) {
    if (!dirty_ && !forceFullRefresh) return true;
    if (!display_.available() || !lockRender()) return false;
    const int64_t started = esp_timer_get_time();
    ESP_LOGI(TAG, "render_start screen=%s", screenName());
    display_.beginFrame();
    switch (screen_) {
        case ControlUiScreen::HOME: renderHome(); break;
        case ControlUiScreen::ROOMS: renderRooms(); break;
        case ControlUiScreen::ROOM_DETAIL: renderRoomDetail(); break;
        case ControlUiScreen::SETTINGS: renderSettings(); break;
        case ControlUiScreen::DEVICE_SCAN: renderDeviceScan(); break;
        case ControlUiScreen::DEVICE_SCAN_RESULT: renderScanResult(); break;
        case ControlUiScreen::DEVICE_ASSIGN_ROOM: renderAssignRoom(); break;
        case ControlUiScreen::DEVICE_RENAME: renderRenameDevice(); break;
        case ControlUiScreen::ROOM_CREATE: renderRoomCreate(); break;
        case ControlUiScreen::ONBOARDING_SUMMARY: renderSummary(); break;
        case ControlUiScreen::CLEAR_CONFIG_CONFIRM: renderClearConfirm(); break;
    }
    const bool committed = display_.commitFull(screenName());
    if (committed) { dirty_ = false; ESP_LOGI(TAG, "render_done screen=%s duration_ms=%u", screenName(),
                                               static_cast<unsigned>((esp_timer_get_time() - started) / 1000)); }
    unlockRender();
    return committed;
}

void ControlUi::renderHome() {
    display_.title("Maison");
    display_.text(680, 45, clockText_);
    display_.text(790, 45, wifiConnected_ ? "Wi-Fi OK" : "Wi-Fi --");
    display_.button(HOME_SETTINGS, "Configurer");
    display_.button(HOME_ALL_OFF, "Tout eteindre", true);
    if (!rooms_ || roomCount_ == 0 || missingBinding_) display_.text(55, 235, "Configuration requise");
    for (size_t i = 0; rooms_ && i < roomCount_ && i < 3; ++i) {
        const UiRoom &room = rooms_[i]; display_.rectangle(HOME_ROOM_ROWS[i]);
        display_.text(55, HOME_ROOM_ROWS[i].y + 29, room.name);
        display_.text(55, HOME_ROOM_ROWS[i].y + 55, countText(activeCount(room), room.device_count));
        display_.text(875, HOME_ROOM_ROWS[i].y + 43, ">");
    }
    display_.button(FOOTER_HOME, "Accueil"); display_.button(FOOTER_ROOMS, "Pieces");
}

void ControlUi::renderRooms() {
    display_.title("Pieces"); display_.button(ROOMS_BACK, "< Retour");
    for (size_t i = 0; rooms_ && i < roomCount_ && i < 6; ++i) {
        const UiRoom &room = rooms_[i]; display_.rectangle(ROOMS_ROWS[i]);
        display_.text(55, ROOMS_ROWS[i].y + 25, room.name);
        display_.text(650, ROOMS_ROWS[i].y + 25, countText(activeCount(room), room.device_count));
        display_.text(875, ROOMS_ROWS[i].y + 32, ">");
    }
    display_.button(FOOTER_HOME, "Accueil"); display_.button(FOOTER_ROOMS, "Pieces", true);
}

void ControlUi::renderRoomDetail() {
    if (!rooms_ || selectedRoom_ >= roomCount_) return;
    const UiRoom &room = rooms_[selectedRoom_]; display_.title(std::string("< ") + room.name);
    display_.button(DETAIL_BACK, "Retour"); display_.button(DETAIL_ALL_ON, "Tout allumer"); display_.button(DETAIL_ALL_OFF, "Tout eteindre");
    for (size_t i = 0; i < room.device_count && i < 5; ++i) {
        const UiDevice &device = room.devices[i]; const UiRect row{32, DETAIL_DEVICE_TOP + static_cast<int>(i) * DETAIL_DEVICE_HEIGHT, 896, DETAIL_DEVICE_HEIGHT - 6};
        display_.rectangle(row); display_.text(55, row.y + 34, device.name); display_.text(680, row.y + 34, deviceState(device));
        display_.text(825, row.y + 34, device.supports_level_control ? std::to_string(device.brightness) + "%" : "On/Off");
    }
}

void ControlUi::renderSettings() {
    display_.title("Configuration"); display_.button(SETTINGS_BACK, "< Retour");
    display_.button(SETTINGS_ROWS[0], "Configurer les appareils"); display_.button(SETTINGS_ROWS[1], "Rescanner les appareils");
    display_.button(SETTINGS_ROWS[2], "Reaffecter les pieces"); display_.button(SETTINGS_ROWS[3], "Effacer configuration UI");
}

void ControlUi::renderDeviceScan() {
    display_.title("Recherche des appareils");
    display_.text(55, 175, discovery_.phase == MatterDiscoveryPhase::FAILED ? "Echec de la recherche" : "Lecture des endpoints Matter...");
    display_.text(55, 225, std::string("Endpoints: ") + std::to_string(discovery_.endpoint_count));
    display_.text(55, 265, discovery_.error == ESP_OK ? "Veuillez patienter" : esp_err_to_name(discovery_.error));
    display_.button(SCAN_RESCAN, "Rescanner"); display_.button(SCAN_CONFIGURE, "Resultats"); display_.button(SCAN_BACK, "Retour");
}

void ControlUi::renderScanResult() {
    display_.title("Appareils détectés");
    if (discovery_.endpoint_count == 0 || candidateCount() == 0) display_.text(55, 180, "Aucune lumière détectée");
    for (size_t i = 0; i < discovery_.endpoint_count && i < 5; ++i) {
        const MatterDiscoveredDevice *device = &discovery_.devices[i]; const UiRect &row = SCAN_RESULT_ROWS[i];
        display_.rectangle(row); display_.text(55, row.y + 34, device->display_name);
        display_.text(600, row.y + 34, std::string("Endpoint ") + std::to_string(device->endpoint_id));
        const char *assignment = device->is_light_candidate ? "Non configurée" : "Ignorée";
        for (size_t bindingIndex = 0; bindingIndex < workingConfig_.binding_count; ++bindingIndex) {
            const UiConfigBinding &binding = workingConfig_.bindings[bindingIndex];
            if (binding.configured && binding.node_id == device->node_id && binding.endpoint_id == device->endpoint_id) {
                for (size_t roomIndex = 0; roomIndex < workingConfig_.room_count; ++roomIndex) {
                    if (workingConfig_.rooms[roomIndex].room_id == binding.room_id) assignment = workingConfig_.rooms[roomIndex].name;
                }
            }
        }
        display_.text(805, row.y + 34, assignment);
    }
    display_.button(SCAN_RESULT_NEXT, "Configurer"); display_.button(SCAN_RESCAN, "Rescanner"); display_.button(SCAN_BACK, "Retour");
}

void ControlUi::renderAssignRoom() {
    const MatterDiscoveredDevice *device = candidateAt(selectedDevice_);
    display_.title("Affecter une piece");
    if (device) {
        display_.text(55, 140, device->display_name);
        display_.text(600, 140, std::string("Endpoint ") + std::to_string(device->endpoint_id));
        display_.text(55, 170, device->has_level_control ? "On/Off + luminosite" : "On/Off");
    }
    for (size_t i = 0; i < ROOM_CHOICE_COUNT; ++i) display_.button(ASSIGN_ROOM_ROWS[i], ROOM_CHOICES[i], selectedRoomChoice_ == i);
    display_.button(ASSIGN_NEW_ROOM, "Nouvelle pièce"); display_.button(ASSIGN_RENAME, "Renommer"); display_.button(ASSIGN_SKIP, "Non assigné"); display_.button(ASSIGN_CONFIRM, "Confirmer", true); display_.button(ASSIGN_BACK, "Retour");
}

void ControlUi::renderRenameDevice() {
    display_.title("Nom de l'appareil");
    display_.button(RENAME_ROWS[0], "Lampe 1"); display_.button(RENAME_ROWS[1], "Lampe 2"); display_.button(RENAME_ROWS[2], "Plafonnier");
    display_.button(RENAME_BACK, "Retour");
}

void ControlUi::renderRoomCreate() {
    display_.title("Nouvelle piece");
    display_.button(CREATE_ROWS[0], "Piece 1"); display_.button(CREATE_ROWS[1], "Piece 2"); display_.button(CREATE_ROWS[2], "Piece 3"); display_.button(CREATE_BACK, "Retour");
}

void ControlUi::renderSummary() {
    size_t configured = 0; for (size_t i = 0; i < workingConfig_.binding_count; ++i) if (workingConfig_.bindings[i].configured) ++configured;
    display_.title("Configuration terminee"); display_.text(55, 180, std::string("Appareils affectes: ") + std::to_string(configured));
    display_.text(55, 230, "Enregistrer les affectations ?"); display_.button(SUMMARY_BACK, "Retour"); display_.button(SUMMARY_SAVE, "Enregistrer", true);
}

void ControlUi::renderClearConfirm() {
    display_.title("Effacer la configuration"); display_.text(55, 200, "Les affectations UI seront supprimees.");
    display_.button(CLEAR_YES, "Confirmer", true); display_.button(CLEAR_NO, "Annuler");
}

bool ControlUi::queueDevice(UiDevice &device, bool on, uint32_t batchId) {
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
    if (!device.matter_bound || !device.supports_onoff) { device.command_failed = true; ESP_LOGW(MATTER_TAG, "unbound_device device=%u", device.id); dirty_ = true; return false; }
    if (!device.state_known) { device.command_failed = true; ESP_LOGW(MATTER_TAG, "state_unknown device=%u endpoint=%u", device.id, device.matter_endpoint_id); dirty_ = true; return false; }
    if (device.command_pending) return false;
    MatterUiCommand command{device.matter_node_id, device.matter_endpoint_id, on ? MatterUiAction::SetOn : MatterUiAction::SetOff, device.id, batchId};
    const esp_err_t err = matter_controller_ui_enqueue(command);
    if (err != ESP_OK) { device.command_failed = true; ESP_LOGW(MATTER_TAG, "enqueue_failed device=%u endpoint=%u err=%s", device.id, device.matter_endpoint_id, esp_err_to_name(err)); dirty_ = true; return false; }
    device.command_pending = true; device.command_failed = false; device.pending_batch_id = batchId; dirty_ = true; return true;
#else
    device.is_on = on; dirty_ = true; return true;
#endif
}

void ControlUi::queueRoom(UiRoom &room, bool on, uint32_t batchId) {
    size_t queued = 0, skipped = 0;
    for (size_t i = 0; i < room.device_count; ++i) {
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
        if (!room.devices[i].matter_bound || !room.devices[i].supports_onoff) { ++skipped; continue; }
#endif
        if (queueDevice(room.devices[i], on, batchId)) ++queued;
    }
#if CONFIG_CONTROL_UI_MATTER_COMMANDS
    ESP_LOGI(MATTER_TAG, "room_command room=%s target=%s total=%u queued=%u skipped=%u", room.name, on ? "on" : "off", room.device_count, queued, skipped);
    if (!queued) { activeBatchId_ = 0; return; }
    activeBatchExpected_ = queued; activeBatchSuccess_ = 0; activeBatchFailed_ = 0; activeBatchId_ = batchId; copyText(activeBatchRoom_, sizeof(activeBatchRoom_), room.name);
#endif
}

void ControlUi::chooseRoom(size_t roomIndex) {
    const MatterDiscoveredDevice *device = candidateAt(selectedDevice_);
    if (!device || workingConfig_.binding_count >= CONTROL_UI_CONFIG_MAX_BINDINGS) return;
    uint32_t roomId = 0;
    if (roomIndex < ROOM_CHOICE_COUNT) {
        for (size_t i = 0; i < workingConfig_.room_count; ++i) if (std::strcmp(workingConfig_.rooms[i].name, ROOM_CHOICES[roomIndex]) == 0) roomId = workingConfig_.rooms[i].room_id;
        if (roomId == 0 && workingConfig_.room_count < CONTROL_UI_CONFIG_MAX_ROOMS) {
            roomId = static_cast<uint32_t>(workingConfig_.room_count + 1); workingConfig_.rooms[workingConfig_.room_count] = {roomId, {}, static_cast<uint8_t>(workingConfig_.room_count)}; copyText(workingConfig_.rooms[workingConfig_.room_count].name, 32, ROOM_CHOICES[roomIndex]); ++workingConfig_.room_count;
        }
    } else if (roomIndex < ROOM_CHOICE_COUNT + 3) {
        const size_t generic = roomIndex - ROOM_CHOICE_COUNT;
        if (workingConfig_.room_count < CONTROL_UI_CONFIG_MAX_ROOMS) {
            roomId = static_cast<uint32_t>(workingConfig_.room_count + 1); workingConfig_.rooms[workingConfig_.room_count] = {roomId, {}, static_cast<uint8_t>(workingConfig_.room_count)}; char name[32]; std::snprintf(name, sizeof(name), "Pièce %u", static_cast<unsigned>(generic + 1)); copyText(workingConfig_.rooms[workingConfig_.room_count].name, 32, name); ++workingConfig_.room_count;
        }
    }
    for (size_t i = 0; i < workingConfig_.binding_count; ++i) {
        if (workingConfig_.bindings[i].node_id == device->node_id && workingConfig_.bindings[i].endpoint_id == device->endpoint_id) {
            workingConfig_.bindings[i].room_id = roomId;
            workingConfig_.bindings[i].configured = roomId != 0;
            if (pendingCustomName_[0] != '\0') copyText(workingConfig_.bindings[i].custom_name, sizeof(workingConfig_.bindings[i].custom_name), pendingCustomName_);
            return;
        }
    }
    UiConfigBinding &binding = workingConfig_.bindings[workingConfig_.binding_count++];
    binding.node_id = device->node_id;
    binding.endpoint_id = device->endpoint_id;
    binding.room_id = roomId;
    binding.configured = roomId != 0;
    binding.order = static_cast<uint8_t>(workingConfig_.binding_count - 1);
    copyText(binding.custom_name, sizeof(binding.custom_name), pendingCustomName_[0] ? pendingCustomName_ : device->display_name);
}

void ControlUi::saveOnboarding() {
    if (control_ui_config_save(workingConfig_) != ESP_OK) return;
    config_ = workingConfig_;
    hasConfig_ = config_.binding_count != 0;
    std::vector<MatterDeviceItem> devices;
    devices.reserve(discovery_.endpoint_count);
    for (size_t i = 0; i < discovery_.endpoint_count; ++i) {
        const MatterDiscoveredDevice &source = discovery_.devices[i];
        MatterDeviceItem device;
        device.endpoint_id = source.endpoint_id;
        device.name = source.display_name;
        device.has_onoff = source.has_onoff;
        device.has_level_control = source.has_level_control;
        device.reachable_known = source.reachable_known;
        device.reachable = source.reachable;
        device.onoff_state_known = source.onoff_state_known;
        device.onoff_state = source.onoff_state;
        device.device_type = source.primary_device_type;
        devices.push_back(device);
    }
    rebuildFromConfig(nodeId_, devices, config_);
    onboarding_ = false;
    markScreen(ControlUiScreen::HOME);
    dirty_ = true;
}

ControlUi::TouchZone ControlUi::zoneAt(int x, int y) const {
    switch (screen_) {
        case ControlUiScreen::HOME:
            if (HOME_SETTINGS.contains(x, y)) return TouchZone::HOME_SETTINGS;
            if (HOME_ALL_OFF.contains(x, y)) return TouchZone::HOME_ALL_OFF;
            for (size_t i = 0; i < roomCount_ && i < 3; ++i) {
                if (HOME_ROOM_ROWS[i].contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::HOME_ROOM_0) + i);
            }
            if (FOOTER_HOME.contains(x, y)) return TouchZone::FOOTER_HOME;
            if (FOOTER_ROOMS.contains(x, y)) return TouchZone::FOOTER_ROOMS;
            break;
        case ControlUiScreen::ROOMS:
            if (ROOMS_BACK.contains(x, y)) return TouchZone::ROOMS_BACK;
            for (size_t i = 0; i < roomCount_ && i < 6; ++i) {
                if (ROOMS_ROWS[i].contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::ROOMS_ROOM_0) + i);
            }
            if (FOOTER_HOME.contains(x, y)) return TouchZone::FOOTER_HOME;
            if (FOOTER_ROOMS.contains(x, y)) return TouchZone::FOOTER_ROOMS;
            break;
        case ControlUiScreen::ROOM_DETAIL:
            if (DETAIL_BACK.contains(x, y)) return TouchZone::DETAIL_BACK;
            if (DETAIL_ALL_ON.contains(x, y)) return TouchZone::DETAIL_ALL_ON;
            if (DETAIL_ALL_OFF.contains(x, y)) return TouchZone::DETAIL_ALL_OFF;
            if (rooms_ && selectedRoom_ < roomCount_) {
                for (size_t i = 0; i < rooms_[selectedRoom_].device_count && i < 5; ++i) {
                    UiRect row{32, DETAIL_DEVICE_TOP + static_cast<int>(i) * DETAIL_DEVICE_HEIGHT, 896, DETAIL_DEVICE_HEIGHT - 6};
                    if (row.contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::DETAIL_DEVICE_0) + i);
                }
            }
            break;
        case ControlUiScreen::SETTINGS:
            if (SETTINGS_BACK.contains(x, y)) return TouchZone::SETTINGS_BACK;
            for (size_t i = 0; i < 4; ++i) {
                if (SETTINGS_ROWS[i].contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::SETTINGS_CONFIGURE) + i);
            }
            break;
        case ControlUiScreen::DEVICE_SCAN:
            if (SCAN_RESCAN.contains(x, y)) return TouchZone::SCAN_RESCAN;
            if (SCAN_CONFIGURE.contains(x, y)) return TouchZone::SCAN_CONFIGURE;
            if (SCAN_BACK.contains(x, y)) return TouchZone::SCAN_BACK;
            break;
        case ControlUiScreen::DEVICE_SCAN_RESULT:
            if (SCAN_RESULT_NEXT.contains(x, y)) return TouchZone::SCAN_NEXT;
            if (SCAN_RESCAN.contains(x, y)) return TouchZone::SCAN_RESCAN;
            if (SCAN_BACK.contains(x, y)) return TouchZone::SCAN_BACK;
            break;
        case ControlUiScreen::DEVICE_ASSIGN_ROOM:
            for (size_t i = 0; i < ROOM_CHOICE_COUNT; ++i) {
                if (ASSIGN_ROOM_ROWS[i].contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::ASSIGN_ROOM_0) + i);
            }
            if (ASSIGN_NEW_ROOM.contains(x, y)) return TouchZone::ASSIGN_NEW_ROOM;
            if (ASSIGN_RENAME.contains(x, y)) return TouchZone::ASSIGN_RENAME;
            if (ASSIGN_SKIP.contains(x, y)) return TouchZone::ASSIGN_SKIP;
            if (ASSIGN_CONFIRM.contains(x, y)) return TouchZone::ASSIGN_CONFIRM;
            if (ASSIGN_BACK.contains(x, y)) return TouchZone::SCAN_BACK;
            break;
        case ControlUiScreen::DEVICE_RENAME:
            for (size_t i = 0; i < 3; ++i) {
                if (RENAME_ROWS[i].contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::RENAME_0) + i);
            }
            if (RENAME_BACK.contains(x, y)) return TouchZone::RENAME_BACK;
            break;
        case ControlUiScreen::ROOM_CREATE:
            for (size_t i = 0; i < 3; ++i) {
                if (CREATE_ROWS[i].contains(x, y)) return static_cast<TouchZone>(static_cast<uint8_t>(TouchZone::ROOM_CREATE_0) + i);
            }
            if (CREATE_BACK.contains(x, y)) return TouchZone::ROOM_CREATE_BACK;
            break;
        case ControlUiScreen::ONBOARDING_SUMMARY:
            if (SUMMARY_SAVE.contains(x, y)) return TouchZone::SUMMARY_SAVE;
            if (SUMMARY_BACK.contains(x, y)) return TouchZone::SUMMARY_BACK;
            break;
        case ControlUiScreen::CLEAR_CONFIG_CONFIRM:
            if (CLEAR_YES.contains(x, y)) return TouchZone::CLEAR_CONFIRM_YES;
            if (CLEAR_NO.contains(x, y)) return TouchZone::CLEAR_CONFIRM_NO;
            break;
    }
    return TouchZone::NONE;
}

bool ControlUi::handleTouch(const TouchEvent &event) {
    if (event.type == TouchEventType::NONE) return false;
    if (event.type == TouchEventType::PRESS) { pressedZone_ = zoneAt(event.x, event.y); pressedScreen_ = screen_; pressActive_ = pressedZone_ != TouchZone::NONE; pressMoved_ = false; return true; }
    if (event.type == TouchEventType::MOVE) { if (pressActive_ && zoneAt(event.x, event.y) != pressedZone_) pressMoved_ = true; return true; }
    if (event.type == TouchEventType::RELEASE) { const bool activateZone = pressActive_ && !pressMoved_ && pressedScreen_ == screen_; const TouchZone zone = pressedZone_; pressActive_ = false; pressedZone_ = TouchZone::NONE; if (activateZone) activate(zone); return true; }
    return false;
}

void ControlUi::activate(TouchZone zone) {
    switch (zone) {
        case TouchZone::HOME_SETTINGS: markScreen(ControlUiScreen::SETTINGS); break;
        case TouchZone::HOME_ALL_OFF: if (rooms_) for (size_t i = 0; i < roomCount_; ++i) queueRoom(rooms_[i], false, nextBatchId_++); ESP_LOGI(TAG, "touch_action=all_off"); break;
        case TouchZone::FOOTER_HOME: showHome(); break;
        case TouchZone::FOOTER_ROOMS: showRooms(); break;
        case TouchZone::HOME_ROOM_0: case TouchZone::HOME_ROOM_1: case TouchZone::HOME_ROOM_2: showRoom(static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::HOME_ROOM_0))); break;
        case TouchZone::ROOMS_BACK: showHome(); break;
        case TouchZone::ROOMS_ROOM_0: case TouchZone::ROOMS_ROOM_1: case TouchZone::ROOMS_ROOM_2: case TouchZone::ROOMS_ROOM_3: case TouchZone::ROOMS_ROOM_4: case TouchZone::ROOMS_ROOM_5: showRoom(static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::ROOMS_ROOM_0))); break;
        case TouchZone::DETAIL_BACK: showRooms(); break;
        case TouchZone::DETAIL_ALL_ON: if (rooms_ && selectedRoom_ < roomCount_) queueRoom(rooms_[selectedRoom_], true, nextBatchId_++); break;
        case TouchZone::DETAIL_ALL_OFF: if (rooms_ && selectedRoom_ < roomCount_) queueRoom(rooms_[selectedRoom_], false, nextBatchId_++); break;
        case TouchZone::DETAIL_DEVICE_0: case TouchZone::DETAIL_DEVICE_1: case TouchZone::DETAIL_DEVICE_2: case TouchZone::DETAIL_DEVICE_3: case TouchZone::DETAIL_DEVICE_4: { size_t i = static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::DETAIL_DEVICE_0)); if (rooms_ && selectedRoom_ < roomCount_ && i < rooms_[selectedRoom_].device_count) { UiDevice &device = rooms_[selectedRoom_].devices[i]; const bool target = !device.is_on; queueDevice(device, target); ESP_LOGI(TAG, "device_toggle room=%s device=%s state=%s", rooms_[selectedRoom_].name, device.name, target ? "on" : "off"); } break; }
        case TouchZone::SETTINGS_BACK: showHome(); break;
        case TouchZone::SETTINGS_CONFIGURE: beginOnboarding(); break;
        case TouchZone::SETTINGS_RESCAN: beginOnboarding(); break;
        case TouchZone::SETTINGS_REASSIGN: beginOnboarding(); break;
        case TouchZone::SETTINGS_CLEAR: markScreen(ControlUiScreen::CLEAR_CONFIG_CONFIRM); break;
        case TouchZone::CLEAR_CONFIRM_YES: if (control_ui_config_clear() == ESP_OK) { config_ = {}; hasConfig_ = false; resetRealModel(); beginOnboarding(); } break;
        case TouchZone::CLEAR_CONFIRM_NO: markScreen(ControlUiScreen::SETTINGS); break;
        case TouchZone::SCAN_RESCAN: beginOnboarding(); break;
        case TouchZone::SCAN_CONFIGURE: if (candidateCount()) { selectedDevice_ = 0; markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM); } break;
        case TouchZone::SCAN_BACK: markScreen(ControlUiScreen::SETTINGS); break;
        case TouchZone::SCAN_NEXT: if (candidateCount()) { selectedDevice_ = 0; markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM); } break;
        case TouchZone::ASSIGN_ROOM_0: case TouchZone::ASSIGN_ROOM_1: case TouchZone::ASSIGN_ROOM_2: case TouchZone::ASSIGN_ROOM_3: case TouchZone::ASSIGN_ROOM_4: case TouchZone::ASSIGN_ROOM_5: selectedRoomChoice_ = static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::ASSIGN_ROOM_0)); dirty_ = true; break;
        case TouchZone::ASSIGN_NEW_ROOM: markScreen(ControlUiScreen::ROOM_CREATE); break;
        case TouchZone::ASSIGN_RENAME: markScreen(ControlUiScreen::DEVICE_RENAME); break;
        case TouchZone::RENAME_0: case TouchZone::RENAME_1: case TouchZone::RENAME_2: {
            const char *names[] = {"Lampe 1", "Lampe 2", "Plafonnier"};
            copyText(pendingCustomName_, sizeof(pendingCustomName_), names[static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::RENAME_0))]);
            markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM);
            break;
        }
        case TouchZone::RENAME_BACK: markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM); break;
        case TouchZone::ROOM_CREATE_0: case TouchZone::ROOM_CREATE_1: case TouchZone::ROOM_CREATE_2: selectedRoomChoice_ = ROOM_CHOICE_COUNT + static_cast<size_t>(static_cast<uint8_t>(zone) - static_cast<uint8_t>(TouchZone::ROOM_CREATE_0)); chooseRoom(selectedRoomChoice_); markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM); break;
        case TouchZone::ROOM_CREATE_BACK: markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM); break;
        case TouchZone::ASSIGN_SKIP: chooseRoom(ROOM_CHOICE_COUNT + 3); [[fallthrough]];
        case TouchZone::ASSIGN_CONFIRM: if (zone == TouchZone::ASSIGN_CONFIRM) chooseRoom(selectedRoomChoice_); if (selectedDevice_ + 1 < candidateCount()) { ++selectedDevice_; markScreen(ControlUiScreen::DEVICE_ASSIGN_ROOM); } else markScreen(ControlUiScreen::ONBOARDING_SUMMARY); break;
        case TouchZone::SUMMARY_SAVE: saveOnboarding(); break;
        case TouchZone::SUMMARY_BACK: markScreen(ControlUiScreen::DEVICE_SCAN_RESULT); break;
        case TouchZone::NONE: default: break;
    }
    dirty_ = true;
}
