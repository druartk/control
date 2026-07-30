#pragma once

#include <cstddef>
#include <cstdint>

struct UiDevice {
    uint32_t id;
    const char *name;
    bool is_on;
    uint8_t brightness;
    bool state_known = false;
    bool matter_bound = false;
    uint64_t matter_node_id = 0;
    uint16_t matter_endpoint_id = 0;
    bool supports_onoff = false;
    bool supports_level_control = false;
    bool command_pending = false;
    bool command_failed = false;
    uint32_t pending_batch_id = 0;
};

struct UiRoom {
    uint32_t id;
    const char *name;
    UiDevice *devices;
    size_t device_count;
};

constexpr size_t CONTROL_UI_ROOM_COUNT = 3;

UiRoom *control_ui_mock_rooms();
void control_ui_mock_reset();
