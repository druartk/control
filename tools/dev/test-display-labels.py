#!/usr/bin/env python3
"""Check the production button renderer using the installed font metrics."""
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
display = (root / "main/display/display_manager.cpp").read_text()
font = (root / "components/lilygo_epd47/upstream/firasans.h").read_text()
glyph_text = font.split("const GFXglyph FiraSansGlyphs[] = {")[1].split("};")[0]
glyphs = [tuple(map(int, re.findall(r"-?\d+", line.split("//")[0])))
          for line in glyph_text.splitlines() if "{" in line]
metrics = ",".join("{" + ",".join(map(str, g[:5])) + "}" for g in glyphs[:95])
methods = display[display.index("int DisplayManager::textWidth("):
                  display.index("void DisplayManager::textFitted(")]
button = display[display.index("void DisplayManager::button("):
                 display.index("void DisplayManager::line(")]
ui = (root / "main/ui/control_ui.cpp").read_text()
battery = ui[ui.index("void ControlUi::renderBattery("):ui.index("void ControlUi::renderRooms(")]
home = ui[ui.index("void ControlUi::renderHome("):ui.index("void ControlUi::renderBattery(")]
home_layout = "\n".join(re.search(rf"constexpr (?:int|UiRect) {name}[^;]*;", ui)[0] for name in (
    "SCREEN_MARGIN", "CONTENT_WIDTH", "HOME_ACTION_Y", "HOME_ACTION_HEIGHT",
    "HOME_SETTINGS", "HOME_ALL_OFF", "HOME_ROOMS"))
