# Server Status Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Программа `rpusbdisp-status`: LVGL-интерфейс мониторинга домашнего сервера (экраны «Overview» и «Docker») на RoboPeak USB Display через usermode SDK.

**Architecture:** Один процесс. UI-поток крутит `lv_timer_handler`; фоновый поток `Collector` собирает системные метрики (1 с) и состояние Docker (5 с) в `Snapshot` под mutex. Вывод через сменный `Backend` (rpusbdisp / sdl / dump). Экраны — классы с интерфейсом `Screen`, лежат на плитках `lv_tileview`.

**Tech Stack:** C++17, CMake ≥ 3.20, LVGL v9.6.0 (submodule), scrolltex/rpusbdisp @ `98f96f2` (submodule), libusb-1.0 (pkg-config), nlohmann/json (vendored single header), doctest (vendored), SDL2 (опционально).

**Spec:** `docs/superpowers/specs/2026-11-15-server-status-display-design.md`

## Global Constraints

- Экран 320×240, RGB565, ландшафт; поворот только 0 или 180 (`--rotate 0|180`).
- Docker — только GET `/v1.43/containers/json?all=1` через unix-сокет, таймаут 2 с; никаких изменяющих запросов.
- Опрос: системные метрики 1 с, Docker 5 с, обновление UI 500 мс.
- Пороги: CPU/RAM зелёный < 70, жёлтый 70–90, красный > 90 (%); температура < 65 зелёный, 65–80 жёлтый, > 80 красный (°C).
- Docker-цвета: running зелёный; restarting/paused жёлтый; exited с кодом 0 — серый; прочие exited/dead — красный.
- Сортировка контейнеров: running сверху, затем по имени.
- Тексты UI на английском, шрифты Montserrat 14/20/28.
- Без root в работе; сервис от пользователя `statusdisp` (группы `plugdev`, `docker`).
- Аргументы: `--backend rpusbdisp|sdl|dump`, `--rotate 0|180`, `--temp-path PATH`, `--docker-socket PATH` (по умолчанию `/var/run/docker.sock`), `--dump-dir DIR` (для dump).
- Отклонение от спеки (решение исполнителя): добавлен бэкенд `dump` — рендер в PPM-файлы для проверки UI без железа и без SDL.

## Review Focus

1. Docker не запущен / нет прав на сокет → экран Docker показывает `Docker unavailable: <reason>`, Overview обновляется дальше (тест: `docker_client` с несуществующим сокетом возвращает ошибку за < 2.5 с).
2. Docker-демон принял соединение, но молчит → не дольше таймаута, UI не замирает (тест: сервер-заглушка, который не отвечает).
3. Ответ Docker с `Transfer-Encoding: chunked` (так отвечает реальный dockerd) → парсится корректно (тест с chunked-фикстурой).
4. Дисплей отключён на старте или во время работы → процесс не падает, переподключается раз в 2 с (ручная проверка `hwcheck` и основного бинарника; юнит-тест логики не требуется).
5. Нет ни одного датчика температуры → «—» вместо падения (тест `find_temp_source` на пустом sysfs-корне).

---

## File Structure

```
CMakeLists.txt                  верхний уровень, опции STATUS_SDL, STATUS_TESTS
cmake/rpusbdisp_sdk.cmake       статическая lib rpusbdisp_sdk из submodule
lv_conf.h                       конфиг LVGL
third_party/lvgl                submodule (v9.6.0)
third_party/rpusbdisp           submodule (98f96f2)
third_party/nlohmann/json.hpp   vendored
third_party/doctest/doctest.h   vendored
src/main.cpp                    аргументы, цикл
src/options.{h,cpp}             разбор аргументов
src/data/snapshot.h             структуры данных
src/data/system_stats.{h,cpp}   парсеры /proc и sysfs
src/data/docker_client.{h,cpp}  unix-socket HTTP + JSON
src/data/collector.{h,cpp}      фоновый поток
src/backend/backend.h           интерфейс
src/backend/rotate.{h,cpp}      пересчёт области для 180°
src/backend/rpusbdisp_backend.{h,cpp}
src/backend/dump_backend.{h,cpp}
src/backend/sdl_backend.{h,cpp}
src/ui/format.{h,cpp}           форматирование и цвета по порогам
src/ui/screen.h                 интерфейс экрана
src/ui/app.{h,cpp}              tileview + точки
src/ui/screen_overview.{h,cpp}
src/ui/screen_docker.{h,cpp}
tools/hwcheck.cpp               проверка железа без LVGL
tests/*.cpp, tests/fixtures/*
deploy/status-display.service, deploy/99-rpusbdisp.rules, deploy/install.sh
README.md
```

