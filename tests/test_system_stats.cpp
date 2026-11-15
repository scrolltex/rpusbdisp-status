#include <doctest/doctest.h>
#include "data/system_stats.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace status;

static std::string read_fixture(const std::string& name) {
    std::ifstream f(std::string(FIXTURES_DIR) + "/" + name);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

static void write_file(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("status-test-" + std::to_string(::getpid()) + "-" + std::to_string(rand()));
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

TEST_CASE("cpu_percent from two /proc/stat samples") {
    auto a = parse_proc_stat(read_fixture("proc_stat_a.txt"));
    auto b = parse_proc_stat(read_fixture("proc_stat_b.txt"));
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->total == 1000);
    CHECK(a->idle == 850);
    CHECK(cpu_percent(*a, *b) == doctest::Approx(30.0).epsilon(0.0001));
}

TEST_CASE("cpu_percent is 0 when total did not grow") {
    CpuTimes t{850, 1000};
    CHECK(cpu_percent(t, t) == 0.0);
}

TEST_CASE("parse_proc_stat rejects garbage") {
    CHECK_FALSE(parse_proc_stat("intr 1 2 3\n"));
}

TEST_CASE("parse_meminfo") {
    auto m = parse_meminfo(read_fixture("meminfo.txt"));
    REQUIRE(m);
    CHECK(m->total_kb == 16303988);
    CHECK(m->available_kb == 9876543);
}

TEST_CASE("parse_uptime") {
    auto u = parse_uptime("12345.67 999.0\n");
    REQUIRE(u);
    CHECK(*u == doctest::Approx(12345.67));
    CHECK_FALSE(parse_uptime(""));
}

TEST_CASE("find_temp_source prefers coretemp hwmon") {
    TempDir d;
    write_file(d.path / "class/thermal/thermal_zone0/type", "acpitz\n");
    write_file(d.path / "class/thermal/thermal_zone0/temp", "40000\n");
    write_file(d.path / "class/hwmon/hwmon0/name", "nvme\n");
    write_file(d.path / "class/hwmon/hwmon0/temp1_input", "30000\n");
    write_file(d.path / "class/hwmon/hwmon3/name", "coretemp\n");
    write_file(d.path / "class/hwmon/hwmon3/temp1_input", "54000\n");
    auto src = find_temp_source(d.path.string());
    REQUIRE(src);
    CHECK(*src == (d.path / "class/hwmon/hwmon3/temp1_input").string());
}

TEST_CASE("find_temp_source prefers x86_pkg_temp zone over zone0") {
    TempDir d;
    write_file(d.path / "class/thermal/thermal_zone0/type", "acpitz\n");
    write_file(d.path / "class/thermal/thermal_zone0/temp", "40000\n");
    write_file(d.path / "class/thermal/thermal_zone2/type", "x86_pkg_temp\n");
    write_file(d.path / "class/thermal/thermal_zone2/temp", "50000\n");
    auto src = find_temp_source(d.path.string());
    REQUIRE(src);
    CHECK(*src == (d.path / "class/thermal/thermal_zone2/temp").string());
}

TEST_CASE("find_temp_source falls back to thermal_zone0") {
    TempDir d;
    write_file(d.path / "class/thermal/thermal_zone0/type", "acpitz\n");
    write_file(d.path / "class/thermal/thermal_zone0/temp", "40000\n");
    auto src = find_temp_source(d.path.string());
    REQUIRE(src);
    CHECK(*src == (d.path / "class/thermal/thermal_zone0/temp").string());
}

TEST_CASE("find_temp_source on empty sysfs gives nothing") {
    TempDir d;
    CHECK_FALSE(find_temp_source(d.path.string()));
    CHECK_FALSE(find_temp_source((d.path / "missing").string()));
}

TEST_CASE("read_temp_c converts millidegrees") {
    TempDir d;
    write_file(d.path / "t", "54000\n");
    auto t = read_temp_c((d.path / "t").string());
    REQUIRE(t);
    CHECK(*t == doctest::Approx(54.0));
    CHECK_FALSE(read_temp_c((d.path / "nope").string()));
}
