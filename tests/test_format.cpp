#include <doctest/doctest.h>
#include "ui/format.h"

using namespace status;

TEST_CASE("level_percent thresholds") {
    CHECK(level_percent(0) == Level::Ok);
    CHECK(level_percent(69.9) == Level::Ok);
    CHECK(level_percent(70) == Level::Warn);
    CHECK(level_percent(90) == Level::Warn);
    CHECK(level_percent(90.1) == Level::Crit);
}

TEST_CASE("level_temp thresholds") {
    CHECK(level_temp(64.9) == Level::Ok);
    CHECK(level_temp(65) == Level::Warn);
    CHECK(level_temp(80) == Level::Warn);
    CHECK(level_temp(80.5) == Level::Crit);
}

TEST_CASE("level_container") {
    CHECK(level_container({"a", "running", "Up 3 days"}) == Level::Ok);
    CHECK(level_container({"a", "restarting", "Restarting (1) 5 seconds ago"}) == Level::Warn);
    CHECK(level_container({"a", "paused", "Up 1 hour (Paused)"}) == Level::Warn);
    CHECK(level_container({"a", "exited", "Exited (0) 2 hours ago"}) == Level::Neutral);
    CHECK(level_container({"a", "exited", "Exited (137) 1 day ago"}) == Level::Crit);
    CHECK(level_container({"a", "dead", "Dead"}) == Level::Crit);
    CHECK(level_container({"a", "created", "Created"}) == Level::Neutral);
}

TEST_CASE("format_uptime") {
    CHECK(format_uptime(1051200) == "12d 4h");
    CHECK(format_uptime(11100) == "3h 5m");
    CHECK(format_uptime(2520) == "42m");
    CHECK(format_uptime(30) == "<1m");
}

TEST_CASE("format_bytes_kb") {
    CHECK(format_bytes_kb(6815744) == "6.5G");
    CHECK(format_bytes_kb(16303988) == "15.5G");
    CHECK(format_bytes_kb(524288) == "512M");
}

TEST_CASE("short_status trims Docker status text") {
    CHECK(short_status("Up 3 days") == "Up 3d");
    CHECK(short_status("Up 3 days (healthy)") == "Up 3d");
    CHECK(short_status("Up About an hour") == "Up 1h");
    CHECK(short_status("Up 5 hours (unhealthy)") == "Up 5h !");
    CHECK(short_status("Up Less than a second") == "Up <1m");
    CHECK(short_status("Up 12 minutes") == "Up 12m");
    CHECK(short_status("Up 2 weeks") == "Up 2w");
    CHECK(short_status("Exited (0) 2 hours ago") == "Exited (0)");
    CHECK(short_status("Restarting (1) 5 seconds ago") == "Restarting");
    CHECK(short_status("Created") == "Created");
}

TEST_CASE("format_rate") {
    CHECK(format_rate(0) == "0 B/s");
    CHECK(format_rate(512) == "512 B/s");
    CHECK(format_rate(1536) == "1.5 KB/s");
    CHECK(format_rate(120 * 1024) == "120 KB/s");
    CHECK(format_rate(1.2 * 1024 * 1024) == "1.2 MB/s");
    CHECK(format_rate(1.5 * 1024 * 1024 * 1024) == "1.5 GB/s");
}

TEST_CASE("net_active threshold is 10 KB/s in either direction") {
    CHECK(net_active({10240, 0}));
    CHECK(net_active({0, 20000}));
    CHECK_FALSE(net_active({10239, 10239}));
}
