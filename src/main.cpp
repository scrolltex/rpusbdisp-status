#include "backend/backend.h"
#include "backend/partial_backend.h"
#include "data/collector.h"
#include "options.h"
#include "ui/app.h"
#include "ui/screen_docker.h"
#include "ui/screen_overview.h"

#include <lvgl.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>

#include <unistd.h>

using namespace status;

static std::atomic<bool> g_stop{false};
static void on_signal(int) { g_stop = true; }

static uint32_t tick_ms() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint32_t(uint64_t(ts.tv_sec) * 1000u + uint64_t(ts.tv_nsec) / 1000000u);
}

struct UiCtx {
    App* app;
    Collector* collector;
};

static void ui_timer_cb(lv_timer_t* t) {
    auto* ctx = static_cast<UiCtx*>(lv_timer_get_user_data(t));
    ctx->app->update(ctx->collector->snapshot());
    ctx->app->check_idle(lv_display_get_inactive_time(nullptr));
}

int main(int argc, char** argv) {
    std::string err;
    auto opt = parse_options(argc, argv, err);
    if (!opt) {
        std::fprintf(stderr, "%s\n%s", err.c_str(), usage());
        return 2;
    }
    if (opt->help) {
        std::fputs(usage(), stdout);
        return 0;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    lvgl_init_once();
    lv_tick_set_cb(tick_ms);

    auto backend = make_backend(opt->backend, opt->dump_dir);
    if (!backend) {
        std::fprintf(stderr, "backend '%s' is not available in this build\n", opt->backend.c_str());
        return 2;
    }
    if (auto* pb = dynamic_cast<PartialBackend*>(backend.get())) {
        pb->set_swap_bytes(opt->swap_bytes);
        pb->set_coalesce(opt->coalesce);
        pb->set_max_fps(opt->fps);
        // Colour order is a property of the physical display; the dump file stays RGB.
        if (opt->backend == "rpusbdisp") pb->set_bgr(opt->bgr);
    }
    if (!backend->init(opt->rotate == 180 ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0)) {
        std::fprintf(stderr, "failed to initialise backend '%s'\n", opt->backend.c_str());
        return 1;
    }

    CollectorConfig cc;
    cc.temp_path = opt->temp_path;
    cc.docker_socket = opt->docker_socket;
    cc.net_iface = opt->net_iface;
    Collector collector(cc);
    collector.start();

    App app;
    app.add(std::make_unique<OverviewScreen>());
    app.add(std::make_unique<DockerScreen>());
    if (!app.valid_offset(opt->start_page)) {
        std::fprintf(stderr, "--start-page %d is out of range\n", opt->start_page);
        return 2;
    }
    app.create(opt->start_page);
    app.update(collector.snapshot());

    UiCtx ctx{&app, &collector};
    lv_timer_create(ui_timer_cb, 500, &ctx);

    while (!g_stop) {
        uint32_t wait = lv_timer_handler();
        backend->poll();
        if (wait > 20 || wait == LV_NO_TIMER_READY) wait = 20;
        usleep(wait * 1000);
    }
    collector.stop();
    return 0;
}
