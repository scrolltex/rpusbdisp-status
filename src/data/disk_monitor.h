#pragma once

#include "data/disks.h"
#include "data/nfs_probe.h"
#include "data/snapshot.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace status {

struct DiskMonitorConfig {
    std::string proc_root = "/proc";
    std::string sysfs_root = "/sys";
    std::string smart_dir = "/run/rpusbdisp-status/smart";
    std::vector<std::string> disks;  // --disks: explicit mount points, empty = auto
    StatvfsFn local_statvfs = statvfs_space;
    StatvfsFn nfs_statvfs = statvfs_space;
    std::chrono::milliseconds nfs_deadline{2000};
    std::chrono::milliseconds nfs_period{5000};
    std::chrono::milliseconds mounts_period{5000};
};

// Builds the Disks rows. Call sample() about once a second from one thread.
class DiskMonitor {
public:
    explicit DiskMonitor(DiskMonitorConfig cfg);
    std::vector<DiskInfo> sample();

private:
    struct Row {
        MountEntry mount;
        std::optional<BlockDev> dev;
        std::optional<std::string> temp_input;
        std::unique_ptr<NfsProbe> probe;
        std::chrono::steady_clock::time_point last_nfs_request{};
        std::optional<IoCounters> prev_io;
        std::chrono::steady_clock::time_point prev_io_time{};
    };
    void refresh_mounts();

    DiskMonitorConfig cfg_;
    std::vector<Row> rows_;
    std::chrono::steady_clock::time_point mounts_read_{};
    bool mounts_loaded_ = false;
};

}  // namespace status
