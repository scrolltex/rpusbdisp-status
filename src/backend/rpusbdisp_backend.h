#pragma once

#include "backend/partial_backend.h"

#include <chrono>
#include <functional>
#include <memory>

namespace status {

// What the backend needs from a physical display. Lets tests replace the USB device.
class DisplayDevice {
public:
    virtual ~DisplayDevice() = default;
    virtual bool alive() = 0;
    virtual bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) = 0;
};

class RpUsbDispBackend : public PartialBackend {
public:
    using TouchCallback = std::function<void(int32_t x, int32_t y, bool pressed)>;
    // Returns nullptr when no display can be opened right now.
    using Opener = std::function<std::unique_ptr<DisplayDevice>(TouchCallback)>;

    static Opener usb_opener();

    explicit RpUsbDispBackend(Opener opener = usb_opener(),
                              std::chrono::milliseconds retry = std::chrono::seconds(2));
    ~RpUsbDispBackend() override;
    bool init(lv_display_rotation_t rotation) override;
    void poll() override;

protected:
    bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) override;

private:
    void try_open();
    void drop(const char* why);

    Opener opener_;
    std::chrono::milliseconds retry_;
    std::unique_ptr<DisplayDevice> dev_;
    bool blit_failed_ = false;
    std::chrono::steady_clock::time_point next_try_{};
    bool announced_waiting_ = false;
};

}  // namespace status
