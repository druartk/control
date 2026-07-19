#include "system/control_heap.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

namespace {
constexpr const char *TAG = "CONTROL_HEAP";
}

void control_heap_log(const char *stage) {
    ESP_LOGI(TAG, "%s internal=%u largest_internal=%u psram=%u largest_psram=%u",
             stage ? stage : "-",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

