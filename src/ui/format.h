#pragma once

#include "data/snapshot.h"

#include <cstdint>
#include <string>

namespace status {

enum class Level { Ok, Warn, Crit, Neutral };

Level level_percent(double pct);   // <70 Ok, 70..90 Warn, >90 Crit
Level level_temp(double c);        // <65 Ok, 65..80 Warn, >80 Crit
Level level_container(const ContainerInfo& c);

std::string format_uptime(double seconds);   // "12d 4h", "3h 5m", "42m", "<1m"
std::string format_bytes_kb(uint64_t kb);    // "6.5G", "512M"
std::string format_rate(double bytes_per_s);  // "0 B/s", "1.5 KB/s", "120 KB/s", "1.2 MB/s"
bool net_active(const NetRate& r);           // >= 10 KB/s in either direction
std::string short_status(const std::string& docker_status);  // "Up 3d", "Exited (0)"

}  // namespace status
