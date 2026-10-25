#!/usr/bin/env python3
"""Exercise the production serial frame allocation and failed-init cleanup."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
serial = (root / "main/config/control_config_serial.cpp").read_text()
allocator = serial[serial.index("void *configJsonAllocate("):serial.index("void configureJsonAllocator(")]
cleanup = serial[serial.index("esp_err_t initFailure("):serial.index("SerialTransport configuredTransport(")]
initialize = serial[serial.index("    g_configs = static_cast<UiConfigCollection *>"):serial.index("    memset(g_pending,")]
header = (root / "main/storage/ui_config.h").read_text()
types = header[header.index("constexpr uint16_t"):header.index("esp_err_t control_ui_config_load")]
configs = serial[serial.index("bool ensureConfigs("):serial.index("bool getDevices(")]
source = r'''
#include <cassert>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <new>
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_NO_MEM=1;
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
constexpr size_t MAX_FRAME=8192;
''' + types + r'''
alignas(UiConfigCollection) unsigned char memory[3][sizeof(UiConfigCollection)+MAX_FRAME+16];
bool allocated[3]{};
int failAt=0, calls=0;
int deleted=0;
void* heap_caps_malloc(size_t size,int caps) {
    assert((size==MAX_FRAME || size==sizeof(UiConfigCollection)) && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(++calls==failAt) return nullptr;
    for(int i=0;i<3;++i) if(!allocated[i]) {
        allocated[i]=true; memset(memory[i],0xa5,sizeof(memory[i])); return memory[i]+8;
    }
    assert(false); return nullptr;
}
void heap_caps_free(void* pointer) {
    if(!pointer) return;
    for(int i=0;i<3;++i) if(pointer==memory[i]+8) {
        assert(allocated[i]); allocated[i]=false; return;
    }
    assert(false);
}
UiConfigCollection *g_configs=nullptr,*g_verified_configs=nullptr;
bool g_configs_loaded=false;
char* g_line=nullptr;
size_t g_line_length=13;
bool g_line_discard=true;
int g_input_fd=1;
void *g_results=nullptr,*g_responses=nullptr,*g_web_responses=nullptr,*g_requests=nullptr;
int g_dispatch_request_id=1;
char g_dispatch_cmd[32]="test";
void logInitHeap(const char*) {}
void vQueueDelete(void* queue) { assert(queue); ++deleted; }
''' + allocator + cleanup + r'''
int initialize() {
''' + initialize + r'''
    return ESP_OK;
}
bool failLoad=false;
int control_ui_config_collection_load(UiConfigCollection& out) {
    if(failLoad) return ESP_FAIL;
    out.config_count=1; out.active_config_id=7; out.configs[0].config_id=7;
    return ESP_OK;
}
''' + configs + r'''
int main() {
    assert(initialize()==ESP_OK);
    assert(g_configs!=g_verified_configs && g_configs->schema_version==1 && g_verified_configs->schema_version==1);
    for(size_t i=0;i<sizeof(memory[2]);++i)
        assert(memory[2][i]==(i>=8 && i<MAX_FRAME+8 ? 0 : 0xa5));
    assert(ensureConfigs() && activeConfig()==&g_configs->configs[0]);
    assert(configById(7) && !configById(8));
    g_configs_loaded=false; failLoad=true;
    assert(ensureConfigs() && activeConfig()->config_id==1);
    assert(!strcmp(activeConfig()->name,"Maison"));
    assert(g_verified_configs->config_count==0);
    g_line[MAX_FRAME-1]='x';
    g_requests=(void*)1; g_responses=(void*)2;
    g_results=(void*)3; g_web_responses=(void*)4;
    assert(initFailure("transport","test",1)==ESP_FAIL);
    assert(!allocated[0] && !allocated[1] && !allocated[2] && !g_line && !g_line_length && !g_line_discard);
    assert(!g_configs && !g_verified_configs && !g_configs_loaded);
    assert(deleted==4 && !g_requests && !g_results && !g_responses && !g_web_responses);
    for(failAt=1;failAt<=3;++failAt) {
        calls=0;
        assert(initialize()==ESP_FAIL && !g_line && !g_configs && !g_verified_configs && deleted==4);
        assert(!allocated[0] && !allocated[1] && !allocated[2]);
    }
    failAt=calls=0;
    assert(initialize()==ESP_OK);
    initFailure("task","test",1);
    assert(!allocated[0] && !allocated[1] && !allocated[2] && !g_line);
}
'''
with tempfile.TemporaryDirectory(prefix="control-config-buffer-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: PSRAM configuration copies, receive bounds and cleanup at every allocation failure.")
