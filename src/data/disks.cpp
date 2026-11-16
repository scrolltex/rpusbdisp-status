#include "data/disks.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace status {

namespace {

// mountinfo / mountstats escape space, tab, newline and backslash as \ooo.
std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() && std::isdigit((unsigned char)s[i + 1]) &&
            std::isdigit((unsigned char)s[i + 2]) && std::isdigit((unsigned char)s[i + 3])) {
            out += char((s[i + 1] - '0') * 64 + (s[i + 2] - '0') * 8 + (s[i + 3] - '0'));
            i += 3;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool starts_with(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

bool skipped_mount_point(const std::string& mp) {
    if (mp == "/boot" || starts_with(mp, "/boot/")) return true;
    for (const char* p : {"/snap/", "/var/lib/docker/", "/run/", "/sys/", "/proc/"})
        if (starts_with(mp, p)) return true;
    return mp == "/run" || mp == "/sys" || mp == "/proc";
}

bool real_fstype(const std::string& t) {
    static const std::set<std::string> k = {"ext2", "ext3",  "ext4",  "xfs",   "btrfs", "f2fs", "zfs",
                                            "vfat", "exfat", "ntfs3", "nfs",   "nfs4"};
    return k.count(t) != 0;
}

std::optional<BlockDev> from_sys_dir(const fs::path& dir) {
    std::error_code ec;
    fs::path p = fs::canonical(dir, ec);
    if (ec) return std::nullopt;
    if (fs::exists(p / "partition", ec)) p = p.parent_path();
    BlockDev d;
    d.disk = p.filename().string();
    d.controller = d.disk;
    if (starts_with(d.disk, "nvme")) {
        fs::path c = fs::canonical(p / "device", ec);
        if (!ec) d.controller = c.filename().string();
    }
    return d;
}

}  // namespace

std::vector<MountEntry> parse_mounts(const std::string& mountinfo) {
    std::vector<MountEntry> all;
    std::istringstream in(mountinfo);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string id, parent, majmin, root, mp, opts, tok;
        if (!(ls >> id >> parent >> majmin >> root >> mp >> opts)) continue;
        while (ls >> tok && tok != "-") {
        }
        MountEntry m;
        if (!(ls >> m.fstype >> m.source)) continue;
        m.mount_point = unescape(mp);
        m.source = unescape(m.source);
        size_t colon = majmin.find(':');
        if (colon == std::string::npos) continue;
        m.major = unsigned(std::stoul(majmin.substr(0, colon)));
        m.minor = unsigned(std::stoul(majmin.substr(colon + 1)));
        m.nfs = m.fstype == "nfs" || m.fstype == "nfs4";
        if (!real_fstype(m.fstype) || skipped_mount_point(m.mount_point)) continue;
        all.push_back(std::move(m));
    }

    // One row per device (NFS: per export), shortest mount point wins.
    auto key = [](const MountEntry& m) {
        return m.nfs ? "nfs:" + m.source : std::to_string(m.major) + ":" + std::to_string(m.minor);
    };
    std::sort(all.begin(), all.end(), [](const MountEntry& a, const MountEntry& b) {
        if (a.mount_point.size() != b.mount_point.size()) return a.mount_point.size() < b.mount_point.size();
        return a.mount_point < b.mount_point;
    });
    std::vector<MountEntry> out;
    std::set<std::string> seen;
    for (auto& m : all)
        if (seen.insert(key(m)).second) out.push_back(m);

    std::sort(out.begin(), out.end(), [](const MountEntry& a, const MountEntry& b) {
        auto rank = [](const MountEntry& m) { return m.mount_point == "/" ? 0 : (m.nfs ? 2 : 1); };
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return a.mount_point < b.mount_point;
    });
    return out;
}

std::vector<MountEntry> select_mounts(const std::vector<MountEntry>& all, const std::vector<std::string>& wanted) {
    std::vector<MountEntry> out;
    for (const auto& w : wanted)
        for (const auto& m : all)
            if (m.mount_point == w) {
                out.push_back(m);
                break;
            }
    return out;
}

std::optional<BlockDev> resolve_block_device(const MountEntry& m, const std::string& sysfs_root) {
    if (m.nfs) return std::nullopt;
    const fs::path root(sysfs_root);
    std::error_code ec;
    fs::path by_num = root / "dev/block" / (std::to_string(m.major) + ":" + std::to_string(m.minor));
    if (fs::exists(by_num, ec)) return from_sys_dir(by_num);
    // btrfs/zfs report a virtual 0:NN; fall back to the /dev/... source.
    if (!starts_with(m.source, "/dev/")) return std::nullopt;
    fs::path dev(m.source);
    fs::path real = fs::canonical(dev, ec);  // /dev/mapper/x -> /dev/dm-0 on a real system
    std::string name = (!ec ? real : dev).filename().string();
    fs::path by_name = root / "class/block" / name;
    if (fs::exists(by_name, ec)) return from_sys_dir(by_name);
    return std::nullopt;
}

std::optional<std::string> find_disk_temp_input(const BlockDev& d, const std::string& sysfs_root) {
    const fs::path root(sysfs_root);
    std::error_code ec;
    std::vector<fs::path> dirs = {root / "class/nvme" / d.controller, root / "class/block" / d.disk / "device/hwmon"};
    for (const auto& dir : dirs) {
        if (!fs::exists(dir, ec)) continue;
        std::vector<fs::path> hw;
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
            if (starts_with(it->path().filename().string(), "hwmon")) hw.push_back(it->path());
        std::sort(hw.begin(), hw.end());
        for (const auto& h : hw)
            if (fs::exists(h / "temp1_input", ec)) return (h / "temp1_input").string();
    }
    return std::nullopt;
}

std::optional<IoCounters> parse_diskstats(const std::string& text, const std::string& disk) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        unsigned long maj, min;
        std::string name;
        uint64_t f[7] = {};
        if (!(ls >> maj >> min >> name) || name != disk) continue;
        for (auto& x : f)
            if (!(ls >> x)) return std::nullopt;
        // reads, reads merged, sectors read, ms reading, writes, writes merged, sectors written
        return IoCounters{f[2] * 512, f[6] * 512};
    }
    return std::nullopt;
}

std::optional<IoCounters> parse_mountstats_nfs(const std::string& text, const std::string& mount_point) {
    std::istringstream in(text);
    std::string line;
    bool inside = false;
    while (std::getline(in, line)) {
        if (starts_with(line, "device ")) {
            std::istringstream ls(line);
            std::string dev, src, mounted, on, mp;
            ls >> dev >> src >> mounted >> on >> mp;
            inside = unescape(mp) == mount_point;
            continue;
        }
        if (!inside) continue;
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag != "bytes:") continue;
        uint64_t f[6] = {};
        for (auto& x : f)
            if (!(ls >> x)) return std::nullopt;
        return IoCounters{f[4], f[5]};  // server read / server write bytes
    }
    return std::nullopt;
}

}  // namespace status
