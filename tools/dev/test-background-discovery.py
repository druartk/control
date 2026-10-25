#!/usr/bin/env python3
"""Exercise the production Matter status routing without ESP hardware."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
app = (root / "main/main.cpp").read_text()
matter = (root / "main/matter/matter_controller.h").read_text()
status = re.search(r"enum class MatterPostCommissioningStatus \{.*?\};", matter, re.S)[0]
start = app.index("            switch (post.status)")
routing = app[start:app.index("\n        } else if (post.status", start)]
source = r'''
#include <cassert>
#include <initializer_list>
#include "app_state.h"
#define ESP_LOGI(...) ((void)0)
''' + status + r'''
struct Application {
    bool dashboardAvailable_=true, batchCommandActive_=false;
    AppState state_=AppState::WEATHER_HOME;
    struct Ui {
        bool onboarding=false;
        bool onboardingActive() { return onboarding; }
    } controlUi_;
    void transition(AppState next) { state_=next; }
    void normalizeMatterSelection() {}
    bool startNextRoomCommand() { return false; }
    void process(MatterPostCommissioningStatus status) {
        struct { MatterPostCommissioningStatus status; } post{status};
''' + routing + r'''
    }
};
int main() {
    using Status = MatterPostCommissioningStatus;
    for (auto failure : {Status::CONNECTION_FAILED, Status::DISCOVERY_FAILED}) {
        const auto errorScreen=failure==Status::CONNECTION_FAILED ?
            AppState::MATTER_CONNECTION_FAILED : AppState::MATTER_DISCOVERY_FAILED;
        Application app;
        app.process(failure);
        assert(app.state_==AppState::WEATHER_HOME);
        app.dashboardAvailable_=false;
        app.process(failure);
        assert(app.state_==errorScreen);
        app.dashboardAvailable_=true;
        app.state_=AppState::MATTER_DEVICE_DISCOVERY;
        app.process(failure);
        assert(app.state_==errorScreen);
        app.controlUi_.onboarding=true;
        app.state_=AppState::MATTER_DEVICE_DISCOVERY;
        app.process(failure);
        assert(app.state_==AppState::MATTER_DEVICE_DISCOVERY);
    }
    Application app;
    app.process(Status::SAVED_NODE_INVALID);
    assert(app.state_==AppState::MATTER_SAVED_INVALID);
}
'''
with tempfile.TemporaryDirectory(prefix="control-background-discovery-") as temp:
    cpp = Path(temp) / "test.cpp"
    exe = Path(temp) / "test"
    cpp.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "main"),
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: background discovery keeps the dashboard; explicit errors and invalid saved nodes remain visible.")
