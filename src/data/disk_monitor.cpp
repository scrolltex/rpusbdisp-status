#include "data/disk_monitor.h"

#include "data/system_stats.h"

#include <fstream>

namespace status {

namespace {
std::string slurp(const std::string& path) {
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
}  // namespace

DiskMonitor::DiskMonitor(DiskMonitorConfig cfg) : cfg_(std::move(cfg)) {}

void DiskMonitor::refresh_mounts() {
    const std::string mountinfo = slurp(cfg_.proc_root + "/self/mountinfo");
    // --disks picks from all mounts, so /boot, bind mounts etc. can be chosen.
    auto mounts = cfg_.disks.empty() ? parse_mounts(mountinfo) : select_mounts(parse_mounts_all(mountinfo), cfg_.disks);

    std::vector<Row> next;
    for (auto& m : mounts) {
        // Keep existing rows (NFS probes, IO history) for mounts that are still there.
        bool reused = false;
        for (auto& r : rows_) {
            if (r.mount.mount_point == m.mount_point && r.mount.source == m.source) {
                next.push_back(std::move(r));
                reused = true;
                break;
            }
        }
        if (reused) continue;
        Row r;
        r.mount = m;
        if (m.nfs) {
            r.probe = std::make_unique<NfsProbe>(m.mount_point, cfg_.nfs_statvfs, cfg_.nfs_deadline);
        } else {
            r.dev = resolve_block_device(m, cfg_.sysfs_root);
            // Spinning disks: no hwmon reads (drivetemp would wake them); use SMART instead.
            if (r.dev && !is_rotational(*r.dev, cfg_.sysfs_root)) r.temp_input = find_disk_temp_input(*r.dev, cfg_.sysfs_root);
        }
        next.push_back(std::move(r));
    }
    rows_ = std::move(next);
    mounts_read_ = std::chrono::steady_clock::now();
    mounts_loaded_ = true;
}

std::vector<DiskInfo> DiskMonitor::sample() {
    const auto now = std::chrono::steady_clock::now();
    if (!mounts_loaded_ || now - mounts_read_ >= cfg_.mounts_period) refresh_mounts();

    const std::string diskstats = slurp(cfg_.proc_root + "/diskstats");
    std::string mountstats;
    for (const auto& r : rows_)
        if (r.mount.nfs) {
            mountstats = slurp(cfg_.proc_root + "/self/mountstats");  // never touches the NFS server
            break;
        }

    std::vector<DiskInfo> out;
    for (auto& r : rows_) {
        DiskInfo d;
        d.mount_point = r.mount.mount_point;
        d.nfs = r.mount.nfs;

        std::optional<IoCounters> io;
        if (r.mount.nfs) {
            if (now - r.last_nfs_request >= cfg_.nfs_period) {
                r.probe->request();
                r.last_nfs_request = now;
            }
            auto res = r.probe->result();
            d.available = res.available;
            d.total = res.space.total;
            d.used = res.space.used;
            io = parse_mountstats_nfs(mountstats, r.mount.mount_point);
        } else {
            if (auto sp = cfg_.local_statvfs(r.mount.mount_point)) {
                d.total = sp->total;
                d.used = sp->used;
            } else {
                d.available = false;  // e.g. no permission to reach the mount point
            }
            if (r.dev) {
                d.device = r.dev->controller;
                io = parse_diskstats(diskstats, r.dev->disk);
                auto smart = read_smart(cfg_.smart_dir, r.dev->controller, std::chrono::system_clock::now());
                d.smart = smart.state;
                d.wear_pct = smart.wear_pct;
                d.smart_stale = smart.stale;
                d.smart_asleep = smart.asleep;
                if (!r.temp_input) d.temp_c = smart.temp_c;
            }
            if (r.temp_input) d.temp_c = read_temp_c(*r.temp_input);
        }

        if (io && r.prev_io) {
            const double secs = std::chrono::duration<double>(now - r.prev_io_time).count();
            d.io = net_rate(NetCounters{r.prev_io->read_bytes, r.prev_io->write_bytes},
                            NetCounters{io->read_bytes, io->write_bytes}, secs);
        }
        r.prev_io = io;
        r.prev_io_time = now;
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace status
