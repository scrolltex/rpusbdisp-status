#include <doctest/doctest.h>
#include "backend/rpusbdisp_backend.h"

#include <chrono>
#include <vector>

using namespace status;
using namespace std::chrono_literals;

namespace {

struct Recorder : DisplayDevice {
    std::vector<uint16_t>* fb;
    int* blits;
    Recorder(std::vector<uint16_t>* f, int* b) : fb(f), blits(b) {}
    bool alive() override { return true; }
    bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) override {
        ++*blits;
        for (int32_t r = 0; r < h; ++r)
            for (int32_t c = 0; c < w; ++c) (*fb)[size_t(y + r) * 320 + size_t(x + c)] = px[size_t(r) * w + size_t(c)];
        return true;
    }
};

struct Result {
    int blits_for_update = 0;
    std::vector<uint16_t> fb;
};

lv_obj_t* box(int x, int y, uint32_t color) {
    lv_obj_t* o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, 10, 10);
    return o;
}

void pump(RpUsbDispBackend& b, int ms) {
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
        b.poll();
    }
}

// Two small boxes far apart change in the same frame.
Result run(bool coalesce) {
    Result r;
    r.fb.assign(320 * 240, 0);
    int blits = 0;
    RpUsbDispBackend b([&](RpUsbDispBackend::TouchCallback) { return std::make_unique<Recorder>(&r.fb, &blits); },
                       50ms);
    b.set_coalesce(coalesce);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_t* a = box(10, 10, 0x000000);
    lv_obj_t* c = box(300, 220, 0x000000);
    pump(b, 100);

    blits = 0;
    lv_obj_set_style_bg_color(a, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x00FF00), 0);
    pump(b, 50);
    r.blits_for_update = blits;

    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
    return r;
}

}  // namespace

TEST_CASE("coalescing sends one blit per frame, same picture as without") {
    Result one = run(true);
    Result many = run(false);
    CHECK(one.blits_for_update == 1);
    CHECK(many.blits_for_update == 2);
    CHECK(one.fb == many.fb);
    CHECK(one.fb[15 * 320 + 15] == 0xF800);   // red box (RGB565, bgr off by default in backend)
    CHECK(one.fb[225 * 320 + 305] == 0x07E0); // green box
}

#include <thread>

TEST_CASE("frame rate limit: rapid changes are batched, the last state still arrives") {
    std::vector<uint16_t> fb(320 * 240, 0);
    int blits = 0;
    RpUsbDispBackend b([&](RpUsbDispBackend::TouchCallback) { return std::make_unique<Recorder>(&fb, &blits); },
                       50ms);
    b.set_max_fps(10);  // at most one blit per 100 ms
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);
    lv_obj_t* a = box(10, 10, 0x000000);
    for (int i = 0; i < 20; ++i) {  // first frame + settle
        lv_tick_inc(10);
        lv_timer_handler();
        b.poll();
        std::this_thread::sleep_for(10ms);
    }

    blits = 0;
    // ~300 ms of changes every 10 ms (like scrolling at full LVGL frame rate)
    for (int i = 0; i < 30; ++i) {
        lv_obj_set_style_bg_color(a, lv_color_hex(i % 2 ? 0x0000FF : 0x00FF00), 0);
        lv_tick_inc(10);
        lv_timer_handler();
        b.poll();
        std::this_thread::sleep_for(10ms);
    }
    lv_obj_set_style_bg_color(a, lv_color_hex(0xFF0000), 0);
    for (int i = 0; i < 20; ++i) {  // 200 ms idle: pending frame must be sent by poll()
        lv_tick_inc(10);
        lv_timer_handler();
        b.poll();
        std::this_thread::sleep_for(10ms);
    }
    CHECK(blits >= 3);
    CHECK(blits <= 7);
    CHECK(fb[15 * 320 + 15] == 0xF800);

    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
}
