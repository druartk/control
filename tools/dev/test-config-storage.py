#!/usr/bin/env python3
"""Run production configuration storage against NVS mocks with concurrent readers."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
storage = (root / 'main/storage/ui_config.cpp').read_text()
storage = re.sub(r'^#include .*\n', '', storage, flags=re.M)
header = (root / 'main/storage/ui_config.h').read_text()
types = header[header.index('constexpr uint16_t'):header.index('esp_err_t control_ui_config_load')]
source = r'''
#include <cassert>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NOT_FOUND=2;
constexpr int ESP_ERR_INVALID_VERSION=3, ESP_ERR_INVALID_SIZE=4, ESP_ERR_INVALID_CRC=5, ESP_ERR_INVALID_ARG=6;
constexpr int ESP_FAIL=7;
constexpr int NVS_READONLY=0, NVS_READWRITE=1, portMAX_DELAY=-1;
using StaticSemaphore_t=std::recursive_mutex;
using SemaphoreHandle_t=StaticSemaphore_t*;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* mutex) { return mutex; }
int xSemaphoreTakeRecursive(SemaphoreHandle_t mutex,int) { mutex->lock(); return 1; }
int xSemaphoreGiveRecursive(SemaphoreHandle_t mutex) { mutex->unlock(); return 1; }
template<class... Args> void log(Args...) {}
#define ESP_LOGW(...) log(__VA_ARGS__)
#define ESP_LOGI(...) log(__VA_ARGS__)
const char* esp_err_to_name(int) { return "test"; }
using nvs_handle_t=int;
std::map<std::string,std::vector<uint8_t>> blobs;
std::atomic<int> sessions{0};
int erases=0;
const char* failReadKey=nullptr;
int nvs_open(const char*,int,nvs_handle_t* handle) {
    assert(sessions.fetch_add(1)==0 && "configuration NVS accesses must be serialized");
    *handle=1; return ESP_OK;
}
void nvs_close(nvs_handle_t) { assert(sessions.fetch_sub(1)==1); }
int nvs_commit(nvs_handle_t) { std::this_thread::yield(); return ESP_OK; }
int nvs_get_blob(nvs_handle_t,const char* key,void* out,size_t* size) {
    if(failReadKey && !strcmp(key,failReadKey)) return ESP_FAIL;
    auto found=blobs.find(key);
    if(found==blobs.end()) return ESP_ERR_NVS_NOT_FOUND;
    const size_t actual=found->second.size();
    if(!out) { *size=actual; return ESP_OK; }
    if(*size<actual) return ESP_ERR_INVALID_SIZE;
    memcpy(out,found->second.data(),actual); *size=actual;
    std::this_thread::yield(); return ESP_OK;
}
int nvs_set_blob(nvs_handle_t,const char* key,const void* data,size_t size) {
    const auto* bytes=static_cast<const uint8_t*>(data);
    blobs[key]=std::vector<uint8_t>(bytes,bytes+size);
    std::this_thread::yield(); return ESP_OK;
}
int nvs_erase_key(nvs_handle_t,const char* key) { blobs.erase(key); return ESP_OK; }
int nvs_erase_all(nvs_handle_t) { ++erases; blobs.clear(); return ESP_OK; }
''' + types + storage + r'''
int main() {
    UiConfigCollection collection;
    collection.config_count=1; collection.active_config_id=1;
    auto& named=collection.configs[0]; named.config_id=1; strcpy(named.name,"Home");
    named.ui.room_count=1; named.ui.rooms[0].room_id=10; strcpy(named.ui.rooms[0].name,"Kitchen");
    assert(control_ui_config_collection_save(collection)==ESP_OK);
    std::thread writer([&] {
        for(int i=0;i<100;++i) {
            named.revision=i;
            named.ui.rooms[0].room_id=i%2 ? 10 : 20;
            strcpy(named.ui.rooms[0].name,i%2 ? "Kitchen" : "Bedroom");
            assert(control_ui_config_collection_save(collection)==ESP_OK);
        }
    });
    std::vector<std::thread> readers;
    for(int t=0;t<3;++t) readers.emplace_back([] {
        for(int i=0;i<100;++i) {
            UiConfig config;
            assert(control_ui_config_load(config)==ESP_OK);
            assert(config.room_count==1);
            assert(!strcmp(config.rooms[0].name,config.rooms[0].room_id==10 ? "Kitchen" : "Bedroom"));
            UiConfigCollection readback;
            assert(control_ui_config_collection_load(readback)==ESP_OK);
            assert(readback.config_count==1 && readback.active_config_id==1);
        }
    });
    writer.join(); for(auto& reader:readers) reader.join();
    assert(erases==0 && sessions==0);
    const auto saved=blobs;
    UiConfig active; UiConfigCollection readback;
    failReadKey="cfgmeta";
    assert(control_ui_config_load(active)==ESP_FAIL);
    assert(control_ui_config_collection_load(readback)==ESP_FAIL);
    assert(blobs==saved && erases==0);
    failReadKey=nullptr;
    blobs["cfgmeta"][0]^=1;
    assert(control_ui_config_load(active)==ESP_ERR_INVALID_CRC);
    assert(control_ui_config_collection_load(readback)==ESP_ERR_INVALID_CRC);
    blobs=saved; blobs.erase("cfg0");
    assert(control_ui_config_load(active)==ESP_ERR_INVALID_CRC);
    assert(control_ui_config_collection_load(readback)==ESP_ERR_INVALID_CRC);
    assert(erases==0 && blobs.count("config"));
    blobs=saved;
    CollectionMeta invalidMeta{};
    memcpy(&invalidMeta,blobs["cfgmeta"].data(),sizeof(invalidMeta));
    invalidMeta.active_config_id=999;
    invalidMeta.crc32=crc32(reinterpret_cast<const uint8_t*>(&invalidMeta),offsetof(CollectionMeta,crc32));
    nvs_set_blob(1,"cfgmeta",&invalidMeta,sizeof(invalidMeta));
    assert(control_ui_config_collection_load(readback)==ESP_ERR_INVALID_CRC);
    blobs=saved;
    // Legacy migration calls the loader recursively and must not deadlock.
    blobs.erase("cfgmeta");
    UiConfigCollection migrated;
    assert(control_ui_config_collection_load(migrated)==ESP_OK);
    assert(migrated.config_count==1 && migrated.configs[0].ui.room_count==1);
    const auto legacy=blobs;
    failReadKey="config";
    assert(control_ui_config_load(active)==ESP_FAIL);
    assert(control_ui_config_collection_load(readback)==ESP_FAIL);
    assert(erases==0 && blobs==legacy);
    failReadKey=nullptr;
    blobs["config"][0]^=1;
    assert(control_ui_config_load(active)==ESP_ERR_INVALID_VERSION);
    assert(control_ui_config_collection_load(readback)==ESP_ERR_INVALID_VERSION);
    assert(erases==0 && blobs.size()==legacy.size());
    blobs.clear();
    assert(control_ui_config_load(active)==ESP_ERR_NOT_FOUND);
    assert(control_ui_config_collection_load(readback)==ESP_ERR_NOT_FOUND);
    assert(erases==0);
    assert(control_ui_config_clear()==ESP_OK && blobs.empty());
}
'''
with tempfile.TemporaryDirectory(prefix='control-config-storage-') as directory:
    cpp = Path(directory) / 'test.cpp'
    exe = Path(directory) / 'test'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-class-memaccess', '-pthread', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=20)
print('PASS: simultaneous saves and readers preserve complete configurations; recursive migration and reset.')