---

### Task 1: Каркас проекта и сборка SDK

**Files:** Create `CMakeLists.txt`, `cmake/rpusbdisp_sdk.cmake`, `lv_conf.h`, `.gitmodules`, `.gitignore`, `third_party/nlohmann/json.hpp`, `third_party/doctest/doctest.h`, `tests/test_main.cpp`, `tests/test_smoke.cpp`

**Interfaces:** Produces CMake targets `rpusbdisp_sdk` (STATIC, PUBLIC includes `infra/include`, `deps-wraps/libusbx_wrap/include`, `rpusbdisp-drv/include`, links `PkgConfig::LIBUSB Threads::Threads`), `lvgl` (из submodule), `status_core` (STATIC, пока пустой — `src/data`, `src/ui/format`), `status_tests` (doctest, `ctest`).

- [ ] Step 1: `git submodule add https://github.com/lvgl/lvgl third_party/lvgl` → checkout `v9.6.0`; `git submodule add https://github.com/scrolltex/rpusbdisp third_party/rpusbdisp` → checkout `98f96f2`.
- [ ] Step 2: `rpusbdisp_sdk.cmake` — `file(GLOB …)` по `drivers/usermode-sdk/{infra/src,deps-wraps/libusbx_wrap/src,rpusbdisp-drv/src}/**/*.cc`, `-Wno-deprecated`.
- [ ] Step 3: `lv_conf.h` из шаблона: `LV_COLOR_DEPTH 16`, `LV_USE_OS LV_OS_NONE`, `LV_FONT_MONTSERRAT_14/20/28 1`, `LV_USE_SDL` = макрос `STATUS_SDL`, `LV_USE_LOG 1` (WARN).
- [ ] Step 4: `test_smoke.cpp`: `TEST_CASE("sdk links") { CHECK(rp::drivers::display::RoboPeakUsbDisplayDevice::ScreenWidth == 320); }`
- [ ] Step 5: Run `cmake -B build -DSTATUS_TESTS=ON && cmake --build build -j && ctest --test-dir build` → PASS.
- [ ] Step 6: Commit `build: project skeleton with LVGL and rpusbdisp SDK submodules`.

### Task 2: hwcheck — проверка железа

**Files:** Create `tools/hwcheck.cpp`; Modify `CMakeLists.txt` (target `hwcheck` → `rpusbdisp_sdk`).

- [ ] Step 1: Реализовать: открыть устройство (`openFirstDevice`, ретрай каждые 2 с с сообщением `waiting for display...`), `enable()`, нарисовать 4 вертикальные полосы через `fillrect`: красная `0xF800`, зелёная `0x07E0`, синяя `0x001F`, белая `0xFFFF`, затем `bitblt` буфера 320×240 с горизонтальным градиентом красного по полному кадру в нижней половине; печатать `touch x=%d y=%d pressed=%d` из колбэка; при `!isAlive()` → `display disconnected`, вернуться к ожиданию. Выход по SIGINT.
- [ ] Step 2: Run `cmake --build build --target hwcheck` → собирается без предупреждений.
- [ ] Step 3: Commit `tools: add hwcheck for display/touch verification`.
- [ ] Step 4 (пользователь): запустить на сервере, сообщить: цвета полос в правильном порядке? координаты углов? переживает переподключение? Результат фиксирует порядок байтов для Task 6.

### Task 3: Системные метрики

**Files:** Create `src/data/snapshot.h`, `src/data/system_stats.{h,cpp}`, `tests/test_system_stats.cpp`, `tests/fixtures/proc_stat_{a,b}.txt`, `tests/fixtures/meminfo.txt`

