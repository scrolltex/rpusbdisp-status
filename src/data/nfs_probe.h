#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace status {

struct SpaceInfo {
    uint64_t total = 0;  // bytes
    uint64_t used = 0;   // bytes (total - free, reserved counts as used)
};

using StatvfsFn = std::function<std::optional<SpaceInfo>(const std::string& path)>;

// statvfs(3) wrapper; nullopt on error.
std::optional<SpaceInfo> statvfs_space(const std::string& path);

// Runs statvfs for one (NFS) mount on a detached worker thread so a dead
// server can never block the caller. A call that has not finished within
// `deadline` makes the mount unavailable; while it is stuck no new call is
// started, and destroying the probe never waits for it.
class NfsProbe {
public:
    struct Result {
        bool available = false;
        SpaceInfo space;
    };

    explicit NfsProbe(std::string path, StatvfsFn fn = statvfs_space,
                      std::chrono::milliseconds deadline = std::chrono::milliseconds(2000));
    void request();
    Result result() const;

private:
    struct Shared;
    std::shared_ptr<Shared> s_;
};

}  // namespace status
