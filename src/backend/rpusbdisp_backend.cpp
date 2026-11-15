#include "backend/rpusbdisp_backend.h"

#include <rp/deps/libusbx_wrap/libusbx_wrap.h>
#include <rp/drivers/display/rpusbdisp/rpusbdisp.h>

#include <cstdio>

using rp::drivers::display::RoboPeakUsbDisplayDevice;

namespace status {

RpUsbDispBackend::~RpUsbDispBackend() { dev_.reset(); }

bool RpUsbDispBackend::init(lv_display_rotation_t rotation) {
    if (!PartialBackend::init(rotation)) return false;
    try_open();
    return true;
}

void RpUsbDispBackend::try_open() {
    next_try_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    dev_.reset();
    try {
        auto dev = RoboPeakUsbDisplayDevice::openFirstDevice();
        if (!dev) {
            if (!announced_waiting_) std::fprintf(stderr, "rpusbdisp: waiting for display...\n");
            announced_waiting_ = true;
            return;
        }
        dev->setStatusUpdatedCallback([this](const rpusbdisp_status_normal_packet_t& s) {
            set_touch(s.touch_x, s.touch_y, s.touch_status == RPUSBDISP_TOUCH_STATUS_PRESSED);
        });
        dev->enable();
        dev_ = dev;
        announced_waiting_ = false;
        std::fprintf(stderr, "rpusbdisp: display connected\n");
        invalidate_all();
    } catch (rp::util::Exception& e) {
        std::fprintf(stderr, "rpusbdisp: open failed (error 0x%08x)\n", unsigned(e.errorCode()));
        dev_.reset();
    }
}

void RpUsbDispBackend::poll() {
    if (dev_) {
        bool alive = false;
        try {
            alive = dev_->isAlive();
        } catch (rp::util::Exception&) {
        }
        if (alive) return;
        std::fprintf(stderr, "rpusbdisp: display disconnected\n");
        dev_.reset();
        set_touch(0, 0, false);
    }
    if (std::chrono::steady_clock::now() >= next_try_) try_open();
}

bool RpUsbDispBackend::blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) {
    if (!dev_) return false;
    try {
        dev_->bitblt(uint16_t(x), uint16_t(y), uint16_t(w), uint16_t(h), RoboPeakUsbDisplayBitOperationCopy,
                     const_cast<uint16_t*>(px));
        return true;
    } catch (rp::util::Exception&) {
        return false;  // poll() notices the dead device and reconnects
    }
}

}  // namespace status
