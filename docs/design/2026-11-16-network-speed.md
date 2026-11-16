# Network speed on the Overview screen — design

Date: 2026-11-16 · Status: approved by the user · Size: small change

## Why
The user wants to see at a glance whether the server has network traffic
right now: an update or backup downloading, or many requests coming to the
server. The exact number matters less than noticing activity.

## Goals
- One row under TEMP on Overview: `NET  ↓ 1.2 MB/s  ↑ 40 KB/s`.
- Bytes per second with automatic units (`B/s`, `KB/s`, `MB/s`, `GB/s`).
- Activity highlight: dim (grey) while both directions are below ~10 KB/s,
  bright text otherwise. No warning colours: high speed is not a problem.

## Non-goals
- Separate network screen, history graph, IP addresses, totals (possible later).
- Megabits / comparison with the ISP plan.
- Per-container or per-process traffic.

## Approaches
- **Chosen: default-route interface.** Read the interface that carries the
  default route from `/proc/net/route`, its byte counters from `/proc/net/dev`,
  rate = delta over the 1 s collector period. `--net-iface NAME` overrides.
- Rejected: sum all interfaces — Docker's `docker0`, `veth*`, `br-*` count the
  same bytes two or three times.
- Rejected: separate Network screen — the user wants it on the first screen.

## Decisions (author's calls)
- Interface resolved on every sample (cheap), so a changed default route or a
  late-appearing interface is picked up without restart. No default route and
  no `--net-iface`: the row shows `--`.
- Counter decrease (interface reset/recreated) → rate 0 for that sample.
- First sample after start → no rate yet (`--`), like CPU starts at 0.
- Activity threshold: 10 KB/s (10 * 1024 B/s) in either direction.
- Arrows: LVGL symbols `LV_SYMBOL_DOWN` / `LV_SYMBOL_UP` (present in the built-in
  Montserrat fonts).
- Unit steps of 1024; one decimal below 100 (`1.2 MB/s`, `512 KB/s`).

## Left to the builder
Exact layout of the row within the existing Overview column.
