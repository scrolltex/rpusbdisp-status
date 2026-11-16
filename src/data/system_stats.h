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

struct NetCounters {
    uint64_t rx_bytes = 0;
    uint64_t tx_bytes = 0;
};

// Interface of the default route (Destination 00000000, Flags has RTF_GATEWAY) in /proc/net/route.
std::optional<std::string> default_route_iface(const std::string& route_text);
// Byte counters of `iface` from /proc/net/dev.
std::optional<NetCounters> parse_net_dev(const std::string& text, const std::string& iface);
// Per-direction rate; a counter that went down (interface recreated) gives 0.
NetRate net_rate(const NetCounters& prev, const NetCounters& cur, double seconds);

}  // namespace status
