#!/usr/bin/env python3
"""Exercise the production touch timeout and UI dispatch without ESP hardware."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
touch = (root / "main/touch/touch_manager.cpp").read_text()
ui = (root / "main/ui/control_ui.cpp").read_text()
header = (root / "main/touch/touch_manager.h").read_text()

# Compile the actual methods with only their hardware and UI effects stubbed.
poll = touch[touch.index("TouchEvent TouchManager::poll()"):
             touch.index("TouchDiagnostics TouchManager::diagnostics()")]
dispatch = ui[ui.index("bool ControlUi::handleTouch("):
              ui.index("void ControlUi::activate(")]
enums = "\n".join(re.findall(r"enum class Touch\w+ \{[^}]+\};", header))
source = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#define ESP_LOGI(...) ((void)0)
constexpr int64_t POLL_INTERVAL_US = 30000, RELEASE_TIMEOUT_US = 90000;
constexpr int MOVE_DEBOUNCE_PIXELS = 1, CONTROL_BOARD_TOUCH_IRQ = 47;
int64_t now = 100000;
int64_t esp_timer_get_time() { return now; }
int gpio_get_level(int) { return 0; }
''' + enums + r'''
struct TouchEvent { TouchEventType type = TouchEventType::NONE; int x = 10, y = 10;
                    int64_t timestampUs = now; };
struct TouchManager {
    enum class SampleKind { NO_DATA, POINT, RELEASE, INVALID, IO_ERROR };
    struct TouchSample { SampleKind kind = SampleKind::NO_DATA; int status = 0;
                         int x = 10, y = 10, rawX = 10, rawY = 10; } sample;
    bool available_ = true, irqEdgeObserved_ = false, irqEdgeLogged_ = false;
    bool coordinatesReadable_ = true, contactActive_ = false;
    int64_t nextPollUs_ = 0, lastPointUs_ = 0;
    int lastStatus_ = 0, lastRawX_ = 0, lastRawY_ = 0, lastX_ = 0, lastY_ = 0;
    TouchInternalState internalState_ = TouchInternalState::IDLE;
    TouchSample readSample() { return sample; }
    void updatePipelineStatus() {}
    TouchEvent makeEvent(TouchEventType type, const TouchSample*) { return {type}; }
    TouchEvent poll();
};
struct ControlUi {
    enum class TouchZone { NONE, BUTTON };
    TouchZone pressedZone_ = TouchZone::NONE;
    int pressedScreen_ = 0, screen_ = 0, activations = 0;
    bool pressActive_ = false, pressMoved_ = false;
    int64_t acceptTouchAfterUs_ = 0;
    TouchZone zoneAt(int x, int) { return x < 100 ? TouchZone::BUTTON : TouchZone::NONE; }
    void activate(TouchZone) { ++activations; }
    bool handleTouch(const TouchEvent&);
};
''' + poll + dispatch + r'''
int main() {
    TouchManager touch; ControlUi ui;
    auto send = [&](TouchManager::SampleKind kind, int64_t elapsed) {
        now += elapsed; touch.sample.kind = kind;
        auto event = touch.poll(); ui.handleTouch(event); return event.type;
    };
    assert(send(TouchManager::SampleKind::POINT, 0) == TouchEventType::PRESS);
    assert(send(TouchManager::SampleKind::NO_DATA, 30000) == TouchEventType::NONE);
    assert(send(TouchManager::SampleKind::NO_DATA, 60000) == TouchEventType::CANCEL);
    assert(ui.activations == 0);
    assert(send(TouchManager::SampleKind::RELEASE, 30000) == TouchEventType::NONE);
    assert(ui.activations == 0);
    // A new confirmed tap still activates exactly once.
    assert(send(TouchManager::SampleKind::POINT, 30000) == TouchEventType::PRESS);
    assert(send(TouchManager::SampleKind::RELEASE, 30000) == TouchEventType::RELEASE);
    ui.handleTouch({TouchEventType::RELEASE});
    assert(ui.activations == 1);
    // A tap queued while the old screen refreshed must not toggle the new one.
    TouchEvent queuedPress{TouchEventType::PRESS};
    TouchEvent queuedRelease{TouchEventType::RELEASE};
    ui.acceptTouchAfterUs_ = now;
    assert(!ui.handleTouch(queuedPress));
    assert(!ui.handleTouch(queuedRelease));
    assert(ui.activations == 1);
    now += 30000;
    assert(!ui.handleTouch({TouchEventType::PRESS}));
    assert(ui.handleTouch({TouchEventType::RELEASE}));
    assert(ui.activations == 2);
    assert(send(TouchManager::SampleKind::POINT, 30000) == TouchEventType::PRESS);
    assert(send(TouchManager::SampleKind::IO_ERROR, 90000) == TouchEventType::CANCEL);
    assert(ui.activations == 2);
    ui.handleTouch({TouchEventType::PRESS});
    ui.handleTouch({TouchEventType::MOVE, 200, 10});
    ui.handleTouch({TouchEventType::RELEASE});
    assert(ui.activations == 2);
}
'''

with tempfile.TemporaryDirectory(prefix="control-touch-test-") as temp:
    source_path = Path(temp) / "test.cpp"
    executable = Path(temp) / "test"
    source_path.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(source_path), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: timeouts and I/O errors cancel; confirmed taps activate once; drags cancel.")
