#include "options.h"

#include <cstdlib>
#include <cstring>

namespace status {

const char* usage() {
    return "usage: rpusbdisp-status [options]\n"
           "  --backend rpusbdisp|dump|sdl  output (default rpusbdisp)\n"
           "  --rotate 0|180                screen rotation (default 0)\n"
           "  --color-order bgr|rgb         display colour order (default bgr; rpusbdisp only)\n"
           "  --fps N                       max display updates per second (default 16, 0 = unlimited)\n"
           "  --no-coalesce                 send every redrawn area separately (debugging)\n"
           "  --swap-bytes                  swap the two bytes of each pixel (if colours are garbled)\n"
           "  --temp-path PATH              temperature file in millidegrees (default: auto)\n"
           "  --net-iface NAME              interface for NET speed (default: default route)\n"
           "  --docker-socket PATH          default /var/run/docker.sock\n"
           "  --dump-dir DIR                where the dump backend writes frame.ppm\n"
           "  --start-tile N                screen shown at start (default 0)\n"
           "  --help\n";
}

std::optional<Options> parse_options(int argc, char** argv, std::string& error) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) {
                error = "missing value for " + a;
                return false;
            }
            out = argv[++i];
            return true;
        };
        std::string v;
        if (a == "--help" || a == "-h") {
            o.help = true;
        } else if (a == "--backend") {
            if (!value(o.backend)) return std::nullopt;
            if (o.backend != "rpusbdisp" && o.backend != "dump" && o.backend != "sdl") {
                error = "unknown backend: " + o.backend;
                return std::nullopt;
            }
        } else if (a == "--rotate") {
            if (!value(v)) return std::nullopt;
            if (v != "0" && v != "180") {
                error = "--rotate must be 0 or 180";
                return std::nullopt;
            }
            o.rotate = std::atoi(v.c_str());
        } else if (a == "--temp-path") {
            if (!value(o.temp_path)) return std::nullopt;
        } else if (a == "--net-iface") {
            if (!value(o.net_iface)) return std::nullopt;
        } else if (a == "--docker-socket") {
            if (!value(o.docker_socket)) return std::nullopt;
        } else if (a == "--dump-dir") {
            if (!value(o.dump_dir)) return std::nullopt;
        } else if (a == "--color-order") {
            if (!value(v)) return std::nullopt;
            if (v != "bgr" && v != "rgb") {
                error = "--color-order must be bgr or rgb";
                return std::nullopt;
            }
            o.bgr = v == "bgr";
        } else if (a == "--fps") {
            if (!value(v)) return std::nullopt;
            char* end = nullptr;
            long n = std::strtol(v.c_str(), &end, 10);
            if (v.empty() || *end != '\0' || n < 0 || n > 1000) {
                error = "--fps must be a number 0..1000";
                return std::nullopt;
            }
            o.fps = int(n);
        } else if (a == "--no-coalesce") {
            o.coalesce = false;
        } else if (a == "--swap-bytes") {
            o.swap_bytes = true;
        } else if (a == "--start-tile") {
            if (!value(v)) return std::nullopt;
            o.start_tile = std::atoi(v.c_str());
        } else {
            error = "unknown option: " + a;
            return std::nullopt;
        }
    }
    return o;
}

}  // namespace status
