#!/usr/bin/env python3
"""Exercise the real control-pin initialization and both ESP32-S3 GPIO banks."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
panel = (root / "components/lilygo_epd47/upstream/ed047tc1.c").read_text()
helpers = panel[panel.index("inline static void fast_gpio_set_hi("):panel.index("inline static void IRAM_ATTR push_cfg_bit(")]
initialize = panel[panel.index("esp_err_t epd_base_init("):panel.index("void epd_poweron(")]
source = r'''
#include <cassert>
#include <cstdint>
using gpio_num_t=int;
using esp_err_t=int;
constexpr int ESP_OK=0, GPIO_MODE_OUTPUT=2, GPIO_PULLUP_DISABLE=0;
constexpr int GPIO_PULLDOWN_DISABLE=0, GPIO_INTR_DISABLE=0;
constexpr int GPIO_DRIVE_CAP_DEFAULT=2;
constexpr int CFG_DATA=13, CFG_CLK=12, CFG_STR=0, CKV=38, STH=40, CKH=41;
constexpr int D0=8,D1=1,D2=2,D3=3,D4=4,D5=5,D6=6,D7=7;
struct Register { uint32_t val=0; };
struct { uint32_t out_w1ts=0,out_w1tc=0; Register out1_w1ts,out1_w1tc; } GPIO;
struct gpio_config_t {
    uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;
};
constexpr uint64_t controls=(1ULL<<13)|(1ULL<<12)|0x1FF|(1ULL<<38)|(1ULL<<40)|(1ULL<<41);
int configResult=0,busResult=0,configCalls=0,busCalls=0,rmtCalls=0,pushCalls=0;
int driveResult=0;
uint64_t driveMask=0;
int gpio_set_drive_capability(int pin,int strength) {
    assert(strength==GPIO_DRIVE_CAP_DEFAULT);
    driveMask|=1ULL<<pin;
    return driveResult;
}
bool digitalPads=false;
int gpio_config(const gpio_config_t* config) {
    ++configCalls;
    assert(config->pin_bit_mask==controls && config->mode==GPIO_MODE_OUTPUT);
    assert(!config->pull_up_en && !config->pull_down_en && !config->intr_type);
    digitalPads=configResult==ESP_OK;
    return configResult;
}
struct {
    bool ep_latch_enable,power_disable,pos_power_enable,neg_power_enable;
    bool ep_stv,ep_scan_direction,ep_mode,ep_output_enable;
} config_reg;
void push_cfg(decltype(config_reg)* config) {
    assert(digitalPads && "control pins must select digital GPIO before shifting");
    assert(config->power_disable && !config->ep_output_enable);
    ++pushCalls;
}
struct i2s_bus_config {
    uint32_t epd_row_width;
    int clock,start_pulse,data_0,data_1,data_2,data_3,data_4,data_5,data_6,data_7;
};
int i2s_bus_init(i2s_bus_config* config) {
    ++busCalls;
    assert(config->epd_row_width==992 && config->clock==41 && config->start_pulse==40);
    assert(config->data_0==8 && config->data_7==7);
    return busResult;
}
int rmt_pulse_init(int pin) { ++rmtCalls; assert(pin==38); return 0; }
''' + helpers + initialize + r'''
int main() {
    for(int pin : {0,12,13,31,32,38,40,48}) {
        GPIO={};
        fast_gpio_set_hi(pin); fast_gpio_set_lo(pin);
        if(pin<32) {
            assert(GPIO.out_w1ts==(1U<<pin) && GPIO.out_w1tc==(1U<<pin));
            assert(!GPIO.out1_w1ts.val && !GPIO.out1_w1tc.val);
        } else {
            assert(!GPIO.out_w1ts && !GPIO.out_w1tc);
            assert(GPIO.out1_w1ts.val==(1U<<(pin-32)));
            assert(GPIO.out1_w1tc.val==(1U<<(pin-32)));
        }
    }
    driveResult=29;
    assert(epd_base_init(960)==29 && !configCalls && !busCalls && !rmtCalls && !pushCalls);
    driveResult=0; configResult=17;
    assert(epd_base_init(960)==17 && !busCalls && !rmtCalls && !pushCalls);
    configResult=0; busResult=23;
    assert(epd_base_init(960)==23 && busCalls==1 && !rmtCalls);
    busResult=0;
    assert(epd_base_init(960)==ESP_OK && busCalls==2 && rmtCalls==1 && configCalls==3);
    assert(driveMask==controls && "all display pads must restore their normal drive strength");
}
'''
source = "#include <initializer_list>\n" + source
with tempfile.TemporaryDirectory(prefix="control-epd-gpio-") as directory:
    cpp = Path(directory) / "test.cpp"
    executable = Path(directory) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    str(cpp), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: digital control-pad setup, error propagation and GPIO banks 0/1.")
