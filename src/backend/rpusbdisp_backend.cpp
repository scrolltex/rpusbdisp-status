#include "backend/rpusbdisp_backend.h"

#include <rp/deps/libusbx_wrap/libusbx_wrap.h>
#include <rp/drivers/display/rpusbdisp/rpusbdisp.h>

#include <cstdio>

using rp::drivers::display::RoboPeakUsbDisplayDevice;

namespace status {

namespace {

class UsbDisplay : public DisplayDevice {
public:
    explicit UsbDisplay(std::shared_ptr<RoboPeakUsbDisplayDevice> d) : dev_(std::move(d)) {}
    bool alive() override {
        try {
            return dev_->isAlive();
        } catch (rp::util::Exception&) {
            return false;
        }
    }
    bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) override {
        try {
            dev_->bitblt(uint16_t(x), uint16_t(y), uint16_t(w), uint16_t(h), RoboPeakUsbDisplayBitOperationCopy,
                         const_cast<uint16_t*>(px));
            return true;
        } catch (rp::util::Exception&) {
            return false;
        }
    }

private:
    std::shared_ptr<RoboPeakUsbDisplayDevice> dev_;
};

}  // namespace

RpUsbDispBackend::Opener RpUsbDispBackend::usb_opener() {
    return [](TouchCallback touch) -> std::unique_ptr<DisplayDevice> {
        try {
            auto dev = RoboPeakUsbDisplayDevice::openFirstDevice();
            if (!dev) return nullptr;
            dev->setStatusUpdatedCallback([touch](const rpusbdisp_status_normal_packet_t& s) {
                touch(s.touch_x, s.touch_y, s.touch_status == RPUSBDISP_TOUCH_STATUS_PRESSED);
            });
            dev->enable();
            return std::make_unique<UsbDisplay>(dev);
        } catch (rp::util::Exception& e) {
            std::fprintf(stderr, "rpusbdisp: open failed (error 0x%08x)\n", unsigned(e.errorCode()));
            return nullptr;
        }
    };
}

RpUsbDispBackend::RpUsbDispBackend(Opener opener, std::chrono::milliseconds retry)
    : opener_(std::move(opener)), retry_(retry) {}

RpUsbDispBackend::~RpUsbDispBackend() { dev_.reset(); }

bool RpUsbDispBackend::init(lv_display_rotation_t rotation) {
    if (!PartialBackend::init(rotation)) return false;
    try_open();
    return true;
}

void RpUsbDispBackend::try_open() {
    next_try_ = std::chrono::steady_clock::now() + retry_;
    dev_ = opener_([this](int32_t x, int32_t y, bool pressed) { set_touch(x, y, pressed); });
    if (!dev_) {
        if (!announced_waiting_) std::fprintf(stderr, "rpusbdisp: waiting for display...\n");
        announced_waiting_ = true;
        return;
    }
    announced_waiting_ = false;
    blit_failed_ = false;
    std::fprintf(stderr, "rpusbdisp: display connected\n");
    invalidate_all();
}

void RpUsbDispBackend::drop(const char* why) {
    std::fprintf(stderr, "rpusbdisp: %s\n", why);
    dev_.reset();
    set_touch(0, 0, false);
    next_try_ = std::chrono::steady_clock::now() + retry_;
}

void RpUsbDispBackend::poll() {
    if (dev_) {
        if (blit_failed_) drop("display not responding, reconnecting");
        else if (!dev_->alive()) drop("display disconnected");
        else return;
    }
    if (std::chrono::steady_clock::now() >= next_try_) try_open();
}

bool RpUsbDispBackend::blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) {
    if (!dev_ || blit_failed_) return false;
    if (!dev_->blit(x, y, w, h, px)) blit_failed_ = true;
    return !blit_failed_;
}

}  // namespace status
