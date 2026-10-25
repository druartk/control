#!/usr/bin/env python3
"""Check real panel clear coordinates and the LCD DMA buffer lifetime."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
driver = (root / "components/lilygo_epd47/upstream/epd_driver.c").read_text()
bus = (root / "components/lilygo_epd47/upstream/i2s_data_bus.c").read_text()
push = driver[driver.index("void epd_push_pixels("):driver.index("void epd_clear_area(")]
reorder_start = driver.index("static void reorder_line_buffer(uint32_t *line_data)\n{")
reorder = driver[reorder_start:driver.index("static void IRAM_ATTR reset_lut(", reorder_start)]
getter = re.search(r"#else\n(volatile uint8_t IRAM_ATTR \*i2s_get_current_buffer\(\).*?)#endif", bus, re.S)[1]
source = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <thread>
#include <vector>
#define IRAM_ATTR
constexpr int EPD_WIDTH=960, EPD_HEIGHT=540, EPD_LINE_BYTES=EPD_WIDTH/4;
constexpr int CLEAR_BYTE=0b10101010, DARK_BYTE=0b01010101;
struct Rect_t { int x,y,width,height; };
alignas(4) uint8_t panelBuffer[EPD_LINE_BYTES]={};
std::vector<std::array<uint8_t,EPD_LINE_BYTES>> rows;
void epd_start_frame() { rows.clear(); }
void epd_end_frame() {}
void epd_switch_buffer() {}
uint8_t* epd_get_current_buffer() { return panelBuffer; }
void skip_row(int) {}
void write_row(int) {
    std::array<uint8_t,EPD_LINE_BYTES> row;
    std::memcpy(row.data(),panelBuffer,EPD_LINE_BYTES); rows.push_back(row);
}
''' + reorder + push + r'''
std::atomic<bool> output_done{false};
uint8_t buffer[248];
''' + getter + r'''
int main() {
    for (auto area : {Rect_t{32,175,285,126}, Rect_t{337,175,285,126},
                      Rect_t{642,319,286,126}, Rect_t{1,1,3,2},
                      Rect_t{959,539,1,1}, Rect_t{0,0,960,540}}) {
        for (int color : {0,1}) {
            epd_push_pixels(area,50,color);
            assert(!rows.empty());
            for(int x=0; x<EPD_WIDTH; ++x) {
                const int byte=(x/4) ^ (USER_I2S_REG ? 2 : 0);
                const int actual=(rows.front()[byte] >> (2*(x%4))) & 3;
                const int expected=x>=area.x && x<area.x+area.width ? (color ? 2 : 1) : 0;
                assert(actual==expected && "clear affects pixels outside the requested rectangle");
            }
        }
    }
    std::atomic<bool> started{false};
    auto writer=std::async(std::launch::async,[&] {
        started=true;
        return i2s_get_current_buffer();
    });
    while(!started) std::this_thread::yield();
    assert(writer.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout &&
           "LCD buffer exposed for writing before DMA completion");
    output_done=true;
    assert(writer.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    assert(writer.get()==buffer);
}
'''
with tempfile.TemporaryDirectory(prefix="control-epd-region-") as temp:
    cpp = Path(temp) / "test.cpp"
    executable = Path(temp) / "test"
    cpp.write_text(source)
    for legacy in (0, 1):
        subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
                        "-Wno-sign-compare", "-Wno-unused-function",
                        f"-DUSER_I2S_REG={legacy}", str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("PASS: clear stays inside every tile; LCD buffer waits for DMA completion.")
