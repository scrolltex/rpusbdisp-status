#include <doctest/doctest.h>
#include "data/collector.h"

#include <chrono>
#include <thread>
#include <unistd.h>

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

#include <filesystem>
#include <algorithm>
#include <fstream>

TEST_CASE("network interface disappearing and coming back") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / ("status-net-" + std::to_string(::getpid()));
    fs::create_directories(root / "net");
    fs::copy_file(std::string(FIXTURES_DIR) + "/proc/stat", root / "stat", fs::copy_options::overwrite_existing);
    const std::string route = std::string(FIXTURES_DIR) + "/net_route.txt";
    auto write_dev = [&](const char* iface, unsigned long rx) {
        std::ofstream(root / "net/dev.tmp") << "Inter-|\n face |\n " << iface << ": " << rx << " 0 0 0 0 0 0 0 1000 0 0 0 0 0 0 0\n";
        fs::rename(root / "net/dev.tmp", root / "net/dev");
    };
    fs::copy_file(route, root / "net/route", fs::copy_options::overwrite_existing);
    write_dev("enp3s0", 0);

    CollectorConfig cfg;
    cfg.proc_root = root.string();
    cfg.sysfs_root = "/nonexistent-sys";
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.sys_period = 50ms;
    cfg.docker_period = 1000ms;
    Collector c(cfg);
    c.start();
    std::this_thread::sleep_for(200ms);
    CHECK(c.snapshot().sys.net.ok);

    write_dev("enp9s9", 0);  // our interface vanished (renamed)
    std::this_thread::sleep_for(200ms);
    CHECK_FALSE(c.snapshot().sys.net.ok);

    write_dev("enp3s0", 5000000);  // back, with a fresh counter
    std::this_thread::sleep_for(300ms);
    auto n = c.snapshot().sys.net;
    CHECK(n.ok);
    CHECK(n.rate.rx_bps == 0);  // no jump from mixing old and new counters
    c.stop();
    fs::remove_all(root);
}

TEST_CASE("default route switching to another interface does not mix counters") {
    namespace fs = std::filesystem;
    fs::path root = fs::temp_directory_path() / ("status-net2-" + std::to_string(::getpid()));
    fs::create_directories(root / "net");
    fs::copy_file(std::string(FIXTURES_DIR) + "/proc/stat", root / "stat", fs::copy_options::overwrite_existing);
    std::ofstream(root / "net/dev") << "Inter-|\n face |\n enp3s0: 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n"
                                       "  wlan0: 9000000000 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n";
    auto set_route = [&](const std::string& fixture) {
        fs::copy_file(std::string(FIXTURES_DIR) + "/" + fixture, root / "net/route.tmp",
                      fs::copy_options::overwrite_existing);
        fs::rename(root / "net/route.tmp", root / "net/route");
    };
    set_route("net_route.txt");  // enp3s0

    CollectorConfig cfg;
    cfg.proc_root = root.string();
    cfg.sysfs_root = "/nonexistent-sys";
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.sys_period = 30ms;
    cfg.docker_period = 1000ms;
    Collector c(cfg);
    c.start();
    std::this_thread::sleep_for(150ms);
    REQUIRE(c.snapshot().sys.net.iface == "enp3s0");

    std::ofstream(root / "net/route.wifi") << "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\n"
                                              "wlan0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\n";
    fs::rename(root / "net/route.wifi", root / "net/route");
    double max_rx = 0;
    for (int i = 0; i < 40; ++i) {
        max_rx = std::max(max_rx, c.snapshot().sys.net.rate.rx_bps);
        std::this_thread::sleep_for(5ms);
    }
    CHECK(c.snapshot().sys.net.iface == "wlan0");
    CHECK(max_rx == 0);  // never a 9 GB jump from enp3s0 -> wlan0 counters
    c.stop();
    fs::remove_all(root);
}
