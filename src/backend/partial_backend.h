#pragma once

#include "backend/backend.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace status {

// Common LVGL plumbing for "push RGB565 rectangles to a 320x240 device"
// backends: partial rendering, 180° rotation, optional byte swap, and a
// pointer indev fed from physical touch coordinates.
class PartialBackend : public Backend {
public:
    static constexpr int32_t kWidth = 320;
    static constexpr int32_t kHeight = 240;
    static constexpr int32_t kBufLines = 40;

    bool init(lv_display_rotation_t rotation) override;
    void set_swap_bytes(bool v) { swap_bytes_ = v; }
    // Device expects BGR565 (red in the low bits) instead of RGB565.
    void set_bgr(bool v) { bgr_ = v; }

protected:
    // Push w*h tightly packed RGB565 pixels to physical position (x, y).
    // Returns false if the device is gone.
    virtual bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) = 0;

    // Physical touch state, callable from any thread.
    void set_touch(int32_t x, int32_t y, bool pressed);
    void invalidate_all();

    lv_display_t* disp_ = nullptr;

private:
    static void flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px_map);
    static void read_cb(lv_indev_t* indev, lv_indev_data_t* data);

    bool swap_bytes_ = false;
    bool bgr_ = false;
    lv_indev_t* indev_ = nullptr;
    std::vector<uint16_t> tmp_;
    std::mutex touch_mu_;
    int32_t touch_x_ = 0, touch_y_ = 0;
    bool touch_pressed_ = false;
};

}  // namespace status
