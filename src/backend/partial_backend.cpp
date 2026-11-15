#include "backend/partial_backend.h"

#include <algorithm>
#include <cstring>

namespace status {

namespace {
alignas(64) uint16_t g_buf1[PartialBackend::kWidth * PartialBackend::kBufLines];
alignas(64) uint16_t g_buf2[PartialBackend::kWidth * PartialBackend::kBufLines];
}  // namespace

bool PartialBackend::init(lv_display_rotation_t rotation) {
    lvgl_init_once();
    disp_ = lv_display_create(kWidth, kHeight);
    if (!disp_) return false;
    lv_display_set_color_format(disp_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp_, g_buf1, g_buf2, sizeof(g_buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp_, &PartialBackend::flush_cb);
    lv_display_set_driver_data(disp_, this);
    lv_display_set_rotation(disp_, rotation);
    tmp_.resize(size_t(kWidth) * kBufLines);

    indev_ = lv_indev_create();
    lv_indev_set_type(indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_, &PartialBackend::read_cb);
    lv_indev_set_driver_data(indev_, this);
    lv_indev_set_display(indev_, disp_);
    return true;
}

void PartialBackend::flush_cb(lv_display_t* d, const lv_area_t* area, uint8_t* px_map) {
    auto* self = static_cast<PartialBackend*>(lv_display_get_driver_data(d));
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    const uint16_t* px = reinterpret_cast<const uint16_t*>(px_map);
    lv_area_t phys = *area;

    if (lv_display_get_rotation(d) == LV_DISPLAY_ROTATION_180) {
        rotate_rgb565_180(px, self->tmp_.data(), w, h);
        lv_display_rotate_area(d, &phys);
        px = self->tmp_.data();
    } else if (self->swap_bytes_) {
        std::memcpy(self->tmp_.data(), px, size_t(w) * h * 2);
        px = self->tmp_.data();
    }
    if (self->swap_bytes_) lv_draw_rgb565_swap(const_cast<uint16_t*>(px), uint32_t(w * h));

    self->blit(phys.x1, phys.y1, w, h, px);
    lv_display_flush_ready(d);
}

void PartialBackend::set_touch(int32_t x, int32_t y, bool pressed) {
    std::lock_guard<std::mutex> lk(touch_mu_);
    touch_x_ = std::clamp<int32_t>(x, 0, kWidth - 1);
    touch_y_ = std::clamp<int32_t>(y, 0, kHeight - 1);
    touch_pressed_ = pressed;
}

void PartialBackend::read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<PartialBackend*>(lv_indev_get_driver_data(indev));
    std::lock_guard<std::mutex> lk(self->touch_mu_);
    data->point.x = self->touch_x_;  // physical; LVGL applies display rotation
    data->point.y = self->touch_y_;
    data->state = self->touch_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void PartialBackend::invalidate_all() {
    if (disp_) lv_obj_invalidate(lv_display_get_screen_active(disp_));
}

}  // namespace status
