# Known issues

Open findings from code reviews and hardware testing that were deliberately
not fixed yet. Each entry is self-contained so it can become a GitHub issue.
Remove an entry when it is fixed.

## Display / hardware

### Rare white stripes while scrolling the Docker list
Coalescing blits and limiting updates to 16 fps removed the stripes on the
Overview page, but short white vertical stripes still flash occasionally while
scrolling the Docker list, even at `--fps 8`. Not yet tried: sending bitblt
without RLE compression (the SDK's RLE encoder is correct — fuzz-tested — but
the firmware might handle large RLE payloads badly). Needs an SDK switch.

### SDL backend never compiled
`-DSTATUS_SDL=ON` has not been built anywhere yet (no SDL2 in the development
sandbox). It may not compile.

### "open failed" logged every 2 s
`src/backend/rpusbdisp_backend.cpp` (`usb_opener`): when the device exists but
cannot be opened (e.g. udev rule not applied), the error is printed on every
retry, flooding the journal. Log once until the state changes.

### Quick taps can be lost
`src/backend/partial_backend.cpp` keeps only the latest touch state; a press
and release that both happen between two LVGL input reads (~33 ms) are never
seen. Queue touch events instead.

## Overview

### RAM "used" can underflow
`src/ui/screen_overview.cpp`: `total_kb - available_kb` is unsigned; if
`MemAvailable > MemTotal` (seen in some containers) the value wraps. Clamp.

### Temperature source resolved once
`src/data/collector.cpp`: the temperature file is found at startup only; a
sensor module loaded later leaves `--` until restart.

### NET: rounding at unit boundaries
`src/ui/format.cpp` (`format_rate`): values just below a unit step print as
`1024 KB/s`, and `100.0 KB/s` appears although "no decimals from 100 up" was
intended.

### NET: interface not visible
The chosen interface (`NetSnapshot::iface`) is never shown or logged, so a wrong
automatic pick is hard to diagnose. Log it when it changes.

### NET value jitters horizontally
The value label is right-aligned with variable width, so the arrows shift as
digits change. Use fixed-width fields.

## Docker

### List jumps to the top on status changes
`src/ui/screen_docker.cpp` rebuilds the whole list whenever any status text
changes (e.g. `Up 3m` → `Up 4m`), resetting the scroll position. Update rows
in place when the set of containers is unchanged.

### EINTR reported as a timeout
`src/data/docker_client.cpp`: `poll()` returning -1 with `EINTR` during connect
or write is reported as "timeout" instead of being retried.

### Dead code
`src/data/docker_client.cpp` (`fetch_docker`) parses the JSON twice;
`src/ui/format.cpp` (`short_status`) has an unused `article` variable.

## Disks

### NFS rows flash "unavailable" at start
`src/data/nfs_probe.cpp`: before the first probe completes, rows show red
`unavailable` for about a second after start (or after a new mount), and the
page rebuilds twice. Treat "no result yet, not overdue" as pending.

### NFS failure detection takes up to ~8 s
5 s probe period + 2 s deadline + sampling. Matches the design; noted here so
nobody is surprised.

### `--start-page` error lacks the valid range
`src/main.cpp`: the out-of-range message does not list valid values, and the
check runs after the backend and collector have started.

### Exceptions that would terminate the process
`std::stoul` in `parse_mounts_all` (`src/data/disks.cpp`) on malformed
mountinfo, and a failing `std::thread` constructor in `NfsProbe::request()`
(which would also leave `in_flight` set forever). Both very unlikely.

### NVMe multipath not resolved (unverified)
Drives exposed under `nvme-subsysN` may resolve their controller as
`nvme-subsysN`, losing temperature and SMART.

### RAID / multi-device volumes show only the first disk
md RAID, multi-PV LVM and multi-device btrfs take temperature and SMART from
the first underlying disk only.

### install.sh rough edges
`deploy/install.sh`: `enable --now` does not pick up a changed timer file on
reinstall (needs `systemctl restart rpusbdisp-smart.timer`); `apt-get install`
runs without `apt-get update`; the SMART service runs twice on first install.

## Tests

### Timing-based collector tests
`tests/test_collector.cpp`, `tests/test_disk_monitor.cpp` sleep a few hundred
milliseconds against short periods; they could flake on a heavily loaded
machine. Poll for the condition with a generous timeout instead.
