#include "backend/backend.h"

#include "backend/dump_backend.h"
#include "backend/rpusbdisp_backend.h"
#if STATUS_SDL
#include "backend/sdl_backend.h"
#endif

namespace status {

void lvgl_init_once() {
    static bool done = false;
    if (!done) {
        lv_init();
        done = true;
    }
}

void rotate_rgb565_180(const uint16_t* src, uint16_t* dst, int32_t w, int32_t h) {
    const int32_t n = w * h;
    for (int32_t i = 0; i < n; ++i) dst[n - 1 - i] = src[i];
}

std::unique_ptr<Backend> make_backend(const std::string& name, const std::string& dump_dir) {
    if (name == "rpusbdisp") return std::make_unique<RpUsbDispBackend>();
    if (name == "dump") return std::make_unique<DumpBackend>(dump_dir);
#if STATUS_SDL
    if (name == "sdl") return std::make_unique<SdlBackend>();
#endif
    return nullptr;
}

}  // namespace status
