#include <doctest/doctest.h>
#include "data/collector.h"
#include "data/disk_monitor.h"
#include "fake_sys.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
using namespace status;
using namespace std::chrono_literals;

namespace {
struct FakeProc {
    fs::path root;
    FakeProc() {
        root = fs::temp_directory_path() / ("status-proc-" + std::to_string(::getpid()) + "-" + std::to_string(rand()));
        fs::create_directories(root / "self");
        std::ofstream(root / "self/mountinfo")
            << "29 1 259:2 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p2 rw\n"
               "50 29 0:60 / /mnt/media rw,relatime shared:7 - nfs4 nas:/media rw\n";
        std::ofstream(root / "self/mountstats")
            << "device nas:/media mounted on /mnt/media with fstype nfs4 statvers=1.1\n\tbytes:\t0 0 0 0 4096 8192 0 0\n";
        set_diskstats(2048, 4096);
        for (const char* f : {"stat", "meminfo", "uptime"})
            fs::copy_file(std::string(FIXTURES_DIR) + "/proc/" + f, root / f);
    }
    void set_diskstats(unsigned long rd, unsigned long wr) {
        std::ofstream(root / "diskstats.tmp")
            << " 259 0 nvme0n1 0 0 " << rd << " 0 0 0 " << wr << " 0 0 0 0 0 0 0 0 0 0\n";
        fs::rename(root / "diskstats.tmp", root / "diskstats");
    }
    ~FakeProc() { fs::remove_all(root); }
};

struct Gate {
    std::mutex mu;
    std::condition_variable cv;
    bool open = false;
};
}  // namespace

TEST_CASE("disk monitor: local NVMe row with device, temperature and IO; NFS row") {
    FakeSys sys;
    FakeProc proc;
    DiskMonitorConfig cfg;
    cfg.proc_root = proc.root.string();
    cfg.sysfs_root = sys.root.string();
    cfg.smart_dir = std::string(FIXTURES_DIR) + "/smart";  // has nvme_ok.json, not nvme0.json
    cfg.nfs_statvfs = [](const std::string&) { return std::optional<SpaceInfo>(SpaceInfo{4000, 3000}); };
    cfg.local_statvfs = [](const std::string&) { return std::optional<SpaceInfo>(SpaceInfo{1000, 620}); };
    DiskMonitor m(cfg);

    auto d = m.sample();
    REQUIRE(d.size() == 2);
    CHECK(d[0].mount_point == "/");
    CHECK(d[0].device == "nvme0");
    CHECK(d[0].total == 1000);
    CHECK(d[0].used == 620);
    REQUIRE(d[0].temp_c);
    CHECK(*d[0].temp_c == doctest::Approx(48.85));
    CHECK(d[0].smart == SmartState::Unknown);
    CHECK_FALSE(d[0].io);  // first sample: no rate yet
    CHECK(d[1].mount_point == "/mnt/media");
    CHECK(d[1].nfs);

    std::this_thread::sleep_for(100ms);
    proc.set_diskstats(2048 + 2000, 4096);
    d = m.sample();
    REQUIRE(d[0].io);
    CHECK(d[0].io->rx_bps > 0);       // read
    CHECK(d[0].io->tx_bps == 0);      // write
    CHECK(d[1].available);
    CHECK(d[1].total == 4000);
}

TEST_CASE("disk monitor: SMART file for the controller is used") {
    FakeSys sys;
    FakeProc proc;
    fs::path smart = fs::temp_directory_path() / ("status-smartdir-" + std::to_string(::getpid()));
    fs::create_directories(smart);
    fs::copy_file(std::string(FIXTURES_DIR) + "/smart/nvme_worn.json", smart / "nvme0.json",
                  fs::copy_options::overwrite_existing);
    DiskMonitorConfig cfg;
    cfg.proc_root = proc.root.string();
    cfg.sysfs_root = sys.root.string();
    cfg.smart_dir = smart.string();
    cfg.nfs_statvfs = [](const std::string&) { return std::optional<SpaceInfo>(); };
    DiskMonitor m(cfg);
    auto d = m.sample();
    CHECK(d[0].smart == SmartState::Warn);
    REQUIRE(d[0].wear_pct);
    CHECK(*d[0].wear_pct == 92);
    fs::remove_all(smart);
}

TEST_CASE("collector: hung NFS does not stop CPU/RAM updates or stop()") {
    FakeSys sys;
    FakeProc proc;
    auto gate = std::make_shared<Gate>();
    CollectorConfig cfg;
    cfg.proc_root = proc.root.string();
    cfg.sysfs_root = sys.root.string();
    cfg.docker_socket = "/nonexistent/docker.sock";
    cfg.sys_period = 30ms;
    cfg.docker_period = 1000ms;
    cfg.nfs_deadline = 100ms;
    cfg.nfs_statvfs = [gate](const std::string&) -> std::optional<SpaceInfo> {
        std::unique_lock<std::mutex> lk(gate->mu);
        gate->cv.wait(lk, [&] { return gate->open; });
        return std::nullopt;
    };
    auto t0 = std::chrono::steady_clock::now();
    {
        Collector c(cfg);
        c.start();
        std::this_thread::sleep_for(300ms);
        auto s = c.snapshot();
        CHECK(s.sys.mem.total_kb == 16303988);
        REQUIRE(s.sys.disks.size() == 2);
        CHECK(s.sys.disks[0].available);
        CHECK_FALSE(s.sys.disks[1].available);
    }
    CHECK(std::chrono::steady_clock::now() - t0 < 1500ms);
    {
        std::lock_guard<std::mutex> lk(gate->mu);
        gate->open = true;
    }
    gate->cv.notify_all();
    std::this_thread::sleep_for(50ms);
}
