# Network Speed on Overview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show current download/upload speed of the main network interface on the Overview screen.

**Architecture:** Pure parsers in `system_stats` (`/proc/net/route`, `/proc/net/dev`, rate) feed a `NetSnapshot` filled by the existing 1 s system loop of `Collector`; `OverviewScreen` renders one extra row.

**Tech Stack:** C++17, LVGL 9.6, doctest (existing).

**Spec:** `docs/superpowers/specs/2026-11-16-network-speed-design.md`

## Global Constraints

- Row text: `NET  ↓ <rx>  ↑ <tx>` using `LV_SYMBOL_DOWN` / `LV_SYMBOL_UP`; `--` when no rate.
- Units: bytes/s, steps of 1024: `B/s`, `KB/s`, `MB/s`, `GB/s`; one decimal below 100, none from 100 up.
- Activity threshold 10 * 1024 B/s in either direction → bright text, else dim; no warning colours.
- Interface: default route from `/proc/net/route`, overridable with `--net-iface NAME`; re-resolved every sample.
- Counter decrease → rate 0; first sample → no rate.

## Review Focus

1. No default route (offline server, `--net-iface` unset) → row shows `--`, no crash (test in Task 1: `default_route_iface` on a route table with no `00000000` destination → nullopt).
2. Interface disappears or is renamed between samples → `--`, then recovers (test in Task 2: collector with proc fixture lacking the interface → `net.ok == false`).
3. Counter wrap/reset (interface recreated) → 0, not a huge number (test in Task 1: `net_rate` with smaller second sample → 0).
4. Docker virtual interfaces present → only the chosen interface counted (test in Task 1: `parse_net_dev` returns per-interface counters; collector picks one).
5. Very high speed (>1 GB/s) → `GB/s`, text still fits (test in Task 3: `format_rate(1.5 * 1024^3) == "1.5 GB/s"`).

---

### Task 1: Parsers

**Files:** Modify `src/data/system_stats.{h,cpp}`, `src/data/snapshot.h`; Test `tests/test_system_stats.cpp`; fixtures `tests/fixtures/net_route.txt`, `tests/fixtures/net_route_nodefault.txt`, `tests/fixtures/net_dev.txt`.

**Interfaces — Produces:**
```cpp
struct NetCounters { uint64_t rx_bytes = 0, tx_bytes = 0; };
std::optional<std::string> default_route_iface(const std::string& route_text); // first line with Destination 00000000 and Flags & 0x2 (UG)
std::optional<NetCounters> parse_net_dev(const std::string& text, const std::string& iface);
struct NetRate { double rx_bps = 0, tx_bps = 0; };
NetRate net_rate(const NetCounters& prev, const NetCounters& cur, double seconds); // per direction: cur<prev → 0
// snapshot.h:
struct NetSnapshot { bool ok = false; std::string iface; NetRate rate; };  // SystemSnapshot gets `NetSnapshot net;`
```
- [ ] Tests: `default_route_iface(net_route.txt)` == `"enp3s0"` (fixture: docker0 and br-x routes first, then enp3s0 default); no-default fixture → nullopt; `parse_net_dev(net_dev.txt, "enp3s0")` → rx 123456789, tx 98765 (fixture includes lo, docker0, veth1a2b with other values); unknown iface → nullopt; `net_rate({1000,500},{3048,1524},2.0)` → rx 1024, tx 512; `net_rate({5000,5000},{100,6000},1.0)` → rx 0, tx 1000.
- [ ] Run tests → FAIL (undefined). Implement. Run → PASS. Commit `feat: network counters parsing`.

### Task 2: Collector

**Files:** Modify `src/data/collector.{h,cpp}`, `src/options.{h,cpp}`, `src/main.cpp`; Test `tests/test_collector.cpp`, `tests/test_options.cpp`; fixtures `tests/fixtures/proc/net/route`, `tests/fixtures/proc/net/dev`.

**Interfaces — Consumes** Task 1. **Produces:** `CollectorConfig::net_iface` (std::string, empty = auto); `Options::net_iface`, option `--net-iface NAME`.
- [ ] Tests: collector with fixture proc root (sys_period 50 ms) → after 200 ms `sys.net.ok == true`, `sys.net.iface == "enp3s0"`, rates ≥ 0 (static counters → 0); with `net_iface = "nope"` → `sys.net.ok == false`; `--net-iface eth1` → `o->net_iface == "eth1"`.
- [ ] FAIL → implement (in `run_system`: resolve iface each sample from `<proc_root>/net/route` unless configured; read `<proc_root>/net/dev`; keep previous counters + steady_clock time; `ok` only when a rate exists) → PASS. Commit `feat: collect network speed`.

### Task 3: Overview row

**Files:** Modify `src/ui/format.{h,cpp}`, `src/ui/screen_overview.{h,cpp}`; Test `tests/test_format.cpp`.

**Interfaces — Produces:** `std::string format_rate(double bytes_per_s);` `bool net_active(const NetRate& r);` (≥ 10240 B/s either way).
- [ ] Tests: `format_rate(0)=="0 B/s"`, `(512)=="512 B/s"`, `(1536)=="1.5 KB/s"`, `(120*1024)=="120 KB/s"`, `(1.2*1024*1024)=="1.2 MB/s"`, `(1.5*1024*1024*1024)=="1.5 GB/s"`; `net_active({10240,0})` true, `({10239,10239})` false.
- [ ] FAIL → implement → PASS.
- [ ] Row under TEMP: label `NET` (14, dim) + value label (20): `LV_SYMBOL_DOWN " 1.2 MB/s  " LV_SYMBOL_UP " 40 KB/s"`; colour `color_text()` when active else `color_dim()`; `--` dim when `!net.ok`.
- [ ] Verify with dump backend screenshot (fixture-free run on this machine): row visible, fits 320 px, rotated 180 OK. Commit `feat: network speed row on overview`.
