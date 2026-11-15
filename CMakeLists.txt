cmake_minimum_required(VERSION 3.20)
project(rpusbdisp_status VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(STATUS_SDL "Build the SDL2 backend (desktop debugging)" OFF)
option(STATUS_TESTS "Build unit tests" ON)

if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/CMakeLists.txt)
    message(FATAL_ERROR "lvgl submodule is missing: run `git submodule update --init`")
endif()

# ---- rpusbdisp SDK -------------------------------------------------------
include(cmake/rpusbdisp_sdk.cmake)

# ---- LVGL ----------------------------------------------------------------
set(LV_BUILD_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)
if(STATUS_SDL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    target_compile_definitions(lvgl PUBLIC STATUS_SDL=1)
    target_link_libraries(lvgl PUBLIC PkgConfig::SDL2)
endif()

set(WARN -Wall -Wextra)

# ---- core (no LVGL, no hardware): data collection + formatting -----------
file(GLOB STATUS_CORE_SOURCES CONFIGURE_DEPENDS src/data/*.cpp src/ui/format.cpp src/options.cpp)
add_library(status_core STATIC ${STATUS_CORE_SOURCES} src/version.cpp)
target_include_directories(status_core PUBLIC src)
target_include_directories(status_core SYSTEM PUBLIC third_party)
target_compile_options(status_core PRIVATE ${WARN})
target_link_libraries(status_core PUBLIC Threads::Threads)

# ---- app (LVGL + backends + screens) -------------------------------------
file(GLOB STATUS_APP_SOURCES CONFIGURE_DEPENDS src/backend/*.cpp src/ui/app.cpp src/ui/screen_*.cpp)
add_library(status_app STATIC ${STATUS_APP_SOURCES})
target_compile_options(status_app PRIVATE ${WARN})
target_link_libraries(status_app PUBLIC status_core lvgl rpusbdisp_sdk)
if(STATUS_SDL)
    target_compile_definitions(status_app PUBLIC STATUS_SDL=1)
endif()

# ---- tests ---------------------------------------------------------------
if(STATUS_TESTS)
    enable_testing()
    file(GLOB STATUS_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(status_tests ${STATUS_TEST_SOURCES})
    target_compile_options(status_tests PRIVATE ${WARN})
    target_compile_definitions(status_tests PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_link_libraries(status_tests PRIVATE status_app)
    add_test(NAME status_tests COMMAND status_tests)
endif()

# ---- hardware check tool -------------------------------------------------
add_executable(hwcheck tools/hwcheck.cpp)
target_compile_options(hwcheck PRIVATE ${WARN})
target_link_libraries(hwcheck PRIVATE rpusbdisp_sdk)

# ---- main program --------------------------------------------------------
add_executable(rpusbdisp-status src/main.cpp)
target_compile_options(rpusbdisp-status PRIVATE ${WARN})
target_link_libraries(rpusbdisp-status PRIVATE status_app)

# ---- install -------------------------------------------------------------
install(TARGETS rpusbdisp-status hwcheck RUNTIME DESTINATION bin)
install(FILES deploy/status-display.service DESTINATION /etc/systemd/system)
install(FILES deploy/99-rpusbdisp.rules DESTINATION /etc/udev/rules.d)
