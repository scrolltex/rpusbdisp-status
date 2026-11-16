# Disks page — design

Date: 2026-11-16 · Status: awaiting user approval · Size: project

## Why
The server has one NVMe SSD and a couple of NFS mounts. The user wants to see
at a glance whether disk space is running out, how hot the NVMe is, and whether
SMART reports a problem. Disk activity is nice to have, shown small.

## Goals
1. New **Disks** page. Page order becomes **Disks · Overview · Docker**;
   the program starts on Overview (the "home" page).
2. **Auto-return:** after 60 s without touch the pager slides back to Overview.
3. Per mounted filesystem (local and NFS): mount point, used/total, percentage
   bar coloured by fullness (< 80 % green, 80–90 % yellow, > 90 % red).
4. For local disks: device name (`nvme0`), temperature (< 60 °C green,
   60–70 yellow, > 70 red), SMART badge `OK` / `WARN` / `FAIL` and wear %.
5. Small dim line per row: `R 12 MB/s  W 3 MB/s` (always visible, no tap).
6. NFS server gone → that row turns red `unavailable`; nothing else is affected,
   the program never hangs.

## Non-goals
- Changing anything on disks; SMART self-tests; per-process IO.
- Partition tables, inode usage, RAID/ZFS pool health.
- IPv6/other network filesystems beyond `nfs`/`nfs4` (SMB etc. may appear as
  plain rows without activity — not designed for).

## Anti-goals
- A dead NFS mount freezing the UI, the collector, or shutdown beyond the
  systemd stop timeout.
- Giving the display program root or `CAP_SYS_ADMIN`.

## Constraints
- Existing architecture: `Collector` threads fill `Snapshot`; screens implement
  `Screen`; tests with doctest and fixtures; dump backend for screenshots.
- Service runs as unprivileged `statusdisp`; unit uses `ProtectSystem=strict`.
- Target: Ubuntu Server 26.04, smartmontools from apt.

## Approaches (SMART access)
- **Chosen — A: root timer writes JSON.** `rpusbdisp-smart.timer` (root,
  every 10 min, first run 1 min after boot) runs `deploy/smart-collect.sh`:
  for each `/dev/nvme[0-9]*` controller (and `/dev/sd[a-z]`) it runs
  `smartctl --json=c -H -A <dev>` and atomically writes
  `/run/rpusbdisp-status/smart/<name>.json` (0644). The program only reads.
- Rejected — B: `CAP_SYS_ADMIN` + NVMe admin ioctl: near-root privilege for a
  UI program, NVMe-only code.
- Rejected: smartd state files (almost no NVMe data); no SMART at all (user
  wants SMART).

## Design (author's calls unless marked as user decision)

### Which filesystems
- Parse `/proc/self/mountinfo`. Keep fstype ∈ {ext2, ext3, ext4, xfs, btrfs,
  f2fs, zfs, vfat, exfat, ntfs3, nfs, nfs4}.
- Skip mount points `/boot`, `/boot/*`, `/snap/*`, `/var/lib/docker/*`,
  `/run/*`, `/sys/*`, `/proc/*`.
- Same device (major:minor; for NFS the `server:/export` source) mounted
  several times (bind mounts, btrfs subvolumes) → one row, shortest mount point.
- Order: `/` first, then local by mount point, then NFS by mount point.
- `--disks /,/mnt/media` overrides the automatic list (exact mount points,
  in the given order).

