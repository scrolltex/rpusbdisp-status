#include <doctest/doctest.h>
#include "data/collector.h"

#include <chrono>
#include <thread>

using namespace status;
using namespace std::chrono_literals;

TEST_CASE("collector fills snapshot from proc root and survives missing docker") {
    CollectorConfig cfg;
    cfg.proc_root = std::string(FIXTURES_DIR) + "/proc";
    cfg.sysfs_root = "/nonexistent-sys";
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.sys_period = 50ms;
    cfg.docker_period = 50ms;

    Collector c(cfg);
    c.start();
    std::this_thread::sleep_for(200ms);
    Snapshot s = c.snapshot();
    CHECK(s.sys.mem.total_kb == 16303988);
    CHECK(s.sys.mem.available_kb == 9876543);
    CHECK(s.sys.uptime_s == doctest::Approx(12345.67));
    CHECK_FALSE(s.sys.temp_c.has_value());
    CHECK_FALSE(s.sys.hostname.empty());
    CHECK_FALSE(s.docker.ok);
    CHECK_FALSE(s.docker.error.empty());

    auto t0 = std::chrono::steady_clock::now();
    c.stop();
    CHECK(std::chrono::steady_clock::now() - t0 < 500ms);
}

TEST_CASE("collector uses explicit temp path") {
    CollectorConfig cfg;
    cfg.proc_root = std::string(FIXTURES_DIR) + "/proc";
    cfg.sysfs_root = "/nonexistent-sys";
    cfg.temp_path = std::string(FIXTURES_DIR) + "/temp_input";
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.sys_period = 50ms;
    cfg.docker_period = 1000ms;
    Collector c(cfg);
    c.start();
    std::this_thread::sleep_for(150ms);
    auto t = c.snapshot().sys.temp_c;
    REQUIRE(t.has_value());
    CHECK(*t == doctest::Approx(47.5));
}

TEST_CASE("destructor stops a running collector") {
    CollectorConfig cfg;
    cfg.proc_root = std::string(FIXTURES_DIR) + "/proc";
    cfg.docker_socket = "/nonexistent/docker.sock";
    auto t0 = std::chrono::steady_clock::now();
    {
        Collector c(cfg);
        c.start();
        std::this_thread::sleep_for(20ms);
    }
    CHECK(std::chrono::steady_clock::now() - t0 < 500ms);
}

TEST_CASE("collector reports network speed of the default-route interface") {
    CollectorConfig cfg;
    cfg.proc_root = std::string(FIXTURES_DIR) + "/proc";
    cfg.sysfs_root = "/nonexistent-sys";
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.sys_period = 50ms;
    cfg.docker_period = 1000ms;
    Collector c(cfg);
    c.start();
    std::this_thread::sleep_for(200ms);
    auto n = c.snapshot().sys.net;
    CHECK(n.ok);
    CHECK(n.iface == "enp3s0");
    CHECK(n.rate.rx_bps == 0);  // static fixture counters
    CHECK(n.rate.tx_bps == 0);
}

TEST_CASE("collector with a missing interface reports no network rate") {
    CollectorConfig cfg;
    cfg.proc_root = std::string(FIXTURES_DIR) + "/proc";
    cfg.sysfs_root = "/nonexistent-sys";
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.net_iface = "nope0";
    cfg.sys_period = 50ms;
    cfg.docker_period = 1000ms;
    Collector c(cfg);
    c.start();
    std::this_thread::sleep_for(200ms);
    CHECK_FALSE(c.snapshot().sys.net.ok);
}
