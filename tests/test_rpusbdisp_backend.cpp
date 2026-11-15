#include <doctest/doctest.h>
#include "backend/rpusbdisp_backend.h"

#include <chrono>
#include <thread>

using namespace status;
using namespace std::chrono_literals;

namespace {

struct FakeState {
    int opens = 0;
    int blits = 0;
    bool available = true;   // can a device be opened right now
    bool alive = true;
    bool blit_fails = false;
    uint16_t first_pixel = 0;
    long pixels = 0;
    RpUsbDispBackend::TouchCallback touch;
};

struct FakeDevice : DisplayDevice {
    FakeState* st;
    explicit FakeDevice(FakeState* s) : st(s) {}
    bool alive() override { return st->alive; }
    bool blit(int32_t, int32_t, int32_t w, int32_t h, const uint16_t* px) override {
        ++st->blits;
        st->pixels += long(w) * h;
        st->first_pixel = px[0];
        return !st->blit_fails;
    }
};

RpUsbDispBackend::Opener fake_opener(FakeState& st) {
    return [&st](RpUsbDispBackend::TouchCallback cb) -> std::unique_ptr<DisplayDevice> {
        ++st.opens;
        if (!st.available) return nullptr;
        st.alive = true;
        st.touch = std::move(cb);
        return std::make_unique<FakeDevice>(&st);
    };
}

void pump(RpUsbDispBackend& b, int ms) {
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
        b.poll();
        std::this_thread::sleep_for(1ms);
    }
}

void teardown() {
    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
}

}  // namespace

TEST_CASE("no display at start: keeps retrying, then draws when it appears") {
    FakeState st;
    st.available = false;
    RpUsbDispBackend b(fake_opener(st), 50ms);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    pump(b, 30);
    CHECK(st.opens == 1);
    std::this_thread::sleep_for(60ms);
    pump(b, 10);
    CHECK(st.opens == 2);
    CHECK(st.blits == 0);

    st.available = true;
    std::this_thread::sleep_for(60ms);
    pump(b, 100);
    CHECK(st.opens == 3);
    CHECK(st.blits > 0);  // whole screen redrawn after connect
    teardown();
}

TEST_CASE("unplug: device dropped, reopened after the retry period, screen redrawn") {
    FakeState st;
    RpUsbDispBackend b(fake_opener(st), 50ms);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    pump(b, 100);
    REQUIRE(st.opens == 1);

    st.alive = false;
    st.available = false;
    pump(b, 20);
    st.available = true;
    std::this_thread::sleep_for(60ms);
    int blits_before = st.blits;
    pump(b, 100);
    CHECK(st.opens >= 2);
    CHECK(st.blits > blits_before);
    teardown();
}

TEST_CASE("failed blit on a device that still claims to be alive triggers a reconnect") {
    FakeState st;
    RpUsbDispBackend b(fake_opener(st), 50ms);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    pump(b, 100);
    REQUIRE(st.opens == 1);

    st.blit_fails = true;
    lv_obj_invalidate(lv_screen_active());
    pump(b, 20);
    st.blit_fails = false;
    std::this_thread::sleep_for(60ms);
    pump(b, 100);
    CHECK(st.opens == 2);
    teardown();
}

TEST_CASE("touch from the device reaches LVGL") {
    FakeState st;
    RpUsbDispBackend b(fake_opener(st), 50ms);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    bool clicked = false;
    lv_obj_t* btn = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(btn, 100, 100);
    lv_obj_set_size(btn, 40, 40);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) { *static_cast<bool*>(lv_event_get_user_data(e)) = true; },
                        LV_EVENT_CLICKED, &clicked);
    pump(b, 50);
    REQUIRE(st.touch);
    st.touch(120, 120, true, false);
    pump(b, 50);
    st.touch(120, 120, false, false);
    pump(b, 50);
    CHECK(clicked);
    teardown();
}

static uint16_t first_pixel_of_red_screen(bool bgr) {
    FakeState st;
    RpUsbDispBackend b(fake_opener(st), 50ms);
    b.set_bgr(bgr);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);
    pump(b, 50);
    uint16_t px = st.first_pixel;
    teardown();
    return px;
}

TEST_CASE("BGR display gets red in the low 5 bits, RGB display in the high 5 bits") {
    CHECK(first_pixel_of_red_screen(true) == 0x001F);
    CHECK(first_pixel_of_red_screen(false) == 0xF800);
}

TEST_CASE("rgb565_to_bgr565 swaps red and blue, keeps green") {
    CHECK(rgb565_to_bgr565(0xF800) == 0x001F);
    CHECK(rgb565_to_bgr565(0x001F) == 0xF800);
    CHECK(rgb565_to_bgr565(0x07E0) == 0x07E0);
    CHECK(rgb565_to_bgr565(0x8410) == 0x8410);
}

TEST_CASE("display reporting the dirty flag gets a full-screen redraw") {
    FakeState st;
    RpUsbDispBackend b(fake_opener(st), 50ms);
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    pump(b, 100);
    REQUIRE(st.touch);
    st.pixels = 0;
    pump(b, 100);
    CHECK(st.pixels == 0);  // idle: nothing redrawn

    st.touch(0, 0, false, true);  // status packet with RPUSBDISP_DISPLAY_STATUS_DIRTY_FLAG
    pump(b, 100);
    CHECK(st.pixels == 320L * 240);
    teardown();
}