**Interfaces:** Produces:
```cpp
struct CpuTimes { uint64_t idle = 0, total = 0; };
std::optional<CpuTimes> parse_proc_stat(const std::string& text);        // строка "cpu "
double cpu_percent(const CpuTimes& prev, const CpuTimes& cur);           // 0..100, 0 если total не вырос
struct MemInfo { uint64_t total_kb = 0, available_kb = 0; };
std::optional<MemInfo> parse_meminfo(const std::string& text);
std::optional<double> parse_uptime(const std::string& text);             // секунды
std::optional<std::string> find_temp_source(const std::string& sysfs_root); // путь к *_input
std::optional<double> read_temp_c(const std::string& path);              // milli°C → °C
struct SystemSnapshot { std::string hostname; double cpu = 0; MemInfo mem; double uptime_s = 0; std::optional<double> temp_c; };
```
`snapshot.h` также: `struct ContainerInfo { std::string name, state, status; }; struct DockerSnapshot { bool ok = false; std::string error; std::vector<ContainerInfo> containers; }; struct Snapshot { SystemSnapshot sys; DockerSnapshot docker; };`

- [ ] Step 1: Тесты: `cpu_percent` для фикстур a→b = ожидаемое значение (рассчитать по фикстурам, допуск 0.01); total не изменился → 0; `parse_meminfo` → total 16303988, available 9876543 (значения из фикстуры); `parse_uptime("12345.67 999.0\n")` → 12345.67; `find_temp_source` на временном каталоге: hwmon `coretemp` с `temp1_input` → выбран; только `thermal/thermal_zone0/type`=`acpitz` → `thermal_zone0/temp`; пустой каталог → `nullopt`; `read_temp_c` файла `54000\n` → 54.0.
- [ ] Step 2: Run `ctest` → FAIL (не определены).
- [ ] Step 3: Реализовать. Приоритет `find_temp_source`: `class/hwmon/*/name` ∈ {coretemp,k10temp,zenpower} → `temp1_input`; иначе `class/thermal/thermal_zone*/type` == `x86_pkg_temp` → `temp`; иначе `thermal_zone0/temp`, если есть.
- [ ] Step 4: Run `ctest` → PASS. Commit `feat: system stats parsers`.

### Task 4: Docker-клиент

**Files:** Create `src/data/docker_client.{h,cpp}`, `tests/test_docker.cpp`, `tests/fixtures/containers.json`, `tests/fixtures/http_chunked.txt`

**Interfaces:** Produces:
```cpp
struct HttpResult { bool ok = false; int status = 0; std::string body, error; };
HttpResult parse_http_response(const std::string& raw);   // Content-Length и chunked
HttpResult unix_http_get(const std::string& socket_path, const std::string& target, int timeout_ms);
std::vector<ContainerInfo> parse_containers(const std::string& json);   // Names[0] без '/', State, Status; отсортировано
DockerSnapshot fetch_docker(const std::string& socket_path);            // timeout 2000
```

- [ ] Step 1: Тесты: chunked-фикстура → `body` равен склеенным чанкам; `Content-Length` ответ → body; status 500 → `ok=false`, `error` содержит `500`; `parse_containers` фикстуры (4 контейнера, один exited, имена с `/`) → running первыми, по алфавиту, без `/`; невалидный JSON → пустой вектор; `fetch_docker("/nonexistent.sock")` → `ok=false`, `error` непустой; сервер-заглушка на unix-сокете (поток в тесте), принимает и молчит → `unix_http_get(..., 300)` возвращается за < 1 с с `ok=false`, `error` содержит `timeout`; заглушка отдаёт `http_chunked.txt` → `ok=true`.
- [ ] Step 2: Run → FAIL.
- [ ] Step 3: Реализовать: `socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC)`, неблокирующий `connect` + `poll`, запрос `GET <target> HTTP/1.1\r\nHost: docker\r\nConnection: close\r\n\r\n`, чтение до EOF с `poll` и общим дедлайном.
- [ ] Step 4: Run → PASS. Commit `feat: docker client over unix socket`.

### Task 5: Collector

**Files:** Create `src/data/collector.{h,cpp}`, `tests/test_collector.cpp`

**Interfaces:**
```cpp
struct CollectorConfig { std::string proc_root = "/proc", sysfs_root = "/sys", temp_path, docker_socket = "/var/run/docker.sock";
                         std::chrono::milliseconds sys_period{1000}, docker_period{5000}; };
class Collector { public: explicit Collector(CollectorConfig); ~Collector(); void start(); void stop(); Snapshot snapshot() const; };
```
- [ ] Step 1: Тест: `proc_root` → каталог с фикстурами, `docker_socket` несуществующий, периоды 50 мс; `start()`, ждать 200 мс; `snapshot().sys.mem.total_kb` == значение фикстуры, `docker.ok == false`; `stop()` возвращается < 500 мс (поток прерывается через condition_variable).
- [ ] Step 2: FAIL → реализовать → PASS. Commit `feat: background collector`.