### Space
- `statvfs`: total = `f_blocks*f_frsize`, used = total − `f_bfree*f_frsize`
  (same as `df`'s "Used" + reserved counted as used is acceptable),
  percent = used/total.
- Local filesystems: `statvfs` in the 1 s system loop (never blocks).
- NFS: one worker thread per NFS mount, asked to refresh every 5 s. The row
  is `available = false` when the latest request has not completed within 2 s
  of being issued, or `statvfs` returned an error; otherwise the last
  successful result is shown (up to ~5 s old, which is fine). A worker stuck
  in `statvfs` gets no new requests while stuck (no thread leak); worker
  threads are detached and never joined, so `Collector::stop()` cannot block
  on them.

### Device, temperature, activity (local)
- mountinfo `major:minor` → `/sys/dev/block/M:m` → resolve to the whole disk
  (partition's parent directory, e.g. `nvme0n1p2` → `nvme0n1`).
- Fallback when that path does not exist (btrfs/zfs report a virtual `0:NN`):
  if the mount source is a `/dev/...` path, `stat()` it and use `st_rdev`.
  Neither works → the row has space only (no device/temp/SMART/activity).
- NVMe controller name: `/sys/block/nvme0n1/device` → `nvme0`. Temperature:
  `/sys/class/nvme/nvme0/hwmon*/temp1_input` (millidegrees); SATA: the
  device's `hwmon*` if present, else none.
- Activity: `/proc/diskstats` sectors read (field 6) / written (field 10) of the
  whole disk × 512, rate over elapsed time, counter decrease → 0.

### Activity (NFS)
- `/proc/self/mountstats`, block of the mount (`device <src> mounted on <mp>
  with fstype nfs…`), line `bytes:` fields 5 and 6 (server read / server
  write bytes). Reading this file does not contact the server.

### SMART
- Program reads `<smart_dir>/<controller>.json` (default
  `/run/rpusbdisp-status/smart`, option `--smart-dir`).
- `FAIL`: `smart_status.passed == false` or
  `nvme_smart_health_information_log.critical_warning != 0`.
- `WARN`: `percentage_used >= 90` or `media_errors > 0`.
- `OK` otherwise. Wear shown as `percentage_used` %.
- File missing / unparsable → no badge. File older than 30 min → badge dim
  with `?` (`OK?`). `smartctl` exit status is ignored by the script (non-zero
  bits are normal); empty output is not written.
- ATA disks: only `smart_status.passed` → `OK`/`FAIL`, no wear.

### UI
Agreed sketch (user approved the content; exact pixels are the builder's):
```
┌───────────────────────────────┐
│ Disks                         │
│ /         nvme0  48°C  OK 3%  │
│ █████████░░░░░░░  62% 290/465G│
│  R 12 MB/s  W 3 MB/s          │
│ /mnt/media      nfs           │
│ ██████████████░░  88% 3.5/4T  │
│  R 0 B/s  W 0 B/s             │
│ /mnt/backup     unavailable   │
│            ● ○ ○              │
└───────────────────────────────┘
```
- Rows: title `Disks`; per row: line 1 mount point
  (left) + device, temperature, SMART badge, wear (right; NFS shows `nfs`);
  line 2 bar + `62%  290/465G`; line 3 dim `R … W …` (`format_rate`).
- More rows than fit → the list scrolls (like Docker).
- Unavailable NFS: mount point stays on the left, red `unavailable` on the
  right, no bar and no activity line.
- Rebuild widgets only when the set of rows changes; otherwise update values.

### Pager
- `App::add` order: Disks, Overview, Docker; `App::set_home(1)`.
- Page numbers are relative to home (user decision): `< 0` left of home,
  `0` home, `> 0` right. With the current pages: `-1` Disks, `0` Overview,
  `1` Docker. `--start-page N` (default 0) replaces `--start-tile`, which is
  removed (debug-only, not used in the unit). Out-of-range N → error at
  startup listing the valid range.
- Auto-return: when not on home and `lv_display_get_inactive_time()` ≥ 60 000 ms
  → `show(home, animate)`. User decision: 60 s.

### Deploy
- `deploy/rpusbdisp-smart.service` (Type=oneshot, root) +
  `deploy/rpusbdisp-smart.timer` (OnBootSec=1min, OnUnitActiveSec=10min),
  `deploy/smart-collect.sh`. `install.sh` installs `smartmontools` if missing
  (apt), installs and enables the timer, runs it once.
- Status unit: `ProtectHome=read-only` instead of `yes` (otherwise `statvfs`
  on a `/home` mount fails).
- README documents `--start-page` and the relative numbering.

## Left to the builder
- Exact pixel layout within the agreed sketch; fixture contents; how the NFS
  worker hands results back (mutex/atomic).
