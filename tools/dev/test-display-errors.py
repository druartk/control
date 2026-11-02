#!/usr/bin/env python3
"""Exercise the real EPD allocation failures and application retry path."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
driver = (root / "components/lilygo_epd47/upstream/epd_driver.c").read_text()
display = (root / "main/display/display_manager.cpp").read_text()
app = (root / "main/main.cpp").read_text()
draw = driver[driver.index("esp_err_t IRAM_ATTR epd_draw_image("):driver.index("/***        local functions", driver.index("esp_err_t IRAM_ATTR epd_draw_image("))]
initialize = driver[driver.index("esp_err_t epd_init(void)"):driver.index("void epd_push_pixels(")]
draw = draw[:draw.rfind("/******************************************************************************")]
full = display[display.index("bool DisplayManager::commitFull("):display.index("esp_err_t DisplayManager::commitRegion(")]
render = app[app.index("    bool renderUi("):app.index("    bool renderUiEvenIfMatterQuiet(")]
retry = app[app.index("    void processPendingRender("):app.index("#if CONFIG_CONTROL_UI_DASHBOARD_V1", app.index("    void processPendingRender("))]
source = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <string>
template<class... T> void log(T...) {}
#define ESP_LOGI(...) log(__VA_ARGS__)
#define ESP_LOGE(...) log(__VA_ARGS__)
#define ESP_LOGW(...) log(__VA_ARGS__)
#define IRAM_ATTR
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_NO_MEM=1, pdPASS=1, pdFAIL=0, portMAX_DELAY=-1;
constexpr int EPD_RENDER_TASK_STACK_WORDS=2048;
constexpr int EPD_WIDTH=960, EPD_OUTPUT_QUEUE_LENGTH=8, MALLOC_CAP_8BIT=1;
using BaseType_t=int;
using SemaphoreHandle_t=void*;
using TaskHandle_t=void*;
struct Rect_t { int x,y,width,height; };
enum DrawMode_t { BLACK_ON_WHITE };
struct OutputParams { Rect_t area; uint8_t* data_ptr; int frame; DrawMode_t mode; SemaphoreHandle_t done_smphr; };
void provide_out(void*) {} void feed_display(void*) {}
int semCalls=0, semFailure=0, semDeleted=0, taskCalls=0, taskFailure=0, taskDeleted=0, waits=0, resets=0;
void* output_queue=nullptr;
int display_core=0,currentCore=0;
uint32_t skipping=0;
uint8_t lutMemory[65536];
uint8_t* conversion_lut=nullptr;
int xPortGetCoreID() { return currentCore; }
int epd_base_init(int width) { assert(width==EPD_WIDTH); return ESP_OK; }
void* heap_caps_malloc(size_t size,int caps) { assert(size==sizeof(lutMemory) && caps==MALLOC_CAP_8BIT); return lutMemory; }
void heap_caps_free(void* pointer) { assert(pointer==lutMemory); }
void* xQueueCreate(int length,int bytes) { assert(length==EPD_OUTPUT_QUEUE_LENGTH && bytes==EPD_WIDTH/2); return (void*)1; }
int delayTicks=0;
void resetMocks() { semCalls=semFailure=semDeleted=taskCalls=taskFailure=taskDeleted=waits=resets=delayTicks=0; }
SemaphoreHandle_t xSemaphoreCreateBinary() {
    ++semCalls; return semCalls==semFailure ? nullptr : reinterpret_cast<void*>(intptr_t(semCalls));
}
void vSemaphoreDelete(void* p) { assert(p); ++semDeleted; }
void vTaskDelay(int ticks) { delayTicks+=ticks; }
int xTaskCreatePinnedToCore(void(*entry)(void*),const char*,int,void*,int,void** handle,int core) {
    assert(core==(entry==feed_display ? display_core : 0) && "row feeder must share the LCD/RMT interrupt core");
    ++taskCalls; if(taskCalls==taskFailure) return pdFAIL;
    *handle=reinterpret_cast<void*>(intptr_t(taskCalls)); return pdPASS;
}
void vTaskDelete(void* handle) { assert(handle); ++taskDeleted; }
void xQueueReset(void*) { ++resets; }
void xSemaphoreTake(void* handle,int) { assert(handle); ++waits; }
''' + initialize + draw + r'''
constexpr const char* TAG="test";
constexpr size_t FRAMEBUFFER_SIZE=4;
const char* esp_err_to_name(int) { return "test"; }
Rect_t epd_full_screen() { return {0,0,2,4}; }
esp_err_t epd_draw_grayscale_image(Rect_t r,uint8_t* p) { return epd_draw_image(r,p,BLACK_ON_WHITE); }
int power=0;
void epd_poweron() { ++power; } void epd_poweroff() { --power; }
void epd_clear() { assert(power==1); }
struct DisplayManager {
    enum class RefreshState { READY,FULL_REFRESHING };
    bool available_=true, firstRender_=true;
    uint8_t pixels[4]={0}; uint8_t* framebuffer_=pixels;
    RefreshState refreshState_=RefreshState::READY;
    bool available() { return available_; } bool busy() { return refreshState_!=RefreshState::READY; }
    bool commitFull(const char*);
};
''' + full + r'''
enum class AppState { WEATHER_HOME };
const char* appStateName(AppState) { return "test"; }
struct Ui {
    bool result=false; int calls=0;
    bool render(bool) { ++calls; return result; }
};
struct ControlApplication {
    std::atomic<bool> matterCommissioningUiQuiet_{false};
    bool dashboardAvailable_=true,pendingRender_=false;
    AppState state_=AppState::WEATHER_HOME, pendingRenderState_=state_;
    const char* pendingRenderReason_=nullptr;
    DisplayManager display_; Ui controlUi_,ui_;
    void updateControlUiContext() {} bool viewModel() { return false; }
''' + render + retry + r'''
};
int main() {
    uint8_t data[4]={0}; Rect_t area{0,0,2,4};
    for(currentCore=0;currentCore<=1;++currentCore) {
        resetMocks();
        assert(epd_init()==ESP_OK && display_core==currentCore);
        assert(epd_draw_image(area,data,BLACK_ON_WHITE)==ESP_OK && taskCalls==30);
    }
    display_core=0;
    for(int failure=1;failure<=2;++failure) {
        resetMocks(); semFailure=failure;
        assert(epd_draw_image(area,data,BLACK_ON_WHITE)==ESP_ERR_NO_MEM);
        assert(semDeleted==1 && taskCalls==0 && waits==0);
    }
    for(int failure : {1,2,5}) {
        resetMocks(); taskFailure=failure;
        assert(epd_draw_image(area,data,BLACK_ON_WHITE)==ESP_ERR_NO_MEM);
        assert(semDeleted==2 && taskDeleted==taskCalls-1 && resets==1);
        assert(waits==(failure==5 ? 4 : 0));
    }
    resetMocks();
    assert(epd_draw_image(area,data,BLACK_ON_WHITE)==ESP_OK);
    assert(taskCalls==30 && taskDeleted==30 && waits==30 && semDeleted==2 && resets==0);
    assert(delayTicks==10+15*5 && "keep factory settling and inter-pass timing");
    DisplayManager display;
    for (auto& pixel : display.pixels) pixel=0xFF;
    resetMocks();
    assert(!display.commitFull("empty") && display.firstRender_ && !display.busy());
    assert(power==0 && taskCalls==0 && "empty frames must preserve the panel");
    display.pixels[0]=0;
    resetMocks(); taskFailure=2;
    assert(!display.commitFull("failed"));
    assert(display.firstRender_ && !display.busy() && power==0);
    resetMocks(); assert(display.commitFull("retry"));
    assert(!display.firstRender_ && !display.busy() && power==0);
    ControlApplication app;
    assert(!app.renderUi("touch") && app.pendingRender_);
    app.processPendingRender(); assert(!app.pendingRender_);
    int calls=app.controlUi_.calls+app.ui_.calls;
    for(int i=0;i<100;++i) app.processPendingRender();
    assert(app.controlUi_.calls+app.ui_.calls==calls);
    assert(!app.renderUi("new_touch") && app.pendingRender_);
    app.matterCommissioningUiQuiet_=true;
    app.processPendingRender(); assert(app.pendingRender_);
    assert(app.controlUi_.calls+app.ui_.calls==calls+1);
    app.matterCommissioningUiQuiet_=false;
    app.controlUi_.result=app.ui_.result=true;
    app.processPendingRender(); assert(!app.pendingRender_);
}
'''
with tempfile.TemporaryDirectory(prefix="control-display-errors-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / "test"
    cpp.write_text(source)
    for dashboard in (0, 1):
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        f"-DCONFIG_CONTROL_UI_DASHBOARD_V1={dashboard}", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print("PASS: LCD/RMT core affinity, EPD error cleanup and bounded retries.")
