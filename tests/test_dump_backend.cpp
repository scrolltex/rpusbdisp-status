#include <doctest/doctest.h>
#include "backend/dump_backend.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace status;

namespace {
// Reads pixel (x, y) from a P6 PPM as 0xRRGGBB.
uint32_t ppm_pixel(const std::string& path, int x, int y) {
    std::ifstream f(path, std::ios::binary);
    std::string magic;
    int w = 0, h = 0, maxv = 0;
    f >> magic >> w >> h >> maxv;
    f.get();
    std::vector<unsigned char> data(size_t(w) * h * 3);
    f.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
    size_t i = (size_t(y) * w + x) * 3;
    return (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
}

void pump(int n = 5) {
    for (int i = 0; i < n; ++i) {
        lv_tick_inc(40);
        lv_timer_handler();
    }
}

void clicked_cb(lv_event_t* e) { *static_cast<bool*>(lv_event_get_user_data(e)) = true; }
}  // namespace

TEST_CASE("dump backend with 180 rotation puts logical top-left at physical bottom-right") {
    DumpBackend b("/tmp");
    REQUIRE(b.init(LV_DISPLAY_ROTATION_180));
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t* r = lv_obj_create(scr);
    lv_obj_remove_style_all(r);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(0xFF0000), 0);
    lv_obj_set_pos(r, 0, 0);
    lv_obj_set_size(r, 20, 20);
    pump();

    const std::string path = "/tmp/status-test-rot.ppm";
    REQUIRE(b.write_ppm(path));
    CHECK(ppm_pixel(path, 315, 235) == 0xFF0000);  // physical bottom-right
    CHECK(ppm_pixel(path, 5, 5) == 0x000000);      // physical top-left
    std::remove(path.c_str());

    // Physical touch near bottom-right hits the logical top-left button.
    bool clicked = false;
    lv_obj_set_clickable(r, true);
    lv_obj_add_event_cb(r, clicked_cb, LV_EVENT_CLICKED, &clicked);
    b.touch(309, 229, true);
    pump(3);
    b.touch(309, 229, false);
    pump(3);
    CHECK(clicked);

    lv_display_delete(lv_display_get_default());
}
