#!/usr/bin/env python3
"""Check production room pages, touch targets and command routing without hardware."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
ui = (root / 'main/ui/control_ui.cpp').read_text()
header = (root / 'main/ui/control_ui.h').read_text()
screens = re.search(r'enum class ControlUiScreen \{.*?\};', header, re.S)[0]
zones = re.search(r'enum class TouchZone : uint8_t \{.*?\};', header, re.S)[0]
layout = '\n'.join(re.findall(r'constexpr (?:UiRect|size_t) DETAIL_[^;]+;', ui, re.S))
render = ui[ui.index('void ControlUi::renderRoomDetail()'):ui.index('void ControlUi::renderSettings()')]
zone = ui[ui.index('ControlUi::TouchZone ControlUi::zoneAt('):ui.index('bool ControlUi::handleTouch(')]
zone = zone[zone.index('        case ControlUiScreen::ROOM_DETAIL:'):zone.index('        case ControlUiScreen::SETTINGS:')]
actions = ui[ui.index('void ControlUi::activate('):]
actions = actions[actions.index('        case TouchZone::DETAIL_BACK:'):actions.index('        case TouchZone::SETTINGS_BACK:')]
mark = ui[ui.index('void ControlUi::markDeviceDirty('):ui.index('bool ControlUi::queueDevice(')]
mark = mark[:mark.index('    } else if (rooms_ && screen_ == ControlUiScreen::ROOMS)')] + '\n    }\n    dirtyRegion_=tile; dirty_=true;\n}\n'
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include "main/ui/control_ui_model.h"
#define ESP_LOGI(...) ((void)0)
struct UiRect {
    int x=0,y=0,width=0,height=0;
    bool contains(int px,int py) const { return px>=x && py>=y && px<x+width && py<y+height; }
};
struct Button { UiRect rect; std::string label; };
struct DisplayManager {
    std::vector<Button> buttons;
    std::string page;
    void title(const std::string&,int) {}
    void button(UiRect rect,const std::string& label,bool=false) { buttons.push_back({rect,label}); }
    void textFitted(int,int,const std::string& text,int) { page=text; }
} display;
''' + screens + layout + r'''
struct ControlUi {
''' + zones + r'''
    UiRoom* rooms_=nullptr;
    size_t roomCount_=1,selectedRoom_=0,roomDevicePage_=0;
    ControlUiScreen screen_=ControlUiScreen::ROOM_DETAIL;
    DisplayManager& display_=display;
    bool dirty_=false; UiRect dirtyRegion_;
    uint32_t nextBatchId_=1;
    int lastDevice=-1,roomCommands=0;
    bool requestedOn=false;
    void showRooms() { screen_=ControlUiScreen::ROOMS; }
    bool queueDevice(UiDevice& device,bool on) { lastDevice=device.id; requestedOn=on; return true; }
    void queueRoom(UiRoom& room,bool,uint32_t) { roomCommands=room.device_count; }
    void renderRoomDetail(); void activate(TouchZone); void markDeviceDirty(const UiDevice&);
    TouchZone zoneAt(int,int) const;
};
''' + render + mark + r'''
ControlUi::TouchZone ControlUi::zoneAt(int x,int y) const {
    switch(screen_) {
''' + zone + r'''
        default: break;
    }
    return TouchZone::NONE;
}
void ControlUi::activate(TouchZone zone) {
    switch(zone) {
''' + actions + r'''
        default: break;
    }
}
int main() {
    for(size_t count : {0,1,6,7,9,12,13,32}) {
        std::vector<UiDevice> devices(count);
        for(size_t i=0;i<count;++i) { devices[i].id=i+1; devices[i].name="Light"; }
        UiRoom room{1,"Salon",devices.data(),count};
        ControlUi control; control.rooms_=&room;
        const size_t pages=std::max(size_t{1},(count+5)/6);
        std::vector<int> accessed;
        for(size_t page=0;page<pages;++page) {
            assert(control.roomDevicePage_==page);
            display.buttons.clear(); display.page.clear(); control.renderRoomDetail();
            size_t tiles=0;
            for(const auto& button:display.buttons) {
                const auto& r=button.rect;
                assert(r.x>=0 && r.y>=0 && r.x+r.width<=960 && r.y+r.height<=540);
                assert(r.height>=44);
                if(r.y==175 || r.y==319) ++tiles;
            }
            assert(tiles==std::min(size_t{6},count-page*6));
            if(count>6) assert(display.page=="Page "+std::to_string(page+1)+" / "+std::to_string(pages));
            else assert(display.page.empty());
            for(size_t i=0;i<6;++i) {
                const auto& r=DETAIL_DEVICE_TILES[i];
                auto target=control.zoneAt(r.x+10,r.y+10);
                if(page*6+i<count) {
                    control.activate(target);
                    assert(control.lastDevice==int(page*6+i+1) && control.requestedOn);
                    accessed.push_back(control.lastDevice);
                    control.dirty_=false; control.dirtyRegion_={};
                    control.markDeviceDirty(devices[page*6+i]);
                    assert(control.dirty_ && control.dirtyRegion_.x==r.x && control.dirtyRegion_.y==r.y);
                } else assert(target==ControlUi::TouchZone::NONE);
            }
            if(page>0) {
                control.dirty_=false; control.dirtyRegion_={};
                control.markDeviceDirty(devices[0]); assert(!control.dirty_);
            }
            assert(control.zoneAt(40,470)==(page>0 ? ControlUi::TouchZone::DETAIL_PREVIOUS : ControlUi::TouchZone::NONE));
            assert(control.zoneAt(670,470)==(page+1<pages ? ControlUi::TouchZone::DETAIL_NEXT : ControlUi::TouchZone::NONE));
            control.dirty_=false;
            control.activate(ControlUi::TouchZone::DETAIL_NEXT);
            assert(control.roomDevicePage_==std::min(page+1,pages-1));
            if(page+1<pages) assert(control.dirty_ && control.dirtyRegion_.width==0);
            else assert(!control.dirty_);
        }
        assert(accessed.size()==count);
        control.activate(ControlUi::TouchZone::DETAIL_ALL_ON); assert(control.roomCommands==int(count));
        while(control.roomDevicePage_>0) control.activate(ControlUi::TouchZone::DETAIL_PREVIOUS);
        control.dirty_=false; control.activate(ControlUi::TouchZone::DETAIL_PREVIOUS);
        assert(!control.dirty_ && control.roomDevicePage_==0);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='control-room-pagination-') as directory:
    cpp = Path(directory) / 'test.cpp'
    exe = Path(directory) / 'test'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(root),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: all lights accessible for 0–32 devices, page touch routing, partial regions and whole-room commands.')
