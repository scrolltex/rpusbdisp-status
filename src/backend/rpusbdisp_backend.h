#pragma once

#include "backend/partial_backend.h"

#include <chrono>
#include <memory>

namespace rp { namespace drivers { namespace display { class RoboPeakUsbDisplayDevice; } } }

namespace status {

class RpUsbDispBackend : public PartialBackend {
public:
    ~RpUsbDispBackend() override;
    bool init(lv_display_rotation_t rotation) override;
    void poll() override;

protected:
    bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) override;

private:
    void try_open();

    std::shared_ptr<rp::drivers::display::RoboPeakUsbDisplayDevice> dev_;
    std::chrono::steady_clock::time_point next_try_{};
    bool announced_waiting_ = false;
};

}  // namespace status
