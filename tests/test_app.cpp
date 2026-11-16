#include <doctest/doctest.h>
#include "backend/dump_backend.h"
#include "ui/app.h"

using namespace status;

namespace {
struct DummyScreen : Screen {
    const char* name() const override { return "dummy"; }
    void create(lv_obj_t*) override {}
    void update(const Snapshot&) override {}
};

void pump(int ms) {
    for (int t = 0; t < ms; t += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
    }
}

void drag(DumpBackend& b, int x0, int y, int x1) {
    const int steps = 8;
    for (int i = 0; i <= steps; ++i) {
        b.touch(x0 + (x1 - x0) * i / steps, y, true);
        pump(20);
    }
    b.touch(x1, y, false);
    pump(600);
}
}  // namespace

TEST_CASE("swipe left goes to the next screen, tapping the first dot goes back") {
    DumpBackend b("/tmp");
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    App app;
    app.add(std::make_unique<DummyScreen>());
    app.add(std::make_unique<DummyScreen>());
    app.create(0);
    pump(100);
    CHECK(app.current() == 0);

    drag(b, 280, 120, 40);
    CHECK(app.current() == 1);

    // Dots: two 26 px targets centred at the bottom -> first at x≈147, y≈231.
    b.touch(147, 231, true);
    pump(50);
    b.touch(147, 231, false);
    pump(600);
    CHECK(app.current() == 0);

    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
}

#include "ui/screen_docker.h"

TEST_CASE("docker screen survives hundreds of containers") {
    DumpBackend b("/tmp");
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    lv_obj_t* tile = lv_obj_create(lv_screen_active());
    DockerScreen ds;
    ds.create(tile);
    Snapshot s;
    s.docker.ok = true;
    for (int i = 0; i < 300; ++i)
        s.docker.containers.push_back({"container-" + std::to_string(i), i % 3 ? "running" : "exited",
                                       i % 3 ? "Up " + std::to_string(i) + " minutes" : "Exited (1) 1 hour ago"});
    ds.update(s);
    pump(100);
    s.docker.containers[0].status = "Up 999 minutes";  // force a rebuild
    ds.update(s);
    pump(100);
    CHECK(lv_obj_get_child_count(lv_obj_get_child(tile, 1)) >= 300);
    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
}

TEST_CASE("home page in the middle, relative start, auto-return after 60 s") {
    DumpBackend b("/tmp");
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    App app;
    app.add(std::make_unique<DummyScreen>());
    app.add(std::make_unique<DummyScreen>());
    app.add(std::make_unique<DummyScreen>());
    app.set_home(1);
    CHECK(app.valid_offset(-1));
    CHECK(app.valid_offset(1));
    CHECK_FALSE(app.valid_offset(2));
    CHECK_FALSE(app.valid_offset(-2));
    app.create(-1);
    pump(100);
    CHECK(app.current() == 0);

    app.check_idle(59999);
    pump(600);
    CHECK(app.current() == 0);
    app.check_idle(60000);
    pump(600);
    CHECK(app.current() == 1);

    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
}

TEST_CASE("default start is the home page") {
    DumpBackend b("/tmp");
    REQUIRE(b.init(LV_DISPLAY_ROTATION_0));
    App app;
    app.add(std::make_unique<DummyScreen>());
    app.add(std::make_unique<DummyScreen>());
    app.add(std::make_unique<DummyScreen>());
    app.set_home(1);
    app.create();
    pump(100);
    CHECK(app.current() == 1);
    lv_obj_clean(lv_screen_active());
    lv_display_delete(lv_display_get_default());
}