### Task 6: Бэкенды вывода

**Files:** Create `src/backend/{backend.h,rotate.h,rotate.cpp,rpusbdisp_backend.*,dump_backend.*,sdl_backend.*}`, `tests/test_rotate.cpp`

**Interfaces:**
```cpp
struct Area { int32_t x1, y1, x2, y2; };                // включительно
Area rotate180_area(Area a, int32_t w, int32_t h);      // {w-1-x2, h-1-y2, w-1-x1, h-1-y1}
class Backend { public: virtual ~Backend() = default;
  virtual bool init(lv_display_rotation_t rot) = 0;      // создаёт lv_display + lv_indev
  virtual void poll() {} };                              // вызывается в главном цикле (переподключение, SDL-события)
std::unique_ptr<Backend> make_backend(const std::string& name, const std::string& dump_dir);
```
- [ ] Step 1: Тест `rotate180_area({0,0,319,39},320,240)` == `{0,200,319,239}`; `{10,20,30,40}` → `{289,199,309,219}`.
- [ ] Step 2: FAIL → реализовать `rotate.cpp` → PASS.
- [ ] Step 3: `RpUsbDispBackend`: display 320×240 `LV_COLOR_FORMAT_RGB565`, PARTIAL, 2 буфера 320×40; `flush_cb`: при 180° — `lv_draw_rotate(px, tmp, w, h, stride, stride, LV_DISPLAY_ROTATION_180, LV_COLOR_FORMAT_RGB565)` и `rotate180_area`; если Task 2 показал перепутанный порядок байтов — `lv_draw_rgb565_swap`; `bitblt`; если устройства нет — просто `lv_display_flush_ready`. Тач: колбэк SDK пишет `std::atomic`-упакованные `x,y,pressed` (clamp 0..319/0..239); `read_cb` отдаёт их. `poll()`: раз в 2 с при отсутствии/`!isAlive()` — `openFirstDevice()`, `enable()`, `lv_obj_invalidate(lv_screen_active())`. Поворот 180° для тача LVGL делает сам (проверить на dump/SDL: тап в логический (10,10) при 180° приходит из физического (309,229)).
- [ ] Step 4: `DumpBackend`: DIRECT-рендер в полный буфер 320×240; `poll()` раз в 1 с пишет `dump_dir/frame.ppm` (P6, RGB888), touch отсутствует. `SdlBackend` (только при `STATUS_SDL`): `lv_sdl_window_create(320,240)` + `lv_sdl_mouse_create()`.
- [ ] Step 5: Commit `feat: display backends with 180° rotation`.

### Task 7: UI

**Files:** Create `src/ui/{format.*,screen.h,app.*,screen_overview.*,screen_docker.*}`, `src/options.*`, `src/main.cpp`, `tests/test_format.cpp`, `tests/test_options.cpp`

