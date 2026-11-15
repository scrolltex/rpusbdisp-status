#include <doctest/doctest.h>
#include "backend/backend.h"

#include <lvgl.h>

#include <vector>

using namespace status;

// Pins the LVGL behaviour the rpusbdisp backend relies on for --rotate 180.
TEST_CASE("180 rotation maps flush areas and touch points") {
    lvgl_init_once();
    lv_display_t* d = lv_display_create(320, 240);
    lv_display_set_rotation(d, LV_DISPLAY_ROTATION_180);

    lv_area_t a{0, 0, 319, 39};
    lv_display_rotate_area(d, &a);
    CHECK(a.x1 == 0);
    CHECK(a.y1 == 200);
    CHECK(a.x2 == 319);
    CHECK(a.y2 == 239);

    lv_area_t b{10, 20, 30, 40};
    lv_display_rotate_area(d, &b);
    CHECK(b.x1 == 289);
    CHECK(b.y1 == 199);
    CHECK(b.x2 == 309);
    CHECK(b.y2 == 219);

    lv_point_t p{309, 229};  // physical touch
    lv_display_rotate_point(d, &p);
    CHECK(p.x == 10);
    CHECK(p.y == 10);
    lv_display_delete(d);
}

TEST_CASE("rotate_rgb565_180 reverses pixel order") {
    std::vector<uint16_t> src{1, 2, 3, 4, 5, 6};  // 3x2
    std::vector<uint16_t> dst(6);
    rotate_rgb565_180(src.data(), dst.data(), 3, 2);
    CHECK(dst == std::vector<uint16_t>{6, 5, 4, 3, 2, 1});
}

TEST_CASE("make_backend rejects unknown names") {
    CHECK(make_backend("nope", ".") == nullptr);
    CHECK(make_backend("dump", ".") != nullptr);
}
