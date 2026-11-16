#pragma once

#include "data/snapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace status {

struct MountEntry {
    std::string mount_point;
    std::string fstype;
    std::string source;
    unsigned major = 0, minor = 0;
    bool nfs = false;
};

// Real filesystems from /proc/self/mountinfo: filtered, deduplicated, ordered
// ("/" first, other local by mount point, then NFS by mount point).
std::vector<MountEntry> parse_mounts(const std::string& mountinfo);
// Only the requested mount points, in the requested order (--disks).
std::vector<MountEntry> select_mounts(const std::vector<MountEntry>& all, const std::vector<std::string>& wanted);

struct BlockDev {
    std::string disk;        // whole disk: nvme0n1, sda
    std::string controller;  // SMART file name: nvme0, sda
};
std::optional<BlockDev> resolve_block_device(const MountEntry& m, const std::string& sysfs_root);
std::optional<std::string> find_disk_temp_input(const BlockDev& d, const std::string& sysfs_root);

struct IoCounters {
    uint64_t read_bytes = 0;
    uint64_t write_bytes = 0;
};
std::optional<IoCounters> parse_diskstats(const std::string& text, const std::string& disk);
std::optional<IoCounters> parse_mountstats_nfs(const std::string& text, const std::string& mount_point);

}  // namespace status
