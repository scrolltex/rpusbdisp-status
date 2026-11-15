#pragma once

#include "data/snapshot.h"

#include <cstdint>
#include <optional>
#include <string>

namespace status {

struct CpuTimes {
    uint64_t idle = 0;
    uint64_t total = 0;
};

std::optional<CpuTimes> parse_proc_stat(const std::string& text);
double cpu_percent(const CpuTimes& prev, const CpuTimes& cur);
std::optional<MemInfo> parse_meminfo(const std::string& text);
std::optional<double> parse_uptime(const std::string& text);
std::optional<std::string> find_temp_source(const std::string& sysfs_root);
std::optional<double> read_temp_c(const std::string& path);

}  // namespace status
