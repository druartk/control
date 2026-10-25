#!/usr/bin/env python3
"""Exercise pulse copying, register-only frame start and partial-region timing."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
driver = (root / "components/lilygo_epd47/upstream/epd_driver.c").read_text()
pulse = (root / "components/lilygo_epd47/upstream/rmt_pulse.c").read_text()
pulse = pulse[pulse.index("void IRAM_ATTR pulse_ckv_wait("):pulse.index("void IRAM_ATTR pulse_ckv_us(")]
skip = driver[driver.index("static void skip_row(uint32_t pipeline_finish_time)\n{"):driver.index("static void reorder_line_buffer(uint32_t *line_data)\n{")]
push = driver[driver.index("void epd_push_pixels("):driver.index("void epd_clear_area(")]
source = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>
#define IRAM_ATTR
#define ESP_ERROR_CHECK(expr) assert((expr)==0)
constexpr int portMAX_DELAY=-1;
struct rmt_item32_t { int level0,duration0,level1,duration1; };
struct { int channel=1; } row_rmt_config;
std::deque<rmt_item32_t> pending;
std::vector<rmt_item32_t> pulses;
int waits=0;
int RMT=0, polls=0;
bool interruptEnabled=true;
uint32_t rawStatus=0;
rmt_item32_t preloaded[2];
constexpr uint32_t RMT_LL_EVENT_TX_DONE(int channel) { return 1U<<channel; }
int rmt_set_tx_intr_en(int channel,bool enabled) { assert(channel==1); interruptEnabled=enabled; return 0; }
int rmt_fill_tx_items(int channel,const rmt_item32_t* data,int count,int offset) {
    assert(channel==1 && count==2 && offset==0);
    std::memcpy(preloaded,data,sizeof(preloaded)); return 0;
}
void rmt_ll_tx_reset_pointer(int*,int channel) { assert(channel==1); }
void rmt_ll_clear_interrupt_status(int*,uint32_t mask) { rawStatus &= ~mask; }
void rmt_ll_tx_start(int*,int channel) {
    assert(channel==1 && !interruptEnabled);
    assert(preloaded[0].level0==1 && preloaded[0].duration0==100);
    assert(preloaded[0].level1==0 && preloaded[0].duration1==100);
    assert(preloaded[1].duration0==0 && preloaded[1].duration1==0);
    rawStatus=RMT_LL_EVENT_TX_DONE(channel); polls=0;
}
uint32_t rmt_ll_tx_get_interrupt_status_raw(int*,int channel) {
    assert(channel==1); return ++polls<3 ? 0 : rawStatus;
}
void completePulse() {
    pulses.push_back(pending.front()); pending.pop_front();
}
int rmt_wait_tx_done(int,int) {
    ++waits;
    while(!pending.empty()) completePulse();
    return 0;
}
int rmt_write_items(int channel,const rmt_item32_t* data,int count,bool wait) {
    assert(channel==1 && count==1);
    while(!pending.empty()) completePulse();
    pending.push_back(*data);
    if(wait) return rmt_wait_tx_done(channel,portMAX_DELAY);
    return 0;
}
''' + pulse + r'''
constexpr int EPD_HEIGHT=540, EPD_LINE_BYTES=240;
constexpr int CLEAR_BYTE=0xAA, DARK_BYTE=0x55;
struct Rect_t { int x,y,width,height; };
uint32_t skipping=0;
uint8_t buffer[EPD_LINE_BYTES];
std::vector<uint32_t> times;
int skipped=0;
void epd_switch_buffer() {}
uint8_t* epd_get_current_buffer() { return buffer; }
void epd_output_row(uint32_t time) { times.push_back(time); }
void epd_skip() { ++skipped; }
void epd_start_frame() { times.clear(); skipped=0; }
void epd_end_frame() {}
void write_row(uint32_t time) { skipping=0; epd_output_row(time); }
''' + skip + push + r'''
int main() {
    for(int i=0;i<30;++i) {
        pulse_ckv_ticks(300+i,50,false);
        assert(waits==0 && !pending.empty() && "row pulses must overlap LCD data transfer");
    }
    pulse_ckv_ticks(0,100,true);
    assert(waits==1 && pending.empty() && pulses.size()==31);
    for(int i=0;i<30;++i) assert(pulses[i].duration0==300+i && pulses[i].duration1==50);
    assert(pulses.back().duration0==100);
    assert(pulses.back().duration1==0 && "factory frame pulse must keep its stop marker");
    pulse_ckv_prepare_frame_start();
    const int waitsBeforeStart=waits;
    pulse_ckv_start_frame();
    assert(waits==waitsBeforeStart && pending.empty() && "critical start must use only hardware registers");
    pulse_ckv_finish_frame_start();
    assert(polls==3 && rawStatus==0 && waits==waitsBeforeStart);
    // The final grayscale pass uses 300 ticks, which must survive row skipping.
    skipping=0; times.clear(); skip_row(300);
    assert(times.size()==1 && times[0]==300);
    skip_row(300); skip_row(300);
    assert(times.size()==2 && times[1]==10 && skipped==1);
    // The clear API takes microseconds; the pipeline flush takes tenths of us.
    for(int y : {0,175,539}) {
        skipping=0;
        epd_push_pixels({32,y,285,1},50,1);
        assert(times.size()>1 && times[0]==500);
        if(y<539) assert(times[times.size()-3]==500);
    }
}
'''
with tempfile.TemporaryDirectory(prefix="control-epd-timing-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-Wno-sign-compare", "-Wno-missing-field-initializers",
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: pulse payload lifetime, 300-tick row flush and clear timing.")
