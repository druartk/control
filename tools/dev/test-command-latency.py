#!/usr/bin/env python3
"""Exercise production command scheduling and confirmed button states."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
matter = (root / "main/matter/matter_controller.cpp").read_text()
ui = (root / "main/ui/control_ui.cpp").read_text()
ui_header = (root / "main/ui/control_ui.h").read_text()
screens = re.search(r"enum class ControlUiScreen \{.*?\};", ui_header, re.S)[0]
show_room = ui[ui.index("void ControlUi::showRoom("):ui.index("void ControlUi::markScreen(")]
header = (root / "main/matter/matter_controller.h").read_text()
types = "\n".join(re.search(pattern, header, re.S)[0] for pattern in (
    r"enum class MatterUiAction.*?\};",
    r"struct MatterUiCommand \{.*?\};",
    r"struct MatterUiResult \{.*?\};",
))
scheduler = matter[matter.index("void servicePendingUiCommands() {"):
                   matter.index("esp_err_t ensureCommandWorker() {", matter.index("void servicePendingUiCommands() {"))]
queue = ui[ui.index("bool ControlUi::queueDevice("):ui.index("void ControlUi::queueRoom(")]
all_off = ui[ui.index("void ControlUi::queueAllOff("):ui.index("void ControlUi::chooseRoom(")]
complete = ui[ui.index("bool ControlUi::completeMatterCommand("):ui.index("void ControlUi::showHome()")]
overlay = matter[matter.index("    for (const auto &[endpoint, on] : g_scanOnOffStates)"):
                 matter.index("    std::stable_sort(g_postResult.devices.begin()")]
load_cache = matter[matter.index("esp_err_t matter_controller_load_device_cache("):
                    matter.index("esp_err_t matter_controller_clear_device_cache()")]
source = r'''
#include <cassert>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include "main/ui/control_ui_model.h"
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=1, pdTRUE=1;
constexpr int ESP_ERR_INVALID_STATE=2, ESP_ERR_NOT_FOUND=3, ESP_ERR_NOT_SUPPORTED=4;
constexpr const char* TAG="test";
constexpr const char* MATTER_TAG="test";
template<class... T> void log(T...) {}
#define ESP_LOGI(...) log(__VA_ARGS__)
#define ESP_LOGW(...) log(__VA_ARGS__)
const char* esp_err_to_name(int) { return "test"; }
''' + types + r'''
struct CommandWorkerArgs { bool from_ui=false, discovery=false; MatterUiCommand ui_command; };
std::deque<CommandWorkerArgs> commands;
constexpr size_t COMMAND_QUEUE_LENGTH=8;
int taskTokens[3], g_commandQueue=0;
void* g_discoveryTask=&taskTokens[0];
void* g_commandTask=&taskTokens[1];
void* currentTask=g_commandTask;
void* xTaskGetCurrentTaskHandle() { return currentTask; }
int xQueuePeek(int, CommandWorkerArgs* out, int) {
    if(commands.empty()) return 0;
    *out=commands.front(); return pdTRUE;
}
int xQueueReceive(int, CommandWorkerArgs* out, int) {
    if(!xQueuePeek(0,out,0)) return 0;
    commands.pop_front(); return pdTRUE;
}
void servicePendingUiCommands();
std::vector<uint16_t> invoked;
bool executing=false;
void executeUiCommand(const MatterUiCommand& command) {
    assert(!executing); executing=true;
    invoked.push_back(command.endpoint_id);
    // Command readback uses readMatterAttribute too; it must not re-enter.
    servicePendingUiCommands();
    executing=false;
}
''' + scheduler + r'''
struct ScanDevice { uint16_t endpoint_id; bool onoff_state_known=false, onoff_state=false; };
enum class MatterSavedState { None, Valid, Invalid };
enum class MatterPostCommissioningStatus { IDLE, DEVICE_LIST, SAVED_NODE_INVALID };
struct SavedNode { uint64_t node_id=123; } g_savedNode;
struct Scan {
    std::vector<ScanDevice> devices;
    SavedNode savedNode;
    MatterPostCommissioningStatus status=MatterPostCommissioningStatus::IDLE;
    int error=ESP_OK; std::string errorText;
} g_postResult, g_discoveryResult;
using MatterPostCommissioningResult=Scan;
MatterSavedState g_savedState=MatterSavedState::Valid;
std::string g_savedStateReason;
void ensureSavedNodeLoaded() {}
int loadDeviceCache(std::vector<ScanDevice>& devices) { devices={{99,true,false}}; return ESP_OK; }
std::map<uint16_t,bool> g_scanOnOffStates;
void applyCommandStates() {
''' + overlay + r'''
}
''' + load_cache + r'''
int enqueued=0, enqueueResult=ESP_OK;
std::vector<MatterUiCommand> recordedCommands;
MatterUiCommand lastCommand;
int matter_controller_ui_enqueue(const MatterUiCommand& command) {
    ++enqueued; lastCommand=command; recordedCommands.push_back(command); return enqueueResult;
}
''' + screens + r'''
struct ControlUi {
    UiRoom* rooms_; size_t roomCount_=1;
    size_t selectedRoom_=0, roomDevicePage_=0;
    ControlUiScreen screen_=ControlUiScreen::HOME;
    void showRoom(size_t);
    void markScreen(ControlUiScreen screen) { screen_=screen; dirty_=true; }
    const char* roomName(size_t i) const { return rooms_[i].name; }
    bool dirty_=false; int redraws=0;
    uint32_t activeBatchId_=0;
    size_t activeBatchExpected_=0, activeBatchSuccess_=0, activeBatchFailed_=0;
    char activeBatchRoom_[32]={};
    void markDeviceDirty(const UiDevice&) { dirty_=true; ++redraws; }
    bool queueDevice(UiDevice&,bool,uint32_t=0);
    void queueAllOff();
    bool completeMatterCommand(const MatterUiResult&);
};
''' + queue + complete + show_room + all_off + r'''
int main() {
    CommandWorkerArgs queued; queued.from_ui=true;
    for(int endpoint=1; endpoint<=3; ++endpoint) {
        queued.ui_command.endpoint_id=endpoint; commands.push_back(queued);
    }
    g_discoveryTask=0; servicePendingUiCommands();
    assert(commands.size()==3 && invoked.empty());
    g_discoveryTask=g_commandTask; currentTask=&taskTokens[2]; servicePendingUiCommands();
    assert(commands.size()==3 && invoked.empty());
    currentTask=g_commandTask; servicePendingUiCommands();
    assert(commands.empty() && (invoked==std::vector<uint16_t>{1,2,3}));
    CommandWorkerArgs scan; scan.discovery=true;
    commands.push_back(scan); commands.push_back(queued);
    servicePendingUiCommands(); assert(commands.size()==2);
    commands.front().discovery=false; // Legacy commands keep their FIFO position.
    servicePendingUiCommands(); assert(commands.size()==2);
    commands.clear(); invoked.clear();
    for(int i=0;i<12;++i) commands.push_back(queued);
    servicePendingUiCommands();
    assert(invoked.size()==COMMAND_QUEUE_LENGTH && commands.size()==4);

    g_postResult.devices={{18,false,false},{19,true,true}};
    g_discoveryResult.devices=g_postResult.devices;
    g_scanOnOffStates[18]=false;
    g_scanOnOffStates[18]=true;
    applyCommandStates();
    assert(g_postResult.devices[0].onoff_state_known && g_postResult.devices[0].onoff_state);
    assert(g_discoveryResult.devices[0].onoff_state_known && g_discoveryResult.devices[0].onoff_state);
    assert(g_postResult.devices[1].onoff_state && g_discoveryResult.devices[1].onoff_state);
    MatterPostCommissioningResult cache;
    assert(matter_controller_load_device_cache(cache)==ESP_OK);
    assert(cache.devices[0].endpoint_id==99 && g_postResult.devices[0].endpoint_id==18);
    g_discoveryTask=0;
    assert(matter_controller_load_device_cache(cache)==ESP_OK);
    assert(g_postResult.devices[0].endpoint_id==99);

    UiDevice device{}; device.id=18; device.name="Lampe";
    device.matter_node_id=123; device.matter_endpoint_id=18;
    device.matter_bound=true; device.supports_onoff=true; device.state_known=true;
    UiRoom room{1,"Salon",&device,1}; ControlUi control{&room};
    assert(control.queueDevice(device,true));
    assert(device.command_pending && !device.is_on && device.state_known);
    assert(!control.dirty_ && control.redraws==0 && enqueued==1);
    assert(!control.queueDevice(device,true) && enqueued==1);
    MatterUiResult result; result.command=lastCommand; result.success=true;
    assert(control.completeMatterCommand(result));
    assert(!device.command_pending && device.is_on && control.redraws==1);
    assert(!control.completeMatterCommand(result) && control.redraws==1);
    control.dirty_=false;
    assert(control.queueDevice(device,false));
    assert(device.command_pending && device.is_on && !control.dirty_);
    result.command=lastCommand; result.success=false;
    assert(control.completeMatterCommand(result));
    assert(device.is_on && device.command_failed && !device.command_pending);
    device.state_known=false;
    assert(!control.queueDevice(device,true) && enqueued==2);

    device.state_known=true;
    for(auto screen : {ControlUiScreen::HOME,ControlUiScreen::ROOMS}) {
        control.screen_=screen; control.dirty_=false; device.is_on=false;
        int before=enqueued;
        control.showRoom(0);
        assert(control.screen_==screen && !control.dirty_ && device.command_pending);
        assert(enqueued==before+1 && lastCommand.action==MatterUiAction::SetOn);
        control.showRoom(0);
        assert(enqueued==before+1 && "repeated taps cannot queue duplicate commands");
        result.command=lastCommand; result.success=true;
        assert(control.completeMatterCommand(result) && device.is_on);
        control.dirty_=false;
        control.showRoom(0);
        assert(control.screen_==screen && !control.dirty_ && device.command_pending);
        assert(enqueued==before+2 && lastCommand.action==MatterUiAction::SetOff);
        result.command=lastCommand;
        assert(control.completeMatterCommand(result) && !device.is_on);
    }
    UiDevice multiple[2]{};
    UiRoom rooms[]={{1,"Empty",nullptr,0},{2,"Multiple",multiple,2}};
    control.rooms_=rooms; control.roomCount_=2;
    int before=enqueued;
    control.showRoom(1);
    assert(control.screen_==ControlUiScreen::ROOM_DETAIL && control.selectedRoom_==1);
    control.showRoom(0);
    assert(control.screen_==ControlUiScreen::ROOM_DETAIL && control.selectedRoom_==0);
    control.showRoom(2);
    control.rooms_=nullptr; control.showRoom(0);
    assert(enqueued==before && control.selectedRoom_==0);
    control.queueAllOff();
    assert(enqueued==before);
    UiDevice lights[32]{};
    for(size_t i=0; i<32; ++i) {
        auto& light=lights[i];
        light.id=i+1; light.name="Lampe"; light.is_on=true;
        light.state_known=true; light.matter_bound=true;
        light.supports_onoff=true; light.matter_node_id=123;
        light.matter_endpoint_id=i+1;
    }
    UiRoom house[]={{1,"Salon",lights,16},{2,"Chambre",lights+16,16},{3,"Vide",nullptr,0}};
    control.rooms_=house; control.roomCount_=3;
    recordedCommands.clear(); control.queueAllOff();
    assert(recordedCommands.size()==32);
    for(size_t i=0; i<32; ++i) {
        assert(recordedCommands[i].action==MatterUiAction::SetOff);
        assert(recordedCommands[i].endpoint_id==i+1);
        assert(lights[i].command_pending && lights[i].is_on);
    }
    control.queueAllOff();
    assert(recordedCommands.size()==32);
    for(auto& light : lights) light.command_pending=false;
    lights[0].supports_onoff=false; lights[1].matter_bound=false;
    lights[2].reachable_known=true; lights[2].reachable=false;
    lights[3].state_known=false;
    recordedCommands.clear(); control.queueAllOff();
    assert(recordedCommands.size()==28);
    assert(lights[2].command_failed && lights[3].command_failed);
}
'''
with tempfile.TemporaryDirectory(prefix="control-command-latency-") as temp:
    cpp = Path(temp) / "test.cpp"
    executable = Path(temp) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DCONFIG_CONTROL_UI_MATTER_COMMANDS=1",
                    "-DCONFIG_CONTROL_MATTER_COMMISSIONING_EXPERIMENTAL=1", "-I", str(root),
                    str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: scan yields to commands, readback cannot recurse, buttons wait for success.")
