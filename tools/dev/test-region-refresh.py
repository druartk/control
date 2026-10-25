#!/usr/bin/env python3
"""Check real UI region selection and pixel packing with EPD effects stubbed."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
ui = (root / "main/ui/control_ui.cpp").read_text()
header = (root / "main/ui/control_ui.h").read_text()
display = (root / "main/display/display_manager.cpp").read_text()
screen_enum = re.search(r"enum class ControlUiScreen \{.*?\};", header, re.S)[0]
tiles = re.search(r"constexpr UiRect DETAIL_DEVICE_TILES\[\] = \{.*?\};", ui, re.S)[0]
layout = "\n".join(re.search(rf"constexpr int {name} = [^;]+;", ui)[0] for name in (
    "SCREEN_MARGIN", "CONTENT_WIDTH", "ROOM_TILES_TOP",
    "ROOM_TILE_HEIGHT", "ROOM_TILE_GAP", "ROOM_TILE_COLUMN_GAP"))
layout += ui[ui.index("UiRect roomTile("):ui.index("void copyText(")]
layout += "\n".join(re.search(rf"constexpr UiRect {name}\{{[^;]+;", ui)[0] for name in ("HEADER_STATUS",))
render = ui[ui.index("bool ControlUi::render("):ui.index("void ControlUi::renderHome()")]
context = ui[ui.index("void ControlUi::setContext("):ui.index("void ControlUi::bindMatterDevices(")]
mark = ui[ui.index("void ControlUi::markDeviceDirty("):ui.index("bool ControlUi::queueDevice(")]
commit = display[display.index("esp_err_t DisplayManager::commitRegion("):]
pixels = display[display.index("uint8_t framebufferPixel("):display.index("#endif")]
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>
template<typename... Args> void testLog(Args...) {}
#define ESP_LOGI(...) testLog(__VA_ARGS__)
#define ESP_LOGW(...) testLog(__VA_ARGS__)
#define ESP_LOGE(...) testLog(__VA_ARGS__)
constexpr const char* TAG="test";
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_INVALID_STATE=1, ESP_ERR_INVALID_ARG=2;
constexpr int ESP_ERR_NO_MEM=3, ESP_ERR_NOT_SUPPORTED=4, MALLOC_CAP_8BIT=1, MALLOC_CAP_SPIRAM=2;
bool failAllocation = false;
const char* esp_err_to_name(int) { return "test"; }
void* heap_caps_malloc(size_t n, int caps) { assert(caps==(MALLOC_CAP_8BIT|MALLOC_CAP_SPIRAM)); return failAllocation ? nullptr : malloc(n); }
void heap_caps_free(void* p) { free(p); }
int64_t esp_timer_get_time() { return 1000; }
void copyText(char* dst, size_t size, const char* src) { std::snprintf(dst,size,"%s",src); }
struct UiRect { int x=0, y=0, width=0, height=0; };
using Rect_t = UiRect;
UiRect cleared, drawn;
std::vector<uint8_t> sent;
int clears=0, draws=0, power=0;
esp_err_t drawResult=ESP_OK;
void epd_poweron() { ++power; }
void epd_poweroff() { --power; }
void epd_clear_area(Rect_t r) { assert(power==1); cleared=r; ++clears; }
esp_err_t epd_draw_grayscale_image(Rect_t r, uint8_t* data) {
    assert(clears>draws && power==1); drawn=r; ++draws;
    sent.assign(data, data + ((r.width+1)/2)*r.height);
    return drawResult;
}
struct DisplayManager {
    static constexpr int WIDTH=960, HEIGHT=540;
    enum class RefreshState { READY, PARTIAL_REFRESHING };
    RefreshState refreshState_ = RefreshState::READY;
    std::vector<uint8_t> pixels = std::vector<uint8_t>(WIDTH*HEIGHT/2, 255);
    uint8_t* framebuffer_ = pixels.data(); bool available_ = true;
    int full=0; bool failFull=false;
    void textFitted(int, int, const char*, int) { ++statusDraws; }
    int statusDraws=0;
    bool available() const { return available_; }
    void beginFrame() {}
    bool commitFull(const char*) { ++full; return !failFull; }
    esp_err_t commitRegion(const UiRect&, const char*);
};
''' + pixels + commit + screen_enum + tiles + layout + r'''
struct UiDevice {};
struct UiRoom { UiDevice* devices; size_t device_count; };
struct ControlUi {
    DisplayManager& display_;
    UiRoom* rooms_; size_t selectedRoom_=0, roomCount_=1;
    ControlUiScreen screen_ = ControlUiScreen::ROOM_DETAIL;
    bool dirty_=true; UiRect dirtyRegion_{};
    int64_t acceptTouchAfterUs_=0;
    bool pressActive_=false;
    enum class TouchZone { NONE };
    TouchZone pressedZone_=TouchZone::NONE;
    bool wifiConnected_=false; char clockText_[8]="--:--";
    int batteryPercent_=-1, batteryDraws=0;
    void setContext(bool,const char*,int=-1);
    bool lockRender() { return true; } void unlockRender() {}
    const char* screenName() { return "test"; }
    bool render(bool); void markDeviceDirty(const UiDevice&);
''' + "\n".join(f"    void {name}() {{" + ("++batteryDraws;" if name=="renderBattery" else "") + "}" for name in re.findall(r"void (render\w+)\(\);", header)) + r'''
};
''' + render + context + mark + r'''
int main() {
    DisplayManager display; UiDevice devices[6]; UiRoom room{devices,6};
    ControlUi ui{display,&room};
    ui.setContext(true,"12:34");
    assert(ui.render(false) && display.full==1 && display.statusDraws==2);
    // Unchanged context must preserve a pending button-only refresh.
    ui.markDeviceDirty(devices[0]);
    ui.setContext(true,"12:34");
    assert(ui.dirtyRegion_.width==285);
    ui.markDeviceDirty(devices[0]);
    assert(ui.dirtyRegion_.x==32 && ui.dirtyRegion_.width==285);
    assert(ui.render(false) && !ui.dirty_ && ui.dirtyRegion_.width==0);
    ui.setContext(true,"12:34");
    assert(!ui.dirty_ && ui.dirtyRegion_.width==0);
    assert(ui.acceptTouchAfterUs_==1000 && !ui.pressActive_);
#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
    assert(display.full==1 && draws==1 && clears==1 && power==0);
    assert(drawn.x==32 && drawn.y==175 && drawn.width==285 && drawn.height==126);
    assert(cleared.x==drawn.x && cleared.width==drawn.width);
#else
    assert(display.full==2 && draws==0);
#endif
    ui.markDeviceDirty(devices[0]); ui.markDeviceDirty(devices[5]);
    assert(ui.dirtyRegion_.x==32 && ui.dirtyRegion_.y==175);
    assert(ui.dirtyRegion_.width==896 && ui.dirtyRegion_.height==270);
    int previousFull=display.full;
    assert(ui.render(true) && display.full==previousFull+1);
    ui.dirty_=true; ui.markDeviceDirty(devices[0]);
    assert(ui.dirtyRegion_.width==0);
    assert(ui.render(false) && display.full==previousFull+2);
    ui.screen_=ControlUiScreen::HOME; ui.dirty_=true; ui.markDeviceDirty(devices[0]);
    assert(ui.dirtyRegion_.width==0);
    assert(ui.render(false) && display.full==previousFull+3);
    ui.setContext(true,"12:36");
    assert(ui.dirty_ && ui.dirtyRegion_.x==HEADER_STATUS.x);
    display.failFull=true; ui.dirty_=true;
    assert(!ui.render(true) && ui.dirty_);
    display.failFull=false;
    assert(ui.render(true) && !ui.dirty_);
    previousFull=display.full-3;
#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
    ui.screen_=ControlUiScreen::ROOM_DETAIL; ui.markDeviceDirty(devices[0]);
    failAllocation=true;
    assert(ui.render(false) && display.full==previousFull+4 && !ui.dirty_);
    failAllocation=false;
    assert(display.commitRegion({0,0,0,10},"empty")==ESP_ERR_INVALID_ARG);
    for (int y=0;y<4;++y) for (int x=0;x<8;++x)
        packedSetPixel(display.framebuffer_,960,x,y,uint8_t(((x+y)%16)<<4));
    assert(display.commitRegion({1,1,3,2},"odd_stride")==ESP_OK);
    assert(sent.size()==4);
    for(int y=0;y<2;++y) for(int x=0;x<3;++x) {
        int value=(sent[y*2+x/2]>>(x%2 ? 4 : 0))&15;
        assert(value==(x+y+2)%16);
    }
    assert(display.commitRegion({-1,-1,3,3},"clipped")==ESP_OK);
    assert(drawn.x==0 && drawn.y==0 && drawn.width==2 && drawn.height==2);
    assert(power==0);
    drawResult=ESP_ERR_NO_MEM;
    assert(display.commitRegion({0,0,10,10},"failed_draw")==ESP_ERR_NO_MEM);
    assert(power==0 && display.refreshState_==DisplayManager::RefreshState::READY);
#endif
    drawResult=ESP_OK;
    DisplayManager overview;
    UiDevice lights[6]; UiRoom rooms[6];
    for(int i=0;i<6;++i) rooms[i]={&lights[i],1};
    ControlUi home{overview,rooms}; home.roomCount_=6;
    for(auto screen : {ControlUiScreen::ROOMS}) {
        home.screen_=screen;
        assert(home.render(true));
        const size_t count=6;
        const int top=ROOM_TILES_TOP;
        for(size_t i=0;i<count;++i) {
            home.markDeviceDirty(lights[i]);
            const auto tile=roomTile(i,count,top);
            assert(home.dirtyRegion_.x==tile.x && home.dirtyRegion_.y==tile.y);
            assert(home.dirtyRegion_.width==tile.width && home.dirtyRegion_.height==tile.height);
            const int fullBefore=overview.full;
            assert(home.render(false));
#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
            assert(overview.full==fullBefore);
            assert(drawn.x==tile.x && drawn.y==tile.y && drawn.width==tile.width);
#else
            assert(overview.full==fullBefore+1);
#endif
        }
        home.markDeviceDirty(lights[0]); home.markDeviceDirty(lights[count-1]);
        assert(home.dirtyRegion_.x==32 && home.dirtyRegion_.y==top);
        assert(home.dirtyRegion_.width==896);
        assert(home.dirtyRegion_.height==(count==3 ? 126 : 270));
        assert(home.render(false));
    }
    home.screen_=ControlUiScreen::HOME;
    assert(home.render(true));
    for(auto& light : lights) home.markDeviceDirty(light);
    assert(!home.dirty_ && home.dirtyRegion_.width==0);
    home.setContext(true,"12:36",75);
    assert(home.render(true));
    home.setContext(true,"12:36",75);
    assert(!home.dirty_);
    home.setContext(true,"12:36",80);
    assert(home.dirty_ && home.dirtyRegion_.x==HEADER_STATUS.x);
    assert(home.render(false));
#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
    assert(drawn.x==HEADER_STATUS.x && drawn.y==HEADER_STATUS.y);
    assert(drawn.width==HEADER_STATUS.width && drawn.height==HEADER_STATUS.height);
#endif
    home.markDeviceDirty(lights[0]);
    home.setContext(true,"12:36",-1);
    assert(home.dirty_ && home.dirtyRegion_.x==HEADER_STATUS.x && home.batteryPercent_==-1);
    assert(home.render(false));
    home.setContext(true,"12:36",200);
    assert(!home.dirty_ && home.batteryPercent_==-1);
    for (auto screen : {ControlUiScreen::HOME, ControlUiScreen::ROOMS,
         ControlUiScreen::ROOM_DETAIL, ControlUiScreen::SETTINGS,
         ControlUiScreen::DEVICE_SCAN, ControlUiScreen::DEVICE_SCAN_RESULT,
         ControlUiScreen::DEVICE_ASSIGN_ROOM, ControlUiScreen::DEVICE_RENAME,
         ControlUiScreen::ROOM_CREATE, ControlUiScreen::ONBOARDING_SUMMARY,
         ControlUiScreen::CLEAR_CONFIG_CONFIRM}) {
        home.screen_=screen;
        const int before=home.batteryDraws;
        assert(home.render(true) && home.batteryDraws==before+1);
        home.setContext(true,"12:36",50);
        assert(home.dirty_);
        assert(home.dirtyRegion_.x==HEADER_STATUS.x);
        assert(home.render(false));
        home.setContext(true,"12:36",-1);
        assert(home.render(false));
        home.setContext(false,"12:37",-1);
        assert(home.dirty_ && home.dirtyRegion_.x==HEADER_STATUS.x);
        const int statusBefore=overview.statusDraws;
        assert(home.render(false) && overview.statusDraws==statusBefore+2);
    }
    home.screen_=ControlUiScreen::ROOM_DETAIL;
    home.markDeviceDirty(lights[0]); home.setContext(true,"12:36",75);
    assert(home.dirty_ && home.dirtyRegion_.width==0);
}
'''
with tempfile.TemporaryDirectory(prefix="control-region-test-") as temp:
    source_path = Path(temp) / "test.cpp"
    executable = Path(temp) / "test"
    source_path.write_text(source)
    for enabled in (0, 1):
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-DCONFIG_CONTROL_ENABLE_PARTIAL_REFRESH={enabled}",
                        str(source_path), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: button regions, batches, full fallback, clearing, clipping and odd-width pixels.")
