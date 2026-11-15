#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace status {

struct MemInfo {
    uint64_t total_kb = 0;
    uint64_t available_kb = 0;
};

struct SystemSnapshot {
    std::string hostname;
    double cpu = 0;              // percent 0..100
    MemInfo mem;
    double uptime_s = 0;
    std::optional<double> temp_c;
};

struct ContainerInfo {
    std::string name;
    std::string state;   // running, exited, restarting, paused, created, dead
    std::string status;  // human-readable from Docker, e.g. "Up 3 days"
};

struct DockerSnapshot {
    bool ok = false;
    std::string error;
    std::vector<ContainerInfo> containers;
};

struct Snapshot {
    SystemSnapshot sys;
    DockerSnapshot docker;
};

}  // namespace status
