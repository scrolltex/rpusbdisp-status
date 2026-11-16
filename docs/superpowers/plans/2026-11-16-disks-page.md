# Disks Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Disks page (space, NVMe temperature, SMART, activity, NFS-safe), page order Disks · Overview · Docker with relative page numbers and 60 s auto-return.

**Architecture:** Pure parsers in `src/data/disks.{h,cpp}` (mountinfo, block-device resolution, diskstats, mountstats, SMART JSON); `NfsProbe` runs `statvfs` on worker threads with a 2 s deadline; `Collector` fills `std::vector<DiskInfo>` in `SystemSnapshot`; `DisksScreen` renders; `App` gains a home page and auto-return.

**Tech Stack:** C++17, LVGL 9.6, nlohmann/json, doctest, systemd timer + smartctl.

**Spec:** `docs/superpowers/specs/2026-11-16-disks-page-design.md`

## Global Constraints

- Space colours: < 80 % green, 80–90 % yellow, > 90 % red. NVMe temp: < 60 green, 60–70 yellow, > 70 red.
- SMART: FAIL if `smart_status.passed == false` or `critical_warning != 0`; WARN if `percentage_used >= 90` or `media_errors > 0`; else OK; file older than 30 min → stale (`OK?`); missing/unparsable → no badge.
- Filesystems kept: ext2 ext3 ext4 xfs btrfs f2fs zfs vfat exfat ntfs3 nfs nfs4. Skipped mount points: `/boot`, `/boot/*`, `/snap/*`, `/var/lib/docker/*`, `/run/*`, `/sys/*`, `/proc/*`. Dedupe by device (NFS: by source), keep shortest mount point. Order: `/`, other local, NFS.
- NFS: refresh every 5 s; unavailable if a request has not completed within 2 s or `statvfs` failed; stuck workers get no new requests and are never joined.
- Pages: relative numbers (`-1` Disks, `0` Overview, `1` Docker); `--start-page N` replaces `--start-tile`; auto-return to home after 60 000 ms inactivity.
- Defaults: `--smart-dir /run/rpusbdisp-status/smart`; `--disks a,b,c` overrides discovery.

## Review Focus

1. NFS server down: `statvfs` blocks forever → row unavailable within ~2 s, UI and other rows keep updating, `stop()` returns (test in Task 4 with a blocking probe function).
2. btrfs/zfs root (virtual `0:NN`) → falls back to `/dev/...` source; nothing resolvable → row with space only (test in Task 2).
3. Many mounts (bind mounts, docker overlays, snaps) → only real filesystems, deduplicated (test in Task 1).
4. SMART JSON missing fields / ATA disk / garbage → no crash, correct badge (test in Task 3).
5. Mount point names with spaces (mountinfo escapes `\040`) → decoded (test in Task 1).

---

### Task 1: Pager — home page, relative numbers, auto-return
**Files:** `src/ui/app.{h,cpp}`, `src/options.{h,cpp}`, `src/main.cpp`, `tests/test_app.cpp`, `tests/test_options.cpp`, README.
**Produces:** `void App::set_home(int index)`; `void App::create(int start_offset = 0)` (offset relative to home); `int App::home() const`; `void App::check_idle(uint32_t inactive_ms)` (called from the 500 ms UI timer with `lv_display_get_inactive_time`); `Options::start_page` (int, default 0), `--start-page`; `--start-tile` removed; `bool App::valid_offset(int) const`.
- [ ] Tests: 3 dummy screens, home 1: `create(0)` → `current()==1`; `create(-1)` → 0; `check_idle(59999)` on page 0 keeps 0; `check_idle(60000)` → 1 (after pump); options `--start-page -1` → -1; `--start-tile 1` → error.
- [ ] FAIL → implement → PASS → commit `feat: home page, relative page numbers, auto-return`.