**Interfaces:**
```cpp
enum class Level { Ok, Warn, Crit, Neutral };
Level level_percent(double pct);            // <70 Ok, <=90 Warn, >90 Crit
Level level_temp(double c);                 // <65 Ok, <=80 Warn, >80 Crit
Level level_container(const ContainerInfo&);// running Ok; restarting/paused Warn; exited с "(0)" Neutral; иначе Crit
std::string format_uptime(double s);        // "12d 4h", "3h 5m", "42m", "<1m"
std::string format_bytes_kb(uint64_t kb);   // "6.5G", "512M"
class Screen { public: virtual ~Screen() = default; virtual const char* name() const = 0;
  virtual void create(lv_obj_t* parent) = 0; virtual void update(const Snapshot&) = 0; };
class App { public: void add(std::unique_ptr<Screen>); void create(); void update(const Snapshot&); };
struct Options { std::string backend = "rpusbdisp"; int rotate = 0; std::string temp_path, docker_socket = "/var/run/docker.sock", dump_dir = "."; };
std::optional<Options> parse_options(int argc, char** argv, std::string& error);
```
- [ ] Step 1: Тесты: `level_percent(69.9)`=Ok, `(70)`=Warn, `(90)`=Warn, `(90.1)`=Crit; `level_temp(64.9)`=Ok, `(80)`=Warn, `(80.5)`=Crit; `level_container({"x","exited","Exited (0) 2 hours ago"})`=Neutral, `Exited (137)`→Crit, `restarting`→Warn; `format_uptime(1058400)`="12d 4h", `(11100)`="3h 5m", `(30)`="<1m"; `format_bytes_kb(6815744)`="6.5G"; `parse_options` с `--rotate 90` → `nullopt`, error содержит `rotate`; `--backend dump --dump-dir /tmp` → поля заданы.
- [ ] Step 2: FAIL → реализовать format/options → PASS.
- [ ] Step 3: `App`: `lv_tileview` на весь экран, по плитке на экран в ряд (`lv_tileview_add_tile(tv, i, 0, LV_DIR_LEFT|LV_DIR_RIGHT)`), снизу ряд точек 8 px (активная — белая), тап по точке → `lv_tileview_set_tile_by_index(tv, i, 0, LV_ANIM_ON)`; событие `LV_EVENT_VALUE_CHANGED` обновляет точки. Тёмная тема (`lv_theme_default_init(..., true, ...)`).
- [ ] Step 4: `OverviewScreen` по наброску спеки: заголовок hostname (20) слева / uptime справа; бары CPU и RAM (`lv_bar`, высота 18) с подписями `CPU 58%`, `RAM 41%  6.5/16G`; температура шрифтом 28 (`54°C` или `—`); цвет индикаторов по `Level`.
- [ ] Step 5: `DockerScreen`: строка сводки `Docker  N running  M other`; `lv_list` со строками «точка 10 px цвета Level + имя (слева) + status (справа, сокращённый до `Up 3d`/`Exited (0)`)»; при `!ok` — одна строка `Docker unavailable: <error>`. Список перестраивать только при изменении набора/статусов.
- [ ] Step 6: `main.cpp`: options → backend → `App` с двумя экранами → `Collector.start()` → цикл `lv_timer_handler(); backend->poll(); usleep(5000)`; `lv_tick` от `clock_gettime(CLOCK_MONOTONIC)` через `lv_tick_set_cb`; UI-таймер 500 мс → `app.update(collector.snapshot())`; SIGINT/SIGTERM → корректный выход.
- [ ] Step 7: Проверка: `./build/rpusbdisp-status --backend dump --dump-dir /tmp/d` 3 с, конвертировать `frame.ppm` в PNG и посмотреть: Overview читаем, цвета по порогам; прокрутить на Docker (временный флаг `--start-tile 1`) — `Docker unavailable` или список. То же с `--rotate 180` — изображение перевёрнуто.
- [ ] Step 8: Commit `feat: LVGL UI with overview and docker screens`.

### Task 8: Деплой и README

**Files:** Create `deploy/status-display.service`, `deploy/99-rpusbdisp.rules`, `deploy/install.sh`, `README.md`; Modify `CMakeLists.txt` (`install(TARGETS rpusbdisp-status hwcheck DESTINATION bin)`, unit → `lib/systemd/system`, rules → `/etc/udev/rules.d`).

- [ ] Step 1: udev: `SUBSYSTEM=="usb", ATTR{idVendor}=="fccf", ATTR{idProduct}=="a001", MODE="0660", GROUP="plugdev"`.
- [ ] Step 2: unit: `User=statusdisp`, `SupplementaryGroups=plugdev docker`, `ExecStart=/usr/local/bin/rpusbdisp-status --backend rpusbdisp --rotate 0`, `Restart=always`, `RestartSec=3`, `NoNewPrivileges=yes`, `ProtectSystem=strict`, `ProtectHome=yes`.
- [ ] Step 3: `install.sh` (идемпотентный, `set -euo pipefail`): `useradd --system --no-create-home statusdisp` если нет; `usermod -aG plugdev,docker`; `cmake --install build`; `udevadm control --reload && udevadm trigger`; `systemctl daemon-reload && systemctl enable --now status-display`.
- [ ] Step 4: README: зависимости apt (`build-essential cmake pkg-config libusb-1.0-0-dev`, опц. `libsdl2-dev`), клонирование с `--recursive`, сборка, `hwcheck`, установка, поворот, предупреждение про группу docker, как добавить экран.
- [ ] Step 5: Run `bash -n deploy/install.sh` и `cmake --install build --prefix /tmp/inst` → файлы на месте. Commit `docs: deployment files and README`.
