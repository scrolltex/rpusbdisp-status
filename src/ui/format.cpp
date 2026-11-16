#include "ui/format.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace status {

Level level_percent(double pct) {
    if (pct < 70) return Level::Ok;
    if (pct <= 90) return Level::Warn;
    return Level::Crit;
}

Level level_temp(double c) {
    if (c < 65) return Level::Ok;
    if (c <= 80) return Level::Warn;
    return Level::Crit;
}

Level level_container(const ContainerInfo& c) {
    if (c.state == "running") return Level::Ok;
    if (c.state == "restarting" || c.state == "paused") return Level::Warn;
    if (c.state == "exited") return c.status.find("(0)") != std::string::npos ? Level::Neutral : Level::Crit;
    if (c.state == "dead") return Level::Crit;
    return Level::Neutral;  // created, removing, unknown
}

std::string format_uptime(double seconds) {
    long s = long(seconds);
    long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    char buf[32];
    if (d > 0) std::snprintf(buf, sizeof(buf), "%ldd %ldh", d, h);
    else if (h > 0) std::snprintf(buf, sizeof(buf), "%ldh %ldm", h, m);
    else if (m > 0) std::snprintf(buf, sizeof(buf), "%ldm", m);
    else return "<1m";
    return buf;
}

std::string format_bytes_kb(uint64_t kb) {
    char buf[32];
    const double gb = double(kb) / (1024.0 * 1024.0);
    if (gb >= 1.0) std::snprintf(buf, sizeof(buf), "%.1fG", std::floor(gb * 10) / 10);
    else std::snprintf(buf, sizeof(buf), "%.0fM", double(kb) / 1024.0);
    return buf;
}

std::string format_rate(double bps) {
    static const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int u = 0;
    while (bps >= 1024.0 && u < 3) {
        bps /= 1024.0;
        ++u;
    }
    char buf[32];
    if (u == 0 || bps >= 100.0) std::snprintf(buf, sizeof(buf), "%.0f %s", bps, units[u]);
    else std::snprintf(buf, sizeof(buf), "%.1f %s", bps, units[u]);
    return buf;
}

bool net_active(const NetRate& r) {
    const double threshold = 10.0 * 1024.0;
    return r.rx_bps >= threshold || r.tx_bps >= threshold;
}

namespace {

std::string unit_suffix(const std::string& unit) {
    if (unit.rfind("second", 0) == 0) return "s";
    if (unit.rfind("minute", 0) == 0) return "m";
    if (unit.rfind("hour", 0) == 0) return "h";
    if (unit.rfind("day", 0) == 0) return "d";
    if (unit.rfind("week", 0) == 0) return "w";
    if (unit.rfind("month", 0) == 0) return "mo";
    if (unit.rfind("year", 0) == 0) return "y";
    return "";
}

}  // namespace

std::string short_status(const std::string& st) {
    if (st.rfind("Exited", 0) == 0) {
        size_t close = st.find(')');
        return close != std::string::npos ? st.substr(0, close + 1) : "Exited";
    }
    if (st.rfind("Restarting", 0) == 0) return "Restarting";
    if (st.rfind("Up ", 0) != 0) return st.size() > 14 ? st.substr(0, 14) : st;

    if (st.find("(Paused)") != std::string::npos) return "Paused";
    const std::string flag = st.find("(unhealthy)") != std::string::npos ? " !" : "";
    std::string rest = st.substr(3);

    if (rest.rfind("Less than", 0) == 0) return "Up <1m" + flag;
    std::istringstream in(rest);
    std::string num, unit;
    in >> num >> unit;
    if (num == "About") {  // "About an hour", "About a minute"
        std::string article = unit;
        in >> unit;
        (void)article;
        num = "1";
    }
    std::string suf = unit_suffix(unit);
    if (suf.empty()) return "Up" + flag;
    if (suf == "s") return "Up <1m" + flag;
    return "Up " + num + suf + flag;
}

}  // namespace status
