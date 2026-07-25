#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include "board/board_config.h"
#include "esp_err.h"
#include "sdkconfig.h"

struct UiRect {
    int x;
    int y;
    int width;
    int height;

    bool contains(int px, int py) const {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
};

class DisplayManager {
public:
    static constexpr int WIDTH = CONTROL_BOARD_DISPLAY_WIDTH;
    static constexpr int HEIGHT = CONTROL_BOARD_DISPLAY_HEIGHT;

    bool initialize();
    bool available() const { return available_; }
    bool busy() const { return refreshState_ != RefreshState::READY; }
    const void *framebufferAddress() const { return framebuffer_; }
    const char *statusText() const { return status_.c_str(); }

    void beginFrame();
    void title(const std::string &text);
    void text(int x, int baseline, const std::string &text);
    void multiline(int x, int baseline, const std::string &text, int lineSpacing = 52);
    void button(const UiRect &rect, const std::string &label, bool inverted = false);
    void rectangle(const UiRect &rect, bool filled = false);
    void fillRectangle(const UiRect &rect, uint8_t gray);
    void line(int x0, int y0, int x1, int y1);
    void touchMarker(int x, int y);
    bool commitFull(const char *stateName = nullptr);
    esp_err_t commitRegion(const UiRect &area, const char *reason = "partial_request");

private:
    enum class RefreshState {
        READY,
        FULL_REFRESHING,
        PARTIAL_REFRESHING,
        ERROR,
    };

    uint8_t *framebuffer_ = nullptr;
    bool available_ = false;
    bool firstRender_ = true;
    RefreshState refreshState_ = RefreshState::READY;
    std::string status_ = "Non initialisé";
};
