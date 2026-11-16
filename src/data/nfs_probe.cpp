#include "data/nfs_probe.h"

#include <mutex>
#include <thread>

#include <sys/statvfs.h>

namespace status {

std::optional<SpaceInfo> statvfs_space(const std::string& path) {
    struct statvfs st {};
    if (::statvfs(path.c_str(), &st) != 0 || st.f_blocks == 0) return std::nullopt;
    SpaceInfo s;
    s.total = uint64_t(st.f_blocks) * st.f_frsize;
    s.used = s.total - uint64_t(st.f_bfree) * st.f_frsize;
    return s;
}

// Lives as long as the probe or a worker that still holds it.
struct NfsProbe::Shared {
    std::string path;
    StatvfsFn fn;
    std::chrono::milliseconds deadline;
    mutable std::mutex mu;
    bool in_flight = false;
    std::chrono::steady_clock::time_point issued;
    bool ok = false;
    SpaceInfo space;
};

NfsProbe::NfsProbe(std::string path, StatvfsFn fn, std::chrono::milliseconds deadline)
    : s_(std::make_shared<Shared>()) {
    s_->path = std::move(path);
    s_->fn = std::move(fn);
    s_->deadline = deadline;
}

void NfsProbe::request() {
    {
        std::lock_guard<std::mutex> lk(s_->mu);
        if (s_->in_flight) return;  // previous call still running (maybe stuck)
        s_->in_flight = true;
        s_->issued = std::chrono::steady_clock::now();
    }
    std::thread([s = s_] {
        std::optional<SpaceInfo> r = s->fn(s->path);
        std::lock_guard<std::mutex> lk(s->mu);
        s->in_flight = false;
        s->ok = r.has_value();
        if (r) s->space = *r;
    }).detach();
}

NfsProbe::Result NfsProbe::result() const {
    std::lock_guard<std::mutex> lk(s_->mu);
    const bool overdue = s_->in_flight && std::chrono::steady_clock::now() - s_->issued > s_->deadline;
    Result r;
    r.available = s_->ok && !overdue;
    r.space = s_->space;
    return r;
}

}  // namespace status
