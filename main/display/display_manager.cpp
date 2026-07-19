#include "display/display_manager.h"

#include <cstring>
#include <algorithm>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "epd_driver.h"
#include "firasans.h"

namespace {
constexpr const char *TAG = "CONTROL_DISPLAY";
constexpr size_t FRAMEBUFFER_SIZE = DisplayManager::WIDTH * DisplayManager::HEIGHT / 2;

#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
uint8_t framebufferPixel(const uint8_t *framebuffer, int x, int y) {
    const uint8_t value = framebuffer[y * DisplayManager::WIDTH / 2 + x / 2];
    return (x % 2) ? (value & 0xF0) : static_cast<uint8_t>((value & 0x0F) << 4);
}

void packedSetPixel(uint8_t *buffer, int width, int x, int y, uint8_t gray) {
    uint8_t *target = &buffer[y * ((width + 1) / 2) + x / 2];
    if (x % 2) {
        *target = (*target & 0x0F) | (gray & 0xF0);
    } else {
        *target = (*target & 0xF0) | (gray >> 4);
    }
}
#endif
}

bool DisplayManager::initialize() {
    if (available_) return true;
    framebuffer_ = static_cast<uint8_t *>(heap_caps_malloc(
        FRAMEBUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (framebuffer_ == nullptr) {
        status_ = "Échec allocation PSRAM";
        ESP_LOGE(TAG, "%s", status_.c_str());
        return false;
    }
    memset(framebuffer_, 0xFF, FRAMEBUFFER_SIZE);
    ESP_LOGI(TAG, "framebuffer=%p bytes=%u PSRAM", framebuffer_,
             static_cast<unsigned>(FRAMEBUFFER_SIZE));
    const esp_err_t err = epd_init();
    if (err != ESP_OK) {
        status_ = std::string("Échec pilote e-paper: ") + esp_err_to_name(err);
        ESP_LOGE(TAG, "%s", status_.c_str());
        heap_caps_free(framebuffer_);
        framebuffer_ = nullptr;
        return false;
    }
    available_ = true;
    status_ = "OK (960x540 paysage, V2.4)";
    ESP_LOGI(TAG, "Initialisation e-paper réussie: %s", status_.c_str());
    return true;
}

void DisplayManager::beginFrame() {
    if (framebuffer_) memset(framebuffer_, 0xFF, FRAMEBUFFER_SIZE);
}

void DisplayManager::text(int x, int baseline, const std::string &value) {
    if (!framebuffer_) return;
    int32_t cx = x;
    int32_t cy = baseline;
    writeln(&FiraSans, value.c_str(), &cx, &cy, framebuffer_);
}

void DisplayManager::title(const std::string &value) {
    text(28, 58, value);
    line(24, 72, WIDTH - 24, 72);
}

void DisplayManager::multiline(int x, int baseline, const std::string &value, int lineSpacing) {
    size_t start = 0;
    int y = baseline;
    do {
        const size_t end = value.find('\n', start);
        text(x, y, value.substr(start, end == std::string::npos ? end : end - start));
        y += lineSpacing;
        if (end == std::string::npos) break;
        start = end + 1;
    } while (start <= value.size());
}

void DisplayManager::rectangle(const UiRect &rect, bool filled) {
    if (!framebuffer_) return;
    if (filled) epd_fill_rect(rect.x, rect.y, rect.width, rect.height, 0, framebuffer_);
    else epd_draw_rect(rect.x, rect.y, rect.width, rect.height, 0, framebuffer_);
}

void DisplayManager::fillRectangle(const UiRect &rect, uint8_t gray) {
    if (framebuffer_) epd_fill_rect(rect.x, rect.y, rect.width, rect.height, gray, framebuffer_);
}

void DisplayManager::button(const UiRect &rect, const std::string &label, bool inverted) {
    if (!framebuffer_) return;
    rectangle(rect, inverted);
    int32_t x1 = 0, y1 = 0, w = 0, h = 0;
    int32_t originX = 0, originY = 0;
    get_text_bounds(&FiraSans, label.c_str(), &originX, &originY, &x1, &y1, &w, &h, nullptr);
    FontProperties props{static_cast<uint8_t>(inverted ? 15 : 0),
                         static_cast<uint8_t>(inverted ? 0 : 15), 0, 0};
    int32_t x = rect.x + (rect.width - w) / 2;
    int32_t y = rect.y + (rect.height + FiraSans.ascender) / 2;
    write_mode(&FiraSans, label.c_str(), &x, &y, framebuffer_, BLACK_ON_WHITE, &props);
}

void DisplayManager::line(int x0, int y0, int x1, int y1) {
    if (framebuffer_) epd_draw_line(x0, y0, x1, y1, 0, framebuffer_);
}

void DisplayManager::touchMarker(int x, int y) {
    if (!framebuffer_) return;
    epd_draw_circle(x, y, 18, 0, framebuffer_);
    epd_draw_line(x - 26, y, x + 26, y, 0, framebuffer_);
    epd_draw_line(x, y - 26, x, y + 26, 0, framebuffer_);
}

bool DisplayManager::commitFull(const char *stateName) {
    if (!available_ || !framebuffer_) return false;
    if (refreshState_ != RefreshState::READY) {
        ESP_LOGW(TAG, "full refresh rejected: display busy");
        return false;
    }
    refreshState_ = RefreshState::FULL_REFRESHING;
    if (firstRender_) {
        ESP_LOGI(TAG, "first_render_start");
    } else {
        ESP_LOGI(TAG, "render_start state=%s", stateName ? stateName : "unknown");
    }
    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer_);
    epd_poweroff();
    if (firstRender_) {
        ESP_LOGI(TAG, "first_render_done");
        firstRender_ = false;
    } else {
        ESP_LOGI(TAG, "render_done state=%s", stateName ? stateName : "unknown");
    }
    refreshState_ = RefreshState::READY;
    return true;
}

esp_err_t DisplayManager::commitRegion(const UiRect &area, const char *reason) {
#if CONFIG_CONTROL_ENABLE_PARTIAL_REFRESH
    if (!available_ || !framebuffer_) return ESP_ERR_INVALID_STATE;
    if (refreshState_ != RefreshState::READY) {
        ESP_LOGW(TAG, "partial_refresh skipped: display busy reason=%s",
                 reason ? reason : "unknown");
        return ESP_ERR_INVALID_STATE;
    }
    const int x0 = std::max(0, area.x);
    const int y0 = std::max(0, area.y);
    const int x1 = std::min(WIDTH, area.x + area.width);
    const int y1 = std::min(HEIGHT, area.y + area.height);
    if (x1 <= x0 || y1 <= y0) return ESP_ERR_INVALID_ARG;

    const int width = x1 - x0;
    const int height = y1 - y0;
    const size_t packedBytes = static_cast<size_t>((width + 1) / 2) * height;
    auto *packed = static_cast<uint8_t *>(heap_caps_malloc(packedBytes, MALLOC_CAP_8BIT));
    if (!packed) {
        ESP_LOGE(TAG, "partial refresh allocation failed bytes=%u reason=%s",
                 static_cast<unsigned>(packedBytes), reason ? reason : "unknown");
        return ESP_ERR_NO_MEM;
    }
    memset(packed, 0xFF, packedBytes);
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            packedSetPixel(packed, width, col, row,
                           framebufferPixel(framebuffer_, x0 + col, y0 + row));
        }
    }

    ESP_LOGI(TAG, "partial_refresh_start area=%s x=%d y=%d w=%d h=%d bytes=%u",
             reason ? reason : "unknown", x0, y0, width, height,
             static_cast<unsigned>(packedBytes));
    refreshState_ = RefreshState::PARTIAL_REFRESHING;
    epd_poweron();
    Rect_t rect{x0, y0, width, height};
    epd_draw_grayscale_image(rect, packed);
    epd_poweroff();
    refreshState_ = RefreshState::READY;
    heap_caps_free(packed);
    ESP_LOGI(TAG, "partial_refresh_done area=%s x=%d y=%d w=%d h=%d",
             reason ? reason : "unknown", x0, y0, width, height);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "partial_refresh disabled; region update ignored");
    ESP_LOGI(TAG, "partial refresh disabled; ignored x=%d y=%d w=%d h=%d reason=%s",
             area.x, area.y, area.width, area.height, reason ? reason : "unknown");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}
