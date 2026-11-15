#pragma once

#include "data/snapshot.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace status {

struct CollectorConfig {
    std::string proc_root = "/proc";
    std::string sysfs_root = "/sys";
    std::string temp_path;  // empty: auto-detect under sysfs_root
    std::string docker_socket = "/var/run/docker.sock";
    std::chrono::milliseconds sys_period{1000};
    std::chrono::milliseconds docker_period{5000};
};

// Background thread that keeps an up-to-date Snapshot.
class Collector {
public:
    explicit Collector(CollectorConfig cfg);
    ~Collector();
    Collector(const Collector&) = delete;
    Collector& operator=(const Collector&) = delete;

    void start();
    void stop();
    Snapshot snapshot() const;

private:
    void run_system();
    void run_docker();
    bool wait_for(std::chrono::milliseconds d);

    CollectorConfig cfg_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    Snapshot snap_;
    std::thread sys_th_;
    std::thread docker_th_;  // separate so a slow Docker never delays CPU/RAM
};

}  // namespace status
