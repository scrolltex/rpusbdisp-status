#include "data/system_stats.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace status {

namespace {

std::optional<std::string> read_first_line(const fs::path& p) {
    std::ifstream f(p);
    if (!f) return std::nullopt;
    std::string line;
    std::getline(f, line);
    return line;
}

std::vector<fs::path> sorted_entries(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        out.push_back(it->path());
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::optional<CpuTimes> parse_proc_stat(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("cpu ", 0) != 0) continue;
        std::istringstream ls(line.substr(4));
        // user nice system idle iowait irq softirq steal [guest guest_nice]
        uint64_t v[8] = {};
        int n = 0;
        while (n < 8 && ls >> v[n]) ++n;
        if (n < 4) return std::nullopt;
        CpuTimes t;
        for (int i = 0; i < n; ++i) t.total += v[i];  // guest is already part of user
        t.idle = v[3] + (n > 4 ? v[4] : 0);
        return t;
    }
    return std::nullopt;
}

double cpu_percent(const CpuTimes& prev, const CpuTimes& cur) {
    if (cur.total <= prev.total || cur.idle < prev.idle) return 0.0;
    const double total = double(cur.total - prev.total);
    const double idle = double(cur.idle - prev.idle);
    return std::clamp(100.0 * (total - idle) / total, 0.0, 100.0);
}

std::optional<MemInfo> parse_meminfo(const std::string& text) {
    std::istringstream in(text);
    std::string key;
    uint64_t value = 0;
    std::string unit;
    MemInfo m;
    bool have_total = false, have_avail = false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        if (!(ls >> key >> value)) continue;
        if (key == "MemTotal:") { m.total_kb = value; have_total = true; }
        else if (key == "MemAvailable:") { m.available_kb = value; have_avail = true; }
    }
    if (!have_total || !have_avail) return std::nullopt;
    return m;
}

std::optional<double> parse_uptime(const std::string& text) {
    std::istringstream in(text);
    double v = 0;
    if (!(in >> v)) return std::nullopt;
    return v;
}

std::optional<std::string> find_temp_source(const std::string& sysfs_root) {
    const fs::path root(sysfs_root);
    std::error_code ec;

    for (const auto& hw : sorted_entries(root / "class/hwmon")) {
        auto name = read_first_line(hw / "name");
        if (!name) continue;
        if (*name == "coretemp" || *name == "k10temp" || *name == "zenpower") {
            auto p = hw / "temp1_input";
            if (fs::exists(p, ec)) return p.string();
        }
    }
    for (const auto& zone : sorted_entries(root / "class/thermal")) {
        if (zone.filename().string().rfind("thermal_zone", 0) != 0) continue;
        auto type = read_first_line(zone / "type");
        if (type && *type == "x86_pkg_temp" && fs::exists(zone / "temp", ec)) return (zone / "temp").string();
    }
    auto zone0 = root / "class/thermal/thermal_zone0/temp";
    if (fs::exists(zone0, ec)) return zone0.string();
    return std::nullopt;
}

std::optional<std::string> default_route_iface(const std::string& route_text) {
    std::istringstream in(route_text);
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string iface, dest, gateway, flags;
        if (!(ls >> iface >> dest >> gateway >> flags)) continue;
        unsigned long f = std::strtoul(flags.c_str(), nullptr, 16);
        if (dest == "00000000" && (f & 0x1) && (f & 0x2)) return iface;  // RTF_UP | RTF_GATEWAY
    }
    return std::nullopt;
}

std::optional<NetCounters> parse_net_dev(const std::string& text, const std::string& iface) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        name.erase(0, name.find_first_not_of(" \t"));
        if (name != iface) continue;
        std::istringstream ls(line.substr(colon + 1));
        uint64_t v[9] = {};
        for (auto& x : v)
            if (!(ls >> x)) return std::nullopt;
        return NetCounters{v[0], v[8]};  // rx bytes, tx bytes
    }
    return std::nullopt;
}

NetRate net_rate(const NetCounters& prev, const NetCounters& cur, double seconds) {
    NetRate r;
    if (seconds <= 0) return r;
    if (cur.rx_bytes >= prev.rx_bytes) r.rx_bps = double(cur.rx_bytes - prev.rx_bytes) / seconds;
    if (cur.tx_bytes >= prev.tx_bytes) r.tx_bps = double(cur.tx_bytes - prev.tx_bytes) / seconds;
    return r;
}

std::optional<double> read_temp_c(const std::string& path) {
    auto line = read_first_line(path);
    if (!line) return std::nullopt;
    std::istringstream in(*line);
    long milli = 0;
    if (!(in >> milli)) return std::nullopt;
    return milli / 1000.0;
}

}  // namespace status
