#pragma once

#include <lvgl.h>

#include <cstdint>
#include <memory>
#include <string>

namespace status {

// Calls lv_init() once per process.
void lvgl_init_once();

// Swaps the red and blue fields of an RGB565 pixel (RGB565 <-> BGR565).
inline uint16_t rgb565_to_bgr565(uint16_t c) {
    return uint16_t(((c & 0x001F) << 11) | (c & 0x07E0) | (c >> 11));
}

// 180° rotation of a tightly packed RGB565 block (stride == width).
void rotate_rgb565_180(const uint16_t* src, uint16_t* dst, int32_t w, int32_t h);

class Backend {
public:
    virtual ~Backend() = default;
    // Creates the lv_display (320x240) and the pointer lv_indev.
    virtual bool init(lv_display_rotation_t rotation) = 0;
    // Called from the main loop: reconnects hardware, pumps events, dumps frames.
    virtual void poll() {}
};

// name: "rpusbdisp", "dump", "sdl" (only when built with STATUS_SDL).
std::unique_ptr<Backend> make_backend(const std::string& name, const std::string& dump_dir);

}  // namespace status
