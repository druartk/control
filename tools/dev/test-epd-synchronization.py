#!/usr/bin/env python3
"""Exercise frame-start preemption and asynchronous row barriers."""
import subprocess
import tempfile
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
panel = (root / 'components/lilygo_epd47/upstream/ed047tc1.c').read_text()
start = panel[re.search(r'void(?: IRAM_ATTR)? epd_start_frame\(\)', panel).start():panel.index('void  epd_skip()')]
rows = panel[panel.index('void  epd_output_row('):panel.index('void  epd_switch_buffer()')]
source = r'''
#include <cassert>
#include <cstdint>
#define IRAM_ATTR
int frame_start_mux=0, criticalDepth=0, deferredInterrupts=0;
bool interruptPending=false, frameStartMissed=false;
void enterCritical(int*) { assert(criticalDepth==0); ++criticalDepth; }
void exitCritical(int*) {
    assert(criticalDepth==1); --criticalDepth;
    if(interruptPending) { ++deferredInterrupts; interruptPending=false; }
}
#define portENTER_CRITICAL(mux) enterCritical(mux)
#define portEXIT_CRITICAL(mux) exitCritical(mux)
struct Config { bool ep_mode=false, ep_output_enable=false, ep_stv=true, ep_latch_enable=false; };
Config config_reg, physical;
bool gatePending=false, dmaPending=false;
int barriers=0, transfers=0;
bool i2s_is_busy() { dmaPending=false; return false; }
void pulse_ckv_wait() { assert(!criticalDepth); gatePending=false; ++barriers; }
void busy_delay(uint32_t) {}
void push_cfg(Config* value) {
    if(!physical.ep_stv && value->ep_stv) {
        assert(!frameStartMissed && "frame-start pulse lost under preemption");
    }
    assert((!gatePending || (physical.ep_mode==value->ep_mode &&
           physical.ep_output_enable==value->ep_output_enable &&
           physical.ep_latch_enable==value->ep_latch_enable)) &&
           "panel controls changed before the row pulse completed");
    physical=*value;
}
bool sawFactoryStart=false;
void pulse_ckv_ticks(uint16_t high,uint16_t low,bool wait) {
    assert((!wait || !criticalDepth) && "blocking waits must remain outside the critical section");
    if(!config_reg.ep_stv && high==100 && low==100) {
        assert(!wait && "factory STV must deassert after CKV starts, before CKV finishes");
        sawFactoryStart=true;
        if(criticalDepth) interruptPending=true;
        else frameStartMissed=true;
    }
    gatePending=!wait;
}
void pulse_ckv_us(uint16_t high,uint16_t low,bool wait) { pulse_ckv_ticks(high*10,low*10,wait); }
void pulse_ckv_prepare_frame_start() { assert(!criticalDepth && !gatePending); }
void pulse_ckv_start_frame() { assert(criticalDepth==1); pulse_ckv_ticks(100,100,false); }
void pulse_ckv_finish_frame_start() { assert(!criticalDepth); pulse_ckv_wait(); }
void i2s_start_line_output() { assert(!dmaPending); dmaPending=true; ++transfers; }
void i2s_switch_buffer() {}
''' + start + rows + r'''
int main(int argc,char**) {
    if(argc==1) {
        epd_start_frame();
        assert(sawFactoryStart && !gatePending && config_reg.ep_output_enable && config_reg.ep_stv);
        assert(!criticalDepth && deferredInterrupts==1);
    } else {
        config_reg.ep_mode=physical.ep_mode=true;
        config_reg.ep_output_enable=physical.ep_output_enable=true;
        if(argc==2) {
            epd_output_row(300);
            assert(gatePending && dmaPending && "row pulse must still overlap LCD data transfer");
            epd_output_row(300);
            assert(transfers==2 && barriers>=2);
        } else {
            gatePending=dmaPending=true;
            epd_end_frame();
            assert(!gatePending && !dmaPending && !config_reg.ep_output_enable && !config_reg.ep_mode);
        }
    }
}
'''
with tempfile.TemporaryDirectory(prefix='control-epd-sync-') as directory:
    cpp=Path(directory)/'test.cpp'; exe=Path(directory)/'test'
    cpp.write_text(source)
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],check=True)
    for args in ([], ['rows'], ['last','row']):
        subprocess.run([str(exe),*args],check=True)
print('PASS: frame start resists preemption without blocking in the critical section; CKV/DMA barriers hold.')
