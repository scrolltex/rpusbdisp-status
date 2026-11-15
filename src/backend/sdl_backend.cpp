#include "backend/sdl_backend.h"

#if STATUS_SDL
namespace status {

bool SdlBackend::init(lv_display_rotation_t rotation) {
    lvgl_init_once();
    lv_display_t* d = lv_sdl_window_create(320, 240);
    if (!d) return false;
    lv_sdl_window_set_title(d, "rpusbdisp-status");
    lv_sdl_window_set_zoom(d, 2);
    lv_display_set_rotation(d, rotation);
    lv_indev_t* mouse = lv_sdl_mouse_create();
    lv_indev_set_display(mouse, d);
    return true;
}

}  // namespace status
#endif
