#!/usr/bin/env python3
"""Exercise calibrated battery readings, Wi-Fi contention and failed setup cleanup."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
battery = (root / "main/system/control_battery.cpp").read_text()
battery = "\n".join(line for line in battery.splitlines() if not line.startswith("#include"))
app = (root / "main/main.cpp").read_text()
context = app[app.index("    void updateControlUiContext() {"):app.index("\n#endif", app.index("    void updateControlUiContext() {"))]
dispatch_start = app.index("        if (dashboardAvailable_ && state_ == AppState::WEATHER_HOME) {", app.index("    void processTouchEvents()"))
dispatch = app[dispatch_start:app.index("\n#endif", dispatch_start)]
source = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <ctime>
using adc_oneshot_unit_handle_t=void*;
using adc_cali_handle_t=void*;
using adc_unit_t=int;
using adc_channel_t=int;
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_TIMEOUT=1, ESP_FAIL=2;
constexpr int CONTROL_BOARD_BATTERY_ADC=14, ADC_ATTEN_DB_12=12, ADC_BITWIDTH_DEFAULT=0;
struct adc_oneshot_unit_init_cfg_t { int unit_id; };
struct adc_oneshot_chan_cfg_t { int atten,bitwidth; };
struct adc_cali_curve_fitting_config_t { int unit_id,chan,atten,bitwidth; };
template<class... T> void testLog(T...) {}
#define ESP_LOGW(...) testLog(__VA_ARGS__)
#define ESP_LOGI(...) testLog(__VA_ARGS__)
const char* esp_err_to_name(int) { return "test"; }
int failStage=0, deleted=0, created=0, reads=0, timeouts=0, rawMv=2000;
bool conversionFailure=false, readFailure=false;
int adc_oneshot_io_to_channel(int gpio,int* unit,int* channel) {
    assert(gpio==14); *unit=2; *channel=3; return failStage==1 ? ESP_FAIL : ESP_OK;
}
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t* c,void** handle) {
    assert(c->unit_id==2); if(failStage==2) return ESP_FAIL;
    ++created; *handle=&created; return ESP_OK;
}
int adc_oneshot_config_channel(void* handle,int ch,const adc_oneshot_chan_cfg_t* c) {
    assert(handle && ch==3 && c->atten==12 && c->bitwidth==0);
    return failStage==3 ? ESP_FAIL : ESP_OK;
}
int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t* c,void** handle) {
    assert(c->unit_id==2 && c->chan==3 && c->atten==12 && c->bitwidth==0);
    if(failStage==4) return ESP_FAIL;
    *handle=&deleted; return ESP_OK;
}
int adc_oneshot_del_unit(void* handle) { assert(handle); ++deleted; return ESP_OK; }
int adc_oneshot_read(void* handle,int ch,int* raw) {
    assert(handle && ch==3); ++reads;
    if(readFailure) return ESP_FAIL;
    if(reads<=timeouts) return ESP_ERR_TIMEOUT;
    *raw=rawMv+(reads%2 ? -10 : 10); return ESP_OK;
}
int adc_cali_raw_to_voltage(void* handle,int raw,int* mv) {
    assert(handle); *mv=raw; return conversionFailure ? ESP_FAIL : ESP_OK;
}
''' + battery + r'''
int64_t nowUs=1000000;
int64_t esp_timer_get_time() { return nowUs; }
enum class TouchEventType { PRESS, MOVE, RELEASE, CANCEL };
enum class AppState { WEATHER_HOME };
struct TouchEvent { TouchEventType type; };
struct Application {
    bool dashboardAvailable_=true;
    AppState state_=AppState::WEATHER_HOME;
    int batteryPercent_=-1; int64_t nextBatteryReadUs_=0;
    struct Wifi { bool isConnected() { return true; } } wifi_;
    struct Ui {
        int percent=-1;
        bool handleTouch(const TouchEvent&) { return false; }
        void setContext(bool,const char*,int p) { percent=p; }
    } controlUi_;
''' + context + r'''
    bool renderUi(const char*) { updateControlUiContext(); return true; }
    void touch(TouchEvent event) {
''' + dispatch + r'''
    }
};
int main() {
    for(int stage=1;stage<=4;++stage) {
        failStage=stage; deleted=created=0;
        assert(control_battery_percent()==-1 && adc==nullptr && reads==0);
        assert(deleted==(stage>=3 ? 1 : 0));
    }
    failStage=0;
    rawMv=2100; reads=0;
    assert(control_battery_percent()==100 && reads==16);
    const int allocations=created;
    rawMv=1650; reads=0; assert(control_battery_percent()==0);
    rawMv=1875; reads=0; assert(control_battery_percent()==50);
    rawMv=2000; reads=0; assert(control_battery_percent()==80);
    assert(created==allocations && "reuse the ADC and its calibration between readings");
    timeouts=8; reads=0; assert(control_battery_percent()==80);
    timeouts=9; reads=0; assert(control_battery_percent()==-1);
    timeouts=16; reads=0; assert(control_battery_percent()==-1);
    timeouts=0; conversionFailure=true; reads=0;
    assert(control_battery_percent()==-1);
    conversionFailure=false; readFailure=true; reads=0;
    assert(control_battery_percent()==-1);
    readFailure=false;
    rawMv=1000; reads=0; assert(control_battery_percent()==-1);
    rawMv=2300; reads=0; assert(control_battery_percent()==-1);
    Application app;
    reads=0; rawMv=1875;
    app.touch({TouchEventType::PRESS}); assert(reads==0);
    app.touch({TouchEventType::RELEASE});
    assert(reads==16 && app.controlUi_.percent==50);
    assert(app.nextBatteryReadUs_==nowUs+60000000);
    nowUs+=1000000; rawMv=2000; reads=0;
    app.updateControlUiContext(); assert(reads==0 && app.controlUi_.percent==50);
    app.touch({TouchEventType::RELEASE});
    assert(reads==16 && app.controlUi_.percent==80);
    reads=0; nowUs=app.nextBatteryReadUs_;
    app.updateControlUiContext(); assert(reads==16);
    for(int mv=1300;mv<=2200;mv+=10) {
        rawMv=mv; reads=0;
        const int percent=control_battery_percent();
        assert(percent>=0 && percent<=100 && percent%5==0);
    }
}
'''
with tempfile.TemporaryDirectory(prefix="control-battery-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: calibrated battery, divider scaling, timeouts, invalid readings setup cleanup, periodic refresh and immediate measurement after touch.")