### Task 2: Mounts and block devices
**Files:** create `src/data/disks.{h,cpp}`, `tests/test_disks.cpp`, fixtures `tests/fixtures/disks/mountinfo.txt`, `tests/fixtures/disks/sys/...` (fake sysfs tree), `tests/fixtures/disks/diskstats.txt`, `tests/fixtures/disks/mountstats.txt`.
**Produces:**
```cpp
struct MountEntry { std::string mount_point, fstype, source; unsigned major = 0, minor = 0; bool nfs = false; };
std::vector<MountEntry> parse_mounts(const std::string& mountinfo);              // filtered, deduped, ordered
std::vector<MountEntry> select_mounts(const std::vector<MountEntry>& all, const std::vector<std::string>& wanted); // --disks
struct BlockDev { std::string disk;  /* nvme0n1, sda */ std::string controller; /* nvme0, sda */ unsigned major = 0, minor = 0; };
std::optional<BlockDev> resolve_block_device(const MountEntry& m, const std::string& sysfs_root, const std::string& dev_root = "/dev");
struct IoCounters { uint64_t read_bytes = 0, write_bytes = 0; };
std::optional<IoCounters> parse_diskstats(const std::string& text, const std::string& disk);           // sectors*512
std::optional<IoCounters> parse_mountstats_nfs(const std::string& text, const std::string& mount_point); // bytes: f5,f6
std::optional<std::string> find_disk_temp_input(const BlockDev& d, const std::string& sysfs_root);
```
- [ ] Tests: mountinfo fixture with `/` ext4 on 259:2, `/home` bind of same device, `/var/lib/docker/overlay2/x` overlay, `/snap/core/1` squashfs, `/boot/efi` vfat, `/mnt/media` nfs4 `nas:/media`, `/mnt/my\040backup` nfs `nas:/backup`, `/mnt/media2` same NFS source as `/mnt/media` → result `/`, `/mnt/media`, `/mnt/my backup` in that order; `select_mounts` keeps given order and drops unknown; `resolve_block_device` with fake sysfs (`dev/block/259:2 -> ../../block/nvme0n1/nvme0n1p2`, `block/nvme0n1/device -> ../../nvme0`) → disk `nvme0n1`, controller `nvme0`; btrfs mount 0:31 with source `/dev/nvme0n1p2` (dev_root fixture with a file → fall back by name lookup under `sys/class/block/nvme0n1p2`) → same; unresolvable → nullopt; diskstats `nvme0n1` sectors 2048/4096 → 1048576/2097152; mountstats block for `/mnt/media` bytes line → fields 5/6; `find_disk_temp_input` → `.../nvme0/hwmon3/temp1_input`.
- [ ] FAIL → implement → PASS → commit `feat: mount and block device discovery`.

### Task 3: SMART
**Files:** `src/data/disks.{h,cpp}`, `tests/test_disks.cpp`, fixtures `tests/fixtures/smart/{nvme_ok,nvme_worn,nvme_fail,ata_ok}.json`.
**Produces:** `enum class SmartState { Unknown, Ok, Warn, Fail }; struct SmartInfo { SmartState state = SmartState::Unknown; std::optional<int> wear_pct; bool stale = false; }; SmartInfo parse_smart_json(const std::string& json); SmartInfo read_smart(const std::string& dir, const std::string& controller, std::chrono::system_clock::time_point now);` (stale if mtime older than 30 min).
- [ ] Tests: ok → Ok wear 3; worn (percentage_used 92) → Warn; media_errors 1 → Warn; critical_warning 4 → Fail; passed false → Fail; ata_ok → Ok no wear; garbage → Unknown; missing file → Unknown; mtime 31 min old → stale.
- [ ] FAIL → implement → PASS → commit `feat: read SMART results`.