home_status = "\n".join(line for line in ui.splitlines() if "textFitted(300, 45" in line or "textFitted(480, 45" in line)
source = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
struct Font { int advance_y = 50, ascender = 39; } FiraSans;
struct FontProperties { uint8_t fg, bg; int flags, fallback; };
struct Glyph { int width, height, advance, left, top; };
const Glyph glyphs[] = {''' + metrics + r'''};
void get_text_bounds(const Font*, const char* text, int32_t* x, int32_t*,
                     int32_t* x1, int32_t* y1, int32_t* w, int32_t* h, const void*) {
    int right = 0, top = 0, bottom = 0;
    for (const unsigned char* c = (const unsigned char*)text; *c; ++c) {
        const auto& g = glyphs[*c >= 32 && *c <= 126 ? *c - 32 : '?' - 32];
        right = std::max(right, *x + g.left + g.width);
        *x += g.advance; top = std::max(top, g.top);
        bottom = std::max(bottom, g.height - g.top);
    }
    *x1 = 0; *y1 = -top; *w = right; *h = top + bottom;
}
struct Draw { std::string label; int x, y; bool inverted; };
std::vector<Draw> draws;
constexpr int BLACK_ON_WHITE = 0;
void write_mode(const Font*, const char* label, int32_t* x, int32_t* y,
                uint8_t*, int, const FontProperties* props) {
    draws.push_back({label, *x, *y, props->fg == 15});
}
struct UiRect { int x, y, width, height; };
''' + home_layout + r'''
struct RectDraw { UiRect rect; bool filled; };
std::vector<RectDraw> rectangles;
struct DisplayManager {
    uint8_t pixel = 0; uint8_t* framebuffer_ = &pixel;
    int textWidth(const std::string&) const;
    std::string fitTextToWidth(const std::string&, int) const;
    void rectangle(const UiRect& rect, bool filled=false) { rectangles.push_back({rect,filled}); }
    void textFitted(int x,int y,const std::string& value,int width) {
        assert(textWidth(value)<=width);
        draws.push_back({value,x,y,false});
    }
    void button(const UiRect&, const std::string&, bool = false);
    void title(const std::string&, int) {}
};
''' + methods + button + r'''
struct ControlUi {
    DisplayManager& display_; int batteryPercent_=-1;
    bool wifiConnected_=false; const char* clockText_="00:00";
    void renderHomeStatus() {
''' + home_status + r'''
    }
    void renderBattery();
    void renderHome();
};
''' + battery + home + r'''
int main() {
    DisplayManager display;
    for(const char* label : {"Précédent", "Suivant"}) {
        draws.clear(); display.button({32,460,270,48},label);
        assert(draws.size()==1 && draws[0].label==label);
        assert(draws[0].x>=32 && draws[0].x+display.textWidth(draws[0].label)<=302);
    }
    assert(display.textWidth("Page 6 / 6")<=260);
    for(const std::string glyph : {"é", "€", "💡"}) {
        const std::string expected=glyph+glyph+"...";
        const auto fitted=display.fitTextToWidth(glyph+glyph+glyph+glyph,display.textWidth(expected));
        assert(fitted==expected && "truncate only at complete UTF-8 character boundaries");
    }
    for (bool inverted : {false, true}) {
        draws.clear();
        display.button({32, 175, 285, 126}, "Plafond Bureau", inverted);
        assert(draws.size() == 2);
        assert(draws[0].label == "Plafond" && draws[1].label == "Bureau");
        for (const auto& draw : draws) {
            assert(draw.inverted == inverted);
            assert(draw.x > 32 && draw.x + display.textWidth(draw.label) < 317);
            assert(draw.y - 41 > 175 && draw.y + 12 < 301);
        }
        assert(draws[1].y - draws[0].y >= 53);
    }
    draws.clear(); display.button({0, 0, 285, 126}, "Salon");
    assert(draws.size() == 1 && draws[0].label == "Salon");
    draws.clear(); display.button({0, 0, 285, 58}, "Plafond Bureau");
    assert(draws.size() == 1 && display.textWidth(draws[0].label) <= 245);
    draws.clear(); display.button({0, 0, 285, 126}, "Salle de bain");
    assert(draws.size() == 1 || draws.size() == 2);
    for (const auto& draw : draws) assert(draw.label.find("...") == std::string::npos);
    for (const auto* label : {"Tout éteindre", "Pièces de la maison"}) {
        draws.clear(); display.button({32, 180, 430, 328}, label);
        assert(draws.size()==1 && draws[0].label==label);
        assert(draws[0].y-39>=180 && draws[0].y+12<=508);
    }
    ControlUi ui{display};
    draws.clear(); rectangles.clear(); ui.renderHome();
    assert(draws.size()==3 && rectangles.size()==3);
    assert(draws[0].label=="Configurer les appareils");
    assert(draws[1].label=="Tout éteindre" && draws[2].label=="Pièces de la maison");
    assert(HOME_ALL_OFF.y>HOME_SETTINGS.y+HOME_SETTINGS.height);
    assert(HOME_ALL_OFF.height>=300 && HOME_ROOMS.height>=300);
    assert(HOME_ALL_OFF.x+HOME_ALL_OFF.width<HOME_ROOMS.x);
    for (bool connected : {false,true}) {
        ui.wifiConnected_=connected; draws.clear(); ui.renderHomeStatus();
        assert(draws.size()==2);
        assert(draws[0].label=="00:00");
        assert(draws[1].label==(connected ? "Wi-Fi OK" : "Wi-Fi --"));
        assert(draws[0].x+display.textWidth(draws[0].label)<draws[1].x);
        assert(draws[1].x+display.textWidth(draws[1].label)<740);
        for (const auto& draw : draws) assert(draw.y-39>=4 && draw.y+12<=62);
    }
    for(int percent : {-1,0,5,50,100}) {
        ui.batteryPercent_=percent; rectangles.clear(); draws.clear();
        ui.renderBattery();
        assert(rectangles.size()==(percent>0 ? 3 : 2));
        for(const auto& draw : rectangles) {
            const auto& r=draw.rect;
            assert(r.x>=740 && r.y>=4 && r.x+r.width<=812 && r.y+r.height<=62);
        }
        assert(draws.size()==1);
        assert(draws[0].label==(percent<0 ? "--%" : std::to_string(percent)+"%"));
        assert(draws[0].x+display.textWidth(draws[0].label)<=928);
    }
}
'''
with tempfile.TemporaryDirectory(prefix="control-label-test-") as temp:
    source_path = Path(temp) / "test.cpp"
    executable = Path(temp) / "test"
    source_path.write_text(source)
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(source_path), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: full Plafond Bureau label wraps; both states fit; short buttons stay on one line; clock, Wi-Fi and battery fit in the header.")
