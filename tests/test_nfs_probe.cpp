#include <doctest/doctest.h>
#include "data/nfs_probe.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

using namespace status;
using namespace std::chrono_literals;

namespace {
void wait_until(const std::function<bool()>& cond, std::chrono::milliseconds limit = 1000ms) {
    auto end = std::chrono::steady_clock::now() + limit;
    while (!cond() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(5ms);
}

// statvfs replacement that blocks until released (like a dead hard-mounted NFS).
struct Blocker {
    std::mutex mu;
    std::condition_variable cv;
    bool released = false;
    std::atomic<int> calls{0};
};
}  // namespace

TEST_CASE("fast statvfs: available with space") {
    NfsProbe p("/mnt/x", [](const std::string&) { return std::optional<SpaceInfo>(SpaceInfo{1000, 400}); }, 100ms);
    p.request();
    wait_until([&] { return p.result().available; });
    auto r = p.result();
    CHECK(r.available);
    CHECK(r.space.total == 1000);
    CHECK(r.space.used == 400);
}

TEST_CASE("failing statvfs: unavailable") {
    NfsProbe p("/mnt/x", [](const std::string&) { return std::optional<SpaceInfo>(); }, 100ms);
    p.request();
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(p.result().available);
}

TEST_CASE("hung statvfs: unavailable after the deadline, no second call while stuck") {
    auto b = std::make_shared<Blocker>();
    auto fn = [b](const std::string&) -> std::optional<SpaceInfo> {
        if (++b->calls == 1) return SpaceInfo{1000, 100};  // server alive on the first call
        std::unique_lock<std::mutex> lk(b->mu);
        b->cv.wait(lk, [&] { return b->released; });
        return SpaceInfo{1, 1};
    };
    {
        NfsProbe p("/mnt/x", fn, 100ms);
        p.request();
        wait_until([&] { return p.result().available; });
        REQUIRE(p.result().available);

        p.request();  // server is dead now: this call hangs
        std::this_thread::sleep_for(50ms);
        CHECK(p.result().available);  // within the deadline the last result still counts
        std::this_thread::sleep_for(100ms);
        CHECK_FALSE(p.result().available);
        p.request();
        p.request();
        std::this_thread::sleep_for(20ms);
        CHECK(b->calls == 2);
    }  // probe destroyed while its worker is stuck
    {
        std::lock_guard<std::mutex> lk(b->mu);
        b->released = true;
    }
    b->cv.notify_all();
    std::this_thread::sleep_for(50ms);  // let the detached worker finish
}

TEST_CASE("destroying a probe with a stuck worker returns immediately") {
    auto b = std::make_shared<Blocker>();
    auto fn = [b](const std::string&) -> std::optional<SpaceInfo> {
        std::unique_lock<std::mutex> lk(b->mu);
        b->cv.wait(lk, [&] { return b->released; });
        return std::nullopt;
    };
    auto t0 = std::chrono::steady_clock::now();
    {
        NfsProbe p("/mnt/x", fn, 100ms);
        p.request();
        std::this_thread::sleep_for(20ms);
    }
    CHECK(std::chrono::steady_clock::now() - t0 < 200ms);
    {
        std::lock_guard<std::mutex> lk(b->mu);
        b->released = true;
    }
    b->cv.notify_all();
    std::this_thread::sleep_for(50ms);
}

TEST_CASE("statvfs_space on a real directory") {
    auto s = statvfs_space("/tmp");
    REQUIRE(s);
    CHECK(s->total > 0);
    CHECK(s->used <= s->total);
    CHECK_FALSE(statvfs_space("/nonexistent/path"));
}
