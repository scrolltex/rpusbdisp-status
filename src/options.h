#pragma once

#include <optional>
#include <string>
#include <vector>

namespace status {

struct Options {
    std::string backend = "rpusbdisp";
    int rotate = 0;
    std::string temp_path;
    std::string docker_socket = "/var/run/docker.sock";
    std::string smart_dir = "/run/rpusbdisp-status/smart";
    std::vector<std::string> disks;  // explicit mount points for the Disks page
    std::string net_iface;  // empty: auto (default route)
    std::string dump_dir = ".";
    bool swap_bytes = false;
    bool coalesce = true;
    int fps = 16;  // max display updates per second (0 = unlimited)  // one blit per refresh instead of one per area
    bool bgr = true;  // RoboPeak display expects BGR565
    int start_page = 0;  // relative to the home page: <0 left, >0 right
    bool help = false;
};

std::optional<Options> parse_options(int argc, char** argv, std::string& error);
const char* usage();

}  // namespace status
