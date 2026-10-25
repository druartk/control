#!/usr/bin/env python3
"""Exercise production Wi-Fi authorization, USB/Wi-Fi routing and bounded JSON bridge."""
import os
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
serial = (root / 'main/config/control_config_serial.cpp').read_text()
web = (root / 'main/config/control_config_web.cpp').read_text()
header = (root / 'main/matter/matter_controller.h').read_text()
cjson = Path(os.environ.get('IDF_PATH', Path.home() / 'esp/esp-idf-v5.4.2')) / 'components/json/cJSON'
types = '\n'.join(re.search(pattern, header, re.S)[0] for pattern in (
    r'enum class MatterUiAction.*?\};', r'struct MatterUiCommand \{.*?\};', r'struct MatterUiResult \{.*?\};'))
structs = '\n'.join(re.search(rf'struct {name} \{{.*?\n\}};', serial, re.S)[0]
                    for name in ('ConfigRequest', 'ConfigResponse', 'PendingRequest'))
pending = serial[serial.index('const cJSON *field('):serial.index('bool ensureConfigs(')]
commands = serial[serial.index('bool enqueueMatter('):serial.index('void startIdentify(')]
complete = serial[serial.index('void handleResult('):serial.index('void progressIdentify(')]
bridge = serial[serial.index('esp_err_t control_config_serial_web_request('):serial.index('void control_config_serial_handle_matter_result(')]
auth = web[web.index('bool authorized('):web.index('esp_err_t serveAsset(')]
http_handler = web[web.index('esp_err_t handleJson('):web.index('\n}\n}\n\nesp_err_t control_config_web_init()') + 2]
source = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>
#include "cJSON.h"
constexpr int MALLOC_CAP_SPIRAM=1, MALLOC_CAP_8BIT=2;
void* heap_caps_malloc(size_t size,int caps) { assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); return malloc(size); }
void heap_caps_free(void* pointer) { free(pointer); }
using esp_err_t=int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=1, ESP_ERR_INVALID_STATE=2, ESP_ERR_INVALID_SIZE=3;
constexpr int ESP_ERR_NO_MEM=4, ESP_ERR_TIMEOUT=5, pdTRUE=1;
constexpr size_t MAX_FRAME=8192, MAX_PENDING=8, MAX_COMMAND_LENGTH=32;
constexpr uint32_t CONTROL_CONFIG_UI_DEVICE_MARKER=0xF0000000;
#define pdMS_TO_TICKS(x) (x)
const char* esp_err_to_name(int) { return "test"; }
int64_t now=0;
int64_t esp_timer_get_time() { return now; }
''' + types + structs + r'''
PendingRequest g_pending[MAX_PENDING]{};
uint32_t g_dispatch_web_request=0, g_next_command_id=0;
int lastRoute=-1; uint32_t lastRequest=0;
void respond(cJSON* object,uint32_t id) {
    lastRoute=g_dispatch_web_request; lastRequest=id; cJSON_Delete(object);
}
MatterUiCommand lastCommand;
int matter_controller_ui_enqueue(const MatterUiCommand& command) { lastCommand=command; return ESP_OK; }
''' + pending + commands + complete + r'''
void* g_task=(void*)1;
void* g_requests=(void*)2;
void* g_web_responses=(void*)3;
std::deque<ConfigResponse> replies;
bool queueFull=false, omitResponse=false;
ConfigRequest lastQueued;
int xQueueSend(void* queue, const ConfigRequest* request, int) {
    assert(queue==g_requests);
    if(queueFull) return 0;
    lastQueued=*request;
    if(!omitResponse) {
        for(auto type : {"stale", "command_queued", "status"}) {
            ConfigResponse reply;
            reply.web_request=type==std::string("stale") ? request->web_request-1 : request->web_request;
            strcpy(reply.type,type);
            // Each queued response owns its string, including discarded responses.
            reply.json=(char*)malloc(3); strcpy(reply.json,"{}"); replies.push_back(reply);
        }
    }
    free(request->json);
    return pdTRUE;
}
int xQueueReceive(void* queue,ConfigResponse* response,int) {
    assert(queue==g_web_responses); now+=100000;
    if(replies.empty()) return 0;
    *response=replies.front(); replies.pop_front(); return pdTRUE;
}
''' + bridge + r'''
char accessCode[17]="0123456789abcdef";
struct httpd_req_t {
    std::string authorization, contentType="application/json", body, status, response;
    size_t content_len=0, offset=0;
    explicit httpd_req_t(std::string value) : authorization(value) {}
};
size_t httpd_req_get_hdr_value_len(httpd_req_t* request,const char*) { return request->authorization.size(); }
int httpd_req_get_hdr_value_str(httpd_req_t* request,const char* header,char* buffer,size_t size) {
    const auto& value=std::string(header)=="Authorization" ? request->authorization : request->contentType;
    if(value.size()+1>size) return ESP_ERR_INVALID_SIZE;
    strcpy(buffer,value.c_str()); return ESP_OK;
}
constexpr size_t MAX_JSON=MAX_FRAME-7;
constexpr int HTTPD_RESP_USE_STRLEN=-1, HTTPD_400_BAD_REQUEST=400, HTTPD_SOCK_ERR_TIMEOUT=-2;
void vTaskDelay(int ticks) { assert(ticks==250); }
int httpd_resp_set_hdr(httpd_req_t*,const char*,const char*) { return ESP_OK; }
int httpd_resp_set_type(httpd_req_t*,const char*) { return ESP_OK; }
int httpd_resp_set_status(httpd_req_t* request,const char* status) { request->status=status; return ESP_OK; }
int httpd_resp_send(httpd_req_t* request,const char* text,int) { request->response=text; return ESP_OK; }
int httpd_resp_send_err(httpd_req_t* request,int code,const char* text) {
    request->status=std::to_string(code); request->response=text; return ESP_OK;
}
int httpd_resp_send_408(httpd_req_t* request) { request->status="408"; return ESP_OK; }
int httpd_req_recv(httpd_req_t* request,char* out,size_t size) {
    if(request->offset==request->body.size()) return HTTPD_SOCK_ERR_TIMEOUT;
    size_t count=std::min(size,std::min(size_t{3},request->body.size()-request->offset));
    memcpy(out,request->body.data()+request->offset,count); request->offset+=count; return count;
}
''' + auth + http_handler + r'''
int main() {
    for(const char* value : {"", "Bearer ", "Bearer 0123456789abcdee", "Basic 0123456789abcdef", "Bearer 0123456789abcdefX"}) {
        httpd_req_t request{value}; assert(!authorized(&request));
    }
    httpd_req_t request{"Bearer 0123456789abcdef"}; assert(authorized(&request));
    accessCode[0]=0; assert(!authorized(&request));
    auto* usb=newPending(1); assert(usb && usb->web_request==0);
    usb->node_id=123; usb->endpoint=10;
    g_dispatch_web_request=10;
    auto* wifi=newPending(1); assert(wifi && wifi!=usb && wifi->web_request==10);
    assert(!newPending(1));
    wifi->node_id=123; wifi->endpoint=11;
    assert(enqueueMatter(*usb,MatterUiAction::SetOff)); const auto usbCommand=lastCommand;
    assert(enqueueMatter(*wifi,MatterUiAction::SetOn)); const auto wifiCommand=lastCommand;
    assert(usbCommand.batch_id!=wifiCommand.batch_id);
    MatterUiResult result; result.command=usbCommand; result.success=true;
    handleResult(result); assert(lastRoute==0 && lastRequest==1 && !usb->used && wifi->used);
    assert(g_dispatch_web_request==10);
    result.command=wifiCommand; result.success=false;
    handleResult(result); assert(lastRoute==10 && !wifi->used);
    // A completed old command cannot complete a new request reusing the client ID.
    auto* reconnect=newPending(1); assert(reconnect);
    assert(enqueueMatter(*reconnect,MatterUiAction::SetOff));
    handleResult(result); assert(reconnect->used);
    char* response=nullptr;
    assert(control_config_serial_web_request("{}",2,&response)==ESP_ERR_INVALID_ARG);
    const char* json="{\"cmd\":\"get_status\",\"request_id\":1}";
    assert(control_config_serial_web_request(json,MAX_FRAME,&response)==ESP_ERR_INVALID_SIZE);
    assert(control_config_serial_web_request(json,strlen(json),nullptr)==ESP_ERR_INVALID_ARG);
    assert(control_config_serial_web_request(json,strlen(json),&response)==ESP_OK);
    assert(response && std::string(response)=="{}" && replies.empty()); cJSON_free(response);
    assert(lastQueued.web_request!=0 && lastQueued.request_id==1);
    const auto previousRequest=lastQueued.web_request;
    queueFull=true;
    assert(control_config_serial_web_request(json,strlen(json),&response)==ESP_ERR_NO_MEM && !response);
    queueFull=false; omitResponse=true;
    assert(control_config_serial_web_request(json,strlen(json),&response)==ESP_ERR_TIMEOUT && !response);
    assert(lastQueued.web_request>previousRequest);
    omitResponse=false;
    httpd_req_t http{"Bearer 0123456789abcdef"};
    strcpy(accessCode,"0123456789abcdef");
    http.body=json; http.content_len=http.body.size();
    assert(handleJson(&http)==ESP_OK && http.response=="{}" && http.offset==http.content_len);
    http.offset=0; http.response.clear(); http.authorization="";
    assert(handleJson(&http)==ESP_OK && http.status=="401 Unauthorized" && http.offset==0);
    http.authorization="Bearer 0123456789abcdef"; http.contentType="text/plain";
    assert(handleJson(&http)==ESP_OK && http.status=="400" && http.offset==0);
    http.contentType="application/json"; http.content_len=8192;
    assert(handleJson(&http)==ESP_OK && http.status=="400" && http.offset==0);
    http.body="{bad"; http.content_len=http.body.size();
    assert(handleJson(&http)==ESP_OK && http.status=="400 Bad Request");
    http.offset=0; http.content_len=http.body.size()+1;
    assert(handleJson(&http)==ESP_FAIL && http.status=="408");
}
'''
with tempfile.TemporaryDirectory(prefix='control-web-test-') as directory:
    directory = Path(directory)
    cpp = directory / 'test.cpp'; obj = directory / 'cJSON.o'; exe = directory / 'test'
    cpp.write_text(source)
    subprocess.run(['cc', '-I', str(cjson), '-c', str(cjson / 'cJSON.c'), '-o', str(obj)], check=True)
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(cjson), str(cpp), str(obj), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: Wi-Fi authentication, simultaneous USB/Wi-Fi IDs, late results, bounds, queue pressure and timeouts.')