### Task 4: NFS probe
**Files:** create `src/data/nfs_probe.{h,cpp}`, `tests/test_nfs_probe.cpp`.
**Produces:** `struct SpaceInfo { uint64_t total = 0, used = 0; }; using StatvfsFn = std::function<std::optional<SpaceInfo>(const std::string&)>; std::optional<SpaceInfo> statvfs_space(const std::string& path); class NfsProbe { public: NfsProbe(std::string path, StatvfsFn fn = statvfs_space, std::chrono::milliseconds deadline = 2000ms); void request(); /* no-op if a call is in flight */ struct Result { bool available = false; SpaceInfo space; }; Result result() const; };` Worker detached; shared state via `shared_ptr` so a stuck worker outliving the probe is safe.
- [ ] Tests: fast fn → available after request; fn returning nullopt → unavailable; fn blocking on a condition (never released during the check) → `result().available == false` 2.1 s after request (use deadline 100 ms in test), second `request()` does not start another call (counter == 1), destroying the probe returns immediately; release the fn at test end.
- [ ] FAIL → implement → PASS → commit `feat: NFS probe with deadline`.

### Task 5: Collector
**Files:** `src/data/snapshot.h`, `src/data/collector.{h,cpp}`, `src/options.{h,cpp}`, `src/main.cpp`, `tests/test_collector.cpp`.
**Produces:** in `snapshot.h`: `enum class SmartState` (moved here) and `struct DiskInfo { std::string mount_point, device; bool nfs = false, available = true; uint64_t total = 0, used = 0; std::optional<double> temp_c; SmartState smart = SmartState::Unknown; std::optional<int> wear_pct; bool smart_stale = false; std::optional<NetRate> io; /* rx=read, tx=write */ };` `SystemSnapshot::disks`. `CollectorConfig::{mountinfo_path (default <proc_root>/self/mountinfo), sysfs_root, smart_dir, disks (vector<string>)}`; options `--smart-dir`, `--disks`.
- Local: statvfs + resolve + temp + diskstats rate every 1 s; mounts re-read every 5 s. NFS: one `NfsProbe` per mount kept across samples, `request()` every 5 s, IO from mountstats.
- [ ] Tests: fixture proc/sys roots with a local mount pointing at a temp dir and an NFS entry whose probe fn is injected (`CollectorConfig::nfs_statvfs` test hook) → snapshot has both, local `device == "nvme0"`, NFS blocking fn → `available == false` while CPU/RAM still update.
- [ ] FAIL → implement → PASS → commit `feat: collect disk information`.

### Task 6: Disks screen
**Files:** create `src/ui/screen_disks.{h,cpp}`; `src/ui/format.{h,cpp}` (`Level level_disk_pct(double)`, `Level level_disk_temp(double)`, `std::string format_size(uint64_t bytes)`; `"465G"`, `"3.5T"`, `"512M"`), `src/main.cpp` (add order Disks, Overview, Docker; `set_home(1)`), `tests/test_format.cpp`, `tests/test_app.cpp`.
- [ ] Tests: `level_disk_pct(79.9)==Ok`, `(80)==Warn`, `(90)==Warn`, `(90.1)==Crit`; `level_disk_temp(59.9)==Ok`, `(60)==Warn`, `(70.5)==Crit`; `format_size`: 499289948160 → "465G", 3.5 TiB → "3.5T"; DisksScreen with 40 rows survives and has 40 row children; unavailable row has no bar.
- [ ] FAIL → implement → PASS; dump screenshots (normal, unavailable NFS, 180°) look right → commit `feat: disks page`.

### Task 7: Deploy and docs
**Files:** create `deploy/smart-collect.sh`, `deploy/rpusbdisp-smart.service`, `deploy/rpusbdisp-smart.timer`; modify `deploy/install.sh`, `deploy/status-display.service` (`ProtectHome=read-only`), `tests/check_deploy.sh`, `CMakeLists.txt` (install), README, AGENTS.md.
- [ ] `check_deploy.sh` additionally asserts: status unit has `ProtectHome=read-only`; smart timer has `OnUnitActiveSec=10min`; `bash -n` on smart-collect.sh; smart-collect.sh run with `SMARTCTL=<fake script>` and `OUT_DIR=<tmp>` and `DEVICES=/dev/nvme0` writes `<tmp>/nvme0.json`, and does not write when the fake prints nothing.
- [ ] FAIL → implement → PASS → commit `feat: SMART timer and deploy updates`.
