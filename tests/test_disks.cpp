#include <doctest/doctest.h>
#include "data/disks.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

namespace fs = std::filesystem;
using namespace status;

static std::string fixture(const std::string& name) {
    std::ifstream f(std::string(FIXTURES_DIR) + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

namespace {
// Fake /sys laid out like a real kernel for one NVMe disk.
struct FakeSys {
    fs::path root;
    FakeSys() {
        root = fs::temp_directory_path() / ("status-sys-" + std::to_string(::getpid()) + "-" + std::to_string(rand()));
        fs::path ctrl = root / "devices/pci0000:00/0000:00:01.0/nvme/nvme0";
        fs::path disk = ctrl / "nvme0n1";
        fs::create_directories(disk / "nvme0n1p2");
        std::ofstream(disk / "nvme0n1p2/partition") << "2\n";
        fs::create_directories(ctrl / "hwmon3");
        std::ofstream(ctrl / "hwmon3/temp1_input") << "48850\n";
        fs::create_directory_symlink(ctrl, disk / "device");  // nvme0n1/device -> controller
        fs::create_directories(root / "dev/block");
        fs::create_directory_symlink(disk / "nvme0n1p2", root / "dev/block/259:2");
        fs::create_directories(root / "class/block");
        fs::create_directory_symlink(disk / "nvme0n1p2", root / "class/block/nvme0n1p2");
        fs::create_directory_symlink(disk, root / "class/block/nvme0n1");
        fs::create_directories(root / "class/nvme");
        fs::create_directory_symlink(ctrl, root / "class/nvme/nvme0");
    }
    ~FakeSys() { fs::remove_all(root); }
};
}  // namespace

TEST_CASE("parse_mounts keeps real filesystems, dedupes, orders, decodes escapes") {
    auto m = parse_mounts(fixture("disks/mountinfo.txt"));
    REQUIRE(m.size() == 3);
    CHECK(m[0].mount_point == "/");
    CHECK(m[0].fstype == "ext4");
    CHECK(m[0].source == "/dev/nvme0n1p2");
    CHECK(m[0].major == 259);
    CHECK(m[0].minor == 2);
    CHECK_FALSE(m[0].nfs);
    CHECK(m[1].mount_point == "/mnt/media");
    CHECK(m[1].nfs);
    CHECK(m[1].source == "nas:/media");
    CHECK(m[2].mount_point == "/mnt/my backup");
}

TEST_CASE("select_mounts keeps the requested order and drops unknown") {
    auto all = parse_mounts(fixture("disks/mountinfo.txt"));
    auto s = select_mounts(all, {"/mnt/media", "/nope", "/"});
    REQUIRE(s.size() == 2);
    CHECK(s[0].mount_point == "/mnt/media");
    CHECK(s[1].mount_point == "/");
}

TEST_CASE("resolve_block_device via major:minor") {
    FakeSys sys;
    MountEntry m{"/", "ext4", "/dev/nvme0n1p2", 259, 2, false};
    auto d = resolve_block_device(m, sys.root.string());
    REQUIRE(d);
    CHECK(d->disk == "nvme0n1");
    CHECK(d->controller == "nvme0");
}

TEST_CASE("resolve_block_device falls back to the source name (btrfs 0:NN)") {
    FakeSys sys;
    MountEntry m{"/data", "btrfs", "/dev/nvme0n1p2", 0, 31, false};
    auto d = resolve_block_device(m, sys.root.string());
    REQUIRE(d);
    CHECK(d->disk == "nvme0n1");
    MountEntry z{"/tank", "zfs", "tank/data", 0, 40, false};
    CHECK_FALSE(resolve_block_device(z, sys.root.string()));
}

TEST_CASE("find_disk_temp_input for NVMe") {
    FakeSys sys;
    BlockDev d{"nvme0n1", "nvme0"};
    auto p = find_disk_temp_input(d, sys.root.string());
    REQUIRE(p);
    CHECK(p->find("hwmon3/temp1_input") != std::string::npos);
    CHECK_FALSE(find_disk_temp_input(BlockDev{"sdz", "sdz"}, sys.root.string()));
}

TEST_CASE("parse_diskstats whole disk, sectors to bytes") {
    auto c = parse_diskstats(fixture("disks/diskstats.txt"), "nvme0n1");
    REQUIRE(c);
    CHECK(c->read_bytes == 2048ull * 512);
    CHECK(c->write_bytes == 4096ull * 512);
    CHECK_FALSE(parse_diskstats(fixture("disks/diskstats.txt"), "nvme0"));
}

TEST_CASE("parse_mountstats_nfs uses server read/write bytes") {
    auto c = parse_mountstats_nfs(fixture("disks/mountstats.txt"), "/mnt/media");
    REQUIRE(c);
    CHECK(c->read_bytes == 4096);
    CHECK(c->write_bytes == 8192);
    auto b = parse_mountstats_nfs(fixture("disks/mountstats.txt"), "/mnt/my backup");
    REQUIRE(b);
    CHECK(b->read_bytes == 5);
    CHECK_FALSE(parse_mountstats_nfs(fixture("disks/mountstats.txt"), "/mnt/none"));
}

TEST_CASE("parse_smart_json") {
    auto ok = parse_smart_json(fixture("smart/nvme_ok.json"));
    CHECK(ok.state == SmartState::Ok);
    REQUIRE(ok.wear_pct);
    CHECK(*ok.wear_pct == 3);
    CHECK(parse_smart_json(fixture("smart/nvme_worn.json")).state == SmartState::Warn);
    CHECK(parse_smart_json(fixture("smart/nvme_media.json")).state == SmartState::Warn);
    CHECK(parse_smart_json(fixture("smart/nvme_fail.json")).state == SmartState::Fail);
    CHECK(parse_smart_json(fixture("smart/nvme_critical.json")).state == SmartState::Fail);
    auto ata = parse_smart_json(fixture("smart/ata_ok.json"));
    CHECK(ata.state == SmartState::Ok);
    CHECK_FALSE(ata.wear_pct);
    CHECK(parse_smart_json("garbage").state == SmartState::Unknown);
    CHECK(parse_smart_json("{}").state == SmartState::Unknown);
    CHECK(parse_smart_json("[1,2]").state == SmartState::Unknown);
}

TEST_CASE("read_smart: missing file, fresh and stale") {
    fs::path dir = fs::temp_directory_path() / ("status-smart-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    auto now = std::chrono::system_clock::now();
    CHECK(read_smart(dir.string(), "nvme0", now).state == SmartState::Unknown);
    fs::copy_file(std::string(FIXTURES_DIR) + "/smart/nvme_ok.json", dir / "nvme0.json");
    auto fresh = read_smart(dir.string(), "nvme0", std::chrono::system_clock::now());
    CHECK(fresh.state == SmartState::Ok);
    CHECK_FALSE(fresh.stale);
    auto later = read_smart(dir.string(), "nvme0", std::chrono::system_clock::now() + std::chrono::minutes(31));
    CHECK(later.state == SmartState::Ok);
    CHECK(later.stale);
    fs::remove_all(dir);
}
