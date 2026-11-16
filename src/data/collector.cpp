#include "data/collector.h"

#include "data/docker_client.h"
#include "data/system_stats.h"

#include <fstream>
#include <optional>

#include <unistd.h>

namespace status {

namespace {

std::string slurp(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

std::string hostname() {
    char buf[256] = {};
    if (::gethostname(buf, sizeof(buf) - 1) != 0) return "server";
    return buf;
}

}  // namespace

Collector::Collector(CollectorConfig cfg) : cfg_(std::move(cfg)) {}

Collector::~Collector() { stop(); }

void Collector::start() {
    std::lock_guard<std::mutex> lk(mu_);
    if (sys_th_.joinable()) return;
    stop_ = false;
    snap_.sys.hostname = hostname();
    snap_.docker.error = "loading...";
    sys_th_ = std::thread(&Collector::run_system, this);
    docker_th_ = std::thread(&Collector::run_docker, this);
}

void Collector::stop() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (sys_th_.joinable()) sys_th_.join();
    if (docker_th_.joinable()) docker_th_.join();
}

Snapshot Collector::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}

bool Collector::wait_for(std::chrono::milliseconds d) {
    std::unique_lock<std::mutex> lk(mu_);
    return !cv_.wait_for(lk, d, [this] { return stop_; });
}

void Collector::run_system() {
    std::optional<CpuTimes> prev;
    std::string net_prev_iface;
    std::optional<NetCounters> net_prev;
    std::chrono::steady_clock::time_point net_prev_time;
    std::string temp_path = cfg_.temp_path;
    if (temp_path.empty()) {
        if (auto t = find_temp_source(cfg_.sysfs_root)) temp_path = *t;
    }
    do {
        SystemSnapshot s;
        s.hostname = hostname();
        auto cur = parse_proc_stat(slurp(cfg_.proc_root + "/stat"));
        if (cur && prev) s.cpu = cpu_percent(*prev, *cur);
        if (cur) prev = cur;
        if (auto m = parse_meminfo(slurp(cfg_.proc_root + "/meminfo"))) s.mem = *m;
        if (auto u = parse_uptime(slurp(cfg_.proc_root + "/uptime"))) s.uptime_s = *u;
        if (!temp_path.empty()) s.temp_c = read_temp_c(temp_path);

        // Network: resolved every sample so route changes / late interfaces are picked up.
        std::optional<std::string> iface;
        if (!cfg_.net_iface.empty()) iface = cfg_.net_iface;
        else iface = default_route_iface(slurp(cfg_.proc_root + "/net/route"));
        std::optional<NetCounters> counters;
        if (iface) counters = parse_net_dev(slurp(cfg_.proc_root + "/net/dev"), *iface);
        const auto now = std::chrono::steady_clock::now();
        if (counters && net_prev && *iface == net_prev_iface) {
            s.net.ok = true;
            s.net.iface = *iface;
            s.net.rate = net_rate(*net_prev, *counters,
                                  std::chrono::duration<double>(now - net_prev_time).count());
        }
        net_prev = counters;
        net_prev_iface = iface ? *iface : std::string();
        net_prev_time = now;

        std::lock_guard<std::mutex> lk(mu_);
        snap_.sys = std::move(s);
    } while (wait_for(cfg_.sys_period));
}

void Collector::run_docker() {
    do {
        DockerSnapshot d = fetch_docker(cfg_.docker_socket);
        std::lock_guard<std::mutex> lk(mu_);
        snap_.docker = std::move(d);
    } while (wait_for(cfg_.docker_period));
}

}  // namespace status
