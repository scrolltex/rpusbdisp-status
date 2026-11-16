#pragma once

#include <optional>
#include <string>

namespace status {

struct Options {
    std::string backend = "rpusbdisp";
    int rotate = 0;
    std::string temp_path;
    std::string docker_socket = "/var/run/docker.sock";
    std::string dump_dir = ".";
    bool swap_bytes = false;
    bool coalesce = true;  // one blit per refresh instead of one per area
    bool bgr = true;  // RoboPeak display expects BGR565
    int start_tile = 0;
    bool help = false;
};

std::optional<Options> parse_options(int argc, char** argv, std::string& error);
const char* usage();

}  // namespace status
