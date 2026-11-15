#include <doctest/doctest.h>
#include "options.h"

#include <string>
#include <vector>

using namespace status;

static std::optional<Options> parse(std::vector<std::string> args, std::string& err) {
    std::vector<char*> argv;
    static std::string prog = "rpusbdisp-status";
    argv.push_back(prog.data());
    for (auto& a : args) argv.push_back(a.data());
    return parse_options(int(argv.size()), argv.data(), err);
}

TEST_CASE("defaults") {
    std::string err;
    auto o = parse({}, err);
    REQUIRE(o);
    CHECK(o->backend == "rpusbdisp");
    CHECK(o->rotate == 0);
    CHECK(o->docker_socket == "/var/run/docker.sock");
    CHECK_FALSE(o->swap_bytes);
    CHECK(o->start_tile == 0);
    CHECK(o->bgr);  // RoboPeak display is BGR565
}

TEST_CASE("color order") {
    std::string err;
    auto o = parse({"--color-order", "rgb"}, err);
    REQUIRE(o);
    CHECK_FALSE(o->bgr);
    CHECK_FALSE(parse({"--color-order", "grb"}, err));
    CHECK(err.find("color-order") != std::string::npos);
}

TEST_CASE("all options") {
    std::string err;
    auto o = parse({"--backend", "dump", "--dump-dir", "/tmp", "--rotate", "180", "--temp-path", "/t",
                    "--docker-socket", "/d.sock", "--swap-bytes", "--start-tile", "1"},
                   err);
    REQUIRE(o);
    CHECK(o->backend == "dump");
    CHECK(o->dump_dir == "/tmp");
    CHECK(o->rotate == 180);
    CHECK(o->temp_path == "/t");
    CHECK(o->docker_socket == "/d.sock");
    CHECK(o->swap_bytes);
    CHECK(o->start_tile == 1);
}

TEST_CASE("rotate must be 0 or 180") {
    std::string err;
    CHECK_FALSE(parse({"--rotate", "90"}, err));
    CHECK(err.find("rotate") != std::string::npos);
}

TEST_CASE("unknown option and missing value") {
    std::string err;
    CHECK_FALSE(parse({"--bogus"}, err));
    CHECK(err.find("--bogus") != std::string::npos);
    CHECK_FALSE(parse({"--backend"}, err));
    CHECK_FALSE(parse({"--backend", "vga"}, err));
}

TEST_CASE("help") {
    std::string err;
    auto o = parse({"--help"}, err);
    REQUIRE(o);
    CHECK(o->help);
}
