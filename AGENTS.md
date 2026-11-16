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
./build/rpusbdisp-status --backend dump --dump-dir /tmp/d --start-tile 1 --rotate 180
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
docs/superpowers  design specs and implementation plans, one pair per feature
third_party   lvgl, rpusbdisp (submodules); nlohmann/json, doctest (vendored)
```

- `status_core` (data + format + options) has no LVGL dependency; keep it that way.
- Collector: two threads — system metrics every 1 s, Docker every 5 s — so a
  hung Docker never stalls CPU/RAM. UI reads a copied `Snapshot` every 500 ms.
- New screen: subclass `status::Screen`, keep the bottom 18 px free for the
  page dots, register it in `src/main.cpp`. New data goes into
  `src/data/snapshot.h` and is filled in `collector.cpp`.

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
- Larger features: design spec in `docs/superpowers/specs/`, plan in
  `docs/superpowers/plans/`, agreed with the human you work for before coding.
- Hardware-related bugs: find the root cause before fixing (compare with the
  kernel driver in `third_party/rpusbdisp/drivers/linux-driver`).

## Commits

- Commit with the git identity already configured in the repository; never
  override `user.name` / `user.email`.
- AI-assisted commits keep a `Co-Authored-By:` trailer naming the agent; no
  other agent trailers (session links etc.).
- Conventional prefixes: `feat:`, `fix:`, `docs:`, `build:`, `tools:`.
- Do not push or force-push unless the human explicitly asks.
