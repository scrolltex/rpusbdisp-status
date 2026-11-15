# Builds the RoboPeak usermode SDK straight from the rpusbdisp submodule,
# without its autoconf/Makefile build.
set(RPUSBDISP_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rpusbdisp/drivers/usermode-sdk)

if(NOT EXISTS ${RPUSBDISP_SDK_DIR}/rpusbdisp-drv/src/rpusbdisp.cc)
    message(FATAL_ERROR "rpusbdisp submodule is missing: run `git submodule update --init`")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(Threads REQUIRED)

file(GLOB_RECURSE RPUSBDISP_SDK_SOURCES CONFIGURE_DEPENDS
    ${RPUSBDISP_SDK_DIR}/infra/src/*.cc
    ${RPUSBDISP_SDK_DIR}/deps-wraps/libusbx_wrap/src/*.cc
    ${RPUSBDISP_SDK_DIR}/rpusbdisp-drv/src/*.cc)

add_library(rpusbdisp_sdk STATIC ${RPUSBDISP_SDK_SOURCES})
target_include_directories(rpusbdisp_sdk SYSTEM PUBLIC
    ${RPUSBDISP_SDK_DIR}/infra/include
    ${RPUSBDISP_SDK_DIR}/deps-wraps/libusbx_wrap/include
    ${RPUSBDISP_SDK_DIR}/rpusbdisp-drv/include)
target_compile_options(rpusbdisp_sdk PRIVATE -Wno-deprecated -w)
set_target_properties(rpusbdisp_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rpusbdisp_sdk PUBLIC PkgConfig::LIBUSB Threads::Threads)
