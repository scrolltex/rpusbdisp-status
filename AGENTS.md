# AGENTS.md

Guidance for coding agents working on **rpusbdisp-status** — an LVGL status
screen for a home server (Ubuntu Server 26.04) on a RoboPeak Mini USB Display
(320×240, RGB565, resistive touch). Read `README.md` for the user-facing view.

## Build and test

```bash
git submodule update --init --depth 1   # lvgl v9.6.0, rpusbdisp SDK
cmake -B build                          # -DSTATUS_SDL=ON for a desktop window (needs libsdl2-dev)
cmake --build build -j
ctest --test-dir build                  # doctest unit tests + deploy file checks
```

- Requires `libusb-1.0-0-dev` and `pkg-config`. Without the real package (e.g. a
  sandbox), point `PKG_CONFIG_PATH` at a fake `libusb-1.0.pc` that uses the
  header from `third_party/rpusbdisp/drivers/usermode-sdk/deps/libusbx-1.0.17/libusb/`.
- Unit tests need no hardware, no Docker and no network.
- `ctest` must stay green and the build warning-free (`-Wall -Wextra`).

## Checking the UI without hardware

The `dump` backend runs the same flush path as the real display and writes
`<dir>/frame.ppm` about once a second:

```bash
./build/rpusbdisp-status --backend dump --dump-dir /tmp/d --start-page 1 --rotate 180
```

Convert the PPM to PNG to look at it. For the Docker screen, serve a fake
`/containers/json` on a unix socket and pass `--docker-socket`. Look at a
screenshot after every visual change; tests do not cover layout.

Agents usually have no access to the display. Anything about USB timing,
touch or colours on hardware is unverified until a human with the device
reports back — say so instead of claiming it works.

## Layout

```
src/data      collection: /proc, /sys, Docker over a unix socket (no libcurl)
src/backend   PartialBackend (LVGL display, rotation, colour conversion,
              coalescing, fps limit) → RpUsbDispBackend (USB), DumpBackend, SdlBackend
src/ui        App (tileview pager + dots), screens, format/theme helpers
tools/hwcheck hardware check without LVGL
deploy        systemd unit, udev rule, install.sh
docs/design   design records, one per feature (why it is built this way)
docs/known-issues.md  open findings from reviews
third_party   lvgl, rpusbdisp (submodules); nlohmann/json, doctest (vendored)
```

- `status_core` (data + format + options) has no LVGL dependency; keep it that way.
- Collector: two threads — system metrics every 1 s, Docker every 5 s — so a
  hung Docker never stalls CPU/RAM. UI reads a copied `Snapshot` every 500 ms.
- New screen: subclass `status::Screen`, keep the bottom 18 px free for the
  page dots, register it in `src/main.cpp`. New data goes into
  `src/data/snapshot.h` and is filled in `collector.cpp`.
- Pages: Disks · Overview (home) · Docker. Page numbers are relative to home
  (`--start-page -1/0/1`); auto-return to home after 60 s without touch.
- Disks: `DiskMonitor` (in the 1 s system loop). Never call `statvfs` on an NFS
  mount from the collector thread — use `NfsProbe` (detached worker, 2 s
  deadline); a dead hard-mounted NFS blocks forever. SMART comes from JSON
  files written by the root `rpusbdisp-smart.timer`; the program itself must
  never need root or `CAP_SYS_ADMIN`.
- Never wake sleeping HDDs: no hwmon (`drivetemp`) reads for rotational disks,
  `smartctl -n standby` in the timer. `statvfs` and `/proc/diskstats` are safe.

## Display quirks (learned on hardware — do not "simplify" away)

- **BGR565.** The display wants red in the low 5 bits. `RpUsbDispBackend`
  converts by default; `--color-order rgb` disables it. The dump file stays RGB.
- **Coalescing + 16 fps.** Many small back-to-back bitblts make the firmware
  flash white vertical stripes. `PartialBackend` collects a refresh in a shadow
  framebuffer and sends one bounding-box blit, at most 16 per second (the kernel
  driver's default). Rare stripes during fast scrolling remain and are accepted.
- **Dirty flag.** `RPUSBDISP_DISPLAY_STATUS_DIRTY_FLAG` in a status packet means
  "send the full screen"; the backend invalidates everything.
- **Touch** coordinates arrive in pixels; LVGL applies the 180° rotation itself.
- **SDK fork** (`scrolltex/rpusbdisp`, submodule): fixes for GCC 15, a
  non-throwing destructor on unplug and USB transfer timeouts live there. SDK
  changes go to the fork first, then bump the submodule here.
- RLE compression in the SDK was fuzz-tested (200k images) and is correct.

## LVGL 9.6 notes

- Public headers moved to `include/lvgl/...`; `src/...` headers are deprecated.
- Flag helpers `lv_obj_add_flag/remove_flag` are deprecated — use
  `lv_obj_set_clickable`, `lv_obj_set_scrollable`, `lv_obj_set_event_bubble`, …
- No `LV_COLOR_DEPTH`: colour format is set per display.
- `lv_conf.h` uses the C library allocator (`LV_STDLIB_CLIB`); the builtin 64 KB
  heap crashed with ~30 Docker containers.
- Built-in Montserrat has no `—`, `↑`, `↓`; use `--` and `LV_SYMBOL_UP/DOWN`.
- Time-based logic in backends uses `lv_tick_get()` so tests can drive it with
  `lv_tick_inc()`.

## Docker

Read-only: only `GET /containers/json?all=1` through the socket, unversioned
path (Docker Engine 29+ rejects API versions below 1.44). Never add calls that
change containers.

## Working style

- TDD: write the failing test, watch it fail, then implement. When a test can
  only be written after the code, prove it by breaking the code once (mutation).
- Larger features: write a design record `docs/design/YYYY-MM-DD-<topic>.md`
  (why, goals/non-goals, approaches considered and the chosen one, decisions)
  and agree it with the human you work for before coding. Commit it.
- Implementation plans and task checklists are working files: keep them under
  the git-ignored `.superpowers/` (or delete them when done), never commit them.
  They go stale as soon as the code changes; the design record and git history
  are the durable record.
- Findings you decide not to fix go to `docs/known-issues.md`; remove an entry
  when it is fixed.
- Hardware-related bugs: find the root cause before fixing (compare with the
  kernel driver in `third_party/rpusbdisp/drivers/linux-driver`).

## Commits

- Commit with the git identity already configured in the repository; never
  override `user.name` / `user.email`.
- AI-assisted commits keep a `Co-Authored-By:` trailer naming the agent; no
  other agent trailers (session links etc.).
- Conventional prefixes: `feat:`, `fix:`, `docs:`, `build:`, `tools:`.
- Do not push or force-push unless the human explicitly asks.
