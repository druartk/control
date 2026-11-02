#!/usr/bin/env python3
"""Exercise production room rebinding after remote configuration changes."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
ui = (root / 'main/ui/control_ui.cpp').read_text()
header = (root / 'main/ui/control_ui.h').read_text()
config = (root / 'main/storage/ui_config.h').read_text()
model = (root / 'main/ui/control_ui_model.h').read_text()
types = config[config.index('constexpr uint16_t'):config.index('esp_err_t control_ui_config_load')]
types += model[model.index('struct UiDevice'):model.index('constexpr size_t CONTROL_UI_ROOM_COUNT')]
types += re.search(r'enum class ControlUiScreen \{.*?\};', header, re.S)[0]
types += re.search(r'constexpr size_t DETAIL_DEVICE_PAGE_SIZE = [^;]+;', ui)[0]
reset = ui[ui.index('void ControlUi::resetRealModel('):ui.index('void ControlUi::init(')]
bind = ui[ui.index('void ControlUi::bindPersistedDevices('):ui.index('size_t ControlUi::candidateCount(')]
source = r'''
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#define ESP_LOGI(...) ((void)0)
''' + types + r'''
struct UiRect { int width=0; };
struct MatterDeviceItem {
    uint16_t endpoint_id=0;
    std::string name;
    bool onoff_state_known=false,onoff_state=false,has_onoff=true,has_level_control=false;
    bool reachable_known=false,reachable=false;
};
void copyText(char* target,size_t size,const char* source) { std::strncpy(target,source,size-1); target[size-1]=0; }
struct ControlUi {
    enum class TouchZone { NONE, ROOM };
    UiRect dirtyRegion_;
    UiRoom realRooms_[16]{}; UiDevice realDevices_[32]{};
    char realRoomNames_[16][32]{},realDeviceNames_[32][48]{};
    size_t roomCount_=0,realDeviceCount_=0,selectedRoom_=0,roomDevicePage_=0;
    bool missingBinding_=false,hasConfig_=false,dirty_=false,pressActive_=false;
    UiRoom* rooms_=nullptr;
    uint64_t nodeId_=0;
    UiConfig config_;
    ControlUiScreen screen_=ControlUiScreen::HOME;
    TouchZone pressedZone_=TouchZone::NONE;
    void markScreen(ControlUiScreen screen) { screen_=screen; }
    void resetRealModel();
    void bindPersistedDevices(uint64_t,const std::vector<MatterDeviceItem>&,const UiConfig&);
    void rebuildFromConfig(uint64_t,const std::vector<MatterDeviceItem>&,const UiConfig&);
};
''' + reset + bind + r'''
int main() {
    ControlUi ui;
    UiConfig config;
    config.room_count=2;
    config.rooms[0].room_id=10; strcpy(config.rooms[0].name,"Kitchen");
    config.rooms[1].room_id=20; strcpy(config.rooms[1].name,"Living room");
    ui.bindPersistedDevices(123,{},config);
    ui.screen_=ControlUiScreen::ROOM_DETAIL; ui.selectedRoom_=1;
    std::swap(config.rooms[0],config.rooms[1]);
    ui.pressActive_=true; ui.pressedZone_=ControlUi::TouchZone::ROOM;
    ui.bindPersistedDevices(123,{},config);
    assert(ui.screen_==ControlUiScreen::ROOM_DETAIL && ui.selectedRoom_==0);
    assert(ui.rooms_[ui.selectedRoom_].id==20 && !ui.pressActive_);
    assert(ui.pressedZone_==ControlUi::TouchZone::NONE && ui.dirty_);
    config.rooms[0]=config.rooms[1]; config.room_count=1;
    ui.bindPersistedDevices(123,{},config);
    assert(ui.screen_==ControlUiScreen::ROOMS && ui.selectedRoom_==0);
    ui.screen_=ControlUiScreen::ROOM_DETAIL;
    ui.bindPersistedDevices(123,{},UiConfig{});
    assert(ui.screen_==ControlUiScreen::ROOMS && ui.roomCount_==0);
    // Offline bindings still produce tiles when no Matter cache is available.
    config.binding_count=1;
    config.bindings[0].node_id=123; config.bindings[0].endpoint_id=7;
    config.bindings[0].room_id=10; config.bindings[0].configured=true;
    ui.bindPersistedDevices(123,{},config);
    assert(ui.realDeviceCount_==1 && ui.realRooms_[0].device_count==1);
    assert(!ui.realDevices_[0].matter_bound && !ui.realDevices_[0].state_known);
    config.binding_count=9;
    for(size_t i=0;i<9;++i) {
        config.bindings[i]=config.bindings[0]; config.bindings[i].endpoint_id=i+1;
    }
    ui.bindPersistedDevices(123,{},config);
    ui.screen_=ControlUiScreen::ROOM_DETAIL; ui.roomDevicePage_=1;
    ui.bindPersistedDevices(123,{},config);
    assert(ui.roomDevicePage_==1 && ui.realDeviceCount_==9);
    config.binding_count=3;
    ui.bindPersistedDevices(123,{},config);
    assert(ui.roomDevicePage_==0 && ui.realDeviceCount_==3);
}
'''
with tempfile.TemporaryDirectory(prefix='control-config-ui-') as directory:
    cpp = Path(directory) / 'test.cpp'
    exe = Path(directory) / 'test'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-class-memaccess', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: remote room removal, reorder, reset, stale touch cancellation and offline bindings.')
