#include "ui/app.h"

#include "ui/theme.h"

namespace status {

void App::add(std::unique_ptr<Screen> s) { screens_.push_back(std::move(s)); }

void App::create(int start_tile) {
    lv_display_t* disp = lv_display_get_default();
    lv_theme_t* th = lv_theme_default_init(disp, lv_color_hex(0x3498DB), lv_color_hex(0x2ECC71), true,
                                           &lv_font_montserrat_14);
    lv_display_set_theme(disp, th);

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, color_bg(), 0);
    lv_obj_set_style_text_color(scr, color_text(), 0);
    lv_obj_set_style_text_font(scr, &lv_font_montserrat_14, 0);

    tileview_ = lv_tileview_create(scr);
    lv_obj_set_size(tileview_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(tileview_, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tileview_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview_, &App::on_tile_changed, LV_EVENT_VALUE_CHANGED, this);

    const int n = int(screens_.size());
    for (int i = 0; i < n; ++i) {
        lv_dir_t dir = LV_DIR_NONE;
        if (i > 0) dir = lv_dir_t(dir | LV_DIR_LEFT);
        if (i < n - 1) dir = lv_dir_t(dir | LV_DIR_RIGHT);
        lv_obj_t* tile = lv_tileview_add_tile(tileview_, uint8_t(i), 0, dir);
        lv_obj_set_style_pad_all(tile, 8, 0);
        lv_obj_set_style_pad_bottom(tile, kDotsHeight, 0);
        tiles_.push_back(tile);
        screens_[size_t(i)]->create(tile);
    }

    // Page dots: each is a 26x18 touch target with a small circle inside.
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, kDotsHeight);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < n; ++i) {
        lv_obj_t* hit = lv_obj_create(bar);
        lv_obj_remove_style_all(hit);
        lv_obj_set_size(hit, 26, kDotsHeight);
        lv_obj_set_clickable(hit, true);
        lv_obj_add_event_cb(hit, &App::on_dot_clicked, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(hit, reinterpret_cast<void*>(intptr_t(i)));
        lv_obj_t* dot = lv_obj_create(hit);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_center(dot);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_clickable(dot, false);
        dots_.push_back(dot);
    }

    if (start_tile < 0 || start_tile >= n) start_tile = 0;
    show(start_tile, false);
}

void App::show(int index, bool animate) {
    if (index < 0 || index >= int(tiles_.size())) return;
    lv_tileview_set_tile_by_index(tileview_, uint32_t(index), 0, animate ? LV_ANIM_ON : LV_ANIM_OFF);
    current_ = index;
    refresh_dots();
}

void App::refresh_dots() {
    for (size_t i = 0; i < dots_.size(); ++i)
        lv_obj_set_style_bg_color(dots_[i], int(i) == current_ ? color_text() : lv_color_hex(0x3A434C), 0);
}

void App::on_tile_changed(lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    lv_obj_t* active = lv_tileview_get_tile_active(self->tileview_);
    for (size_t i = 0; i < self->tiles_.size(); ++i)
        if (self->tiles_[i] == active) self->current_ = int(i);
    self->refresh_dots();
}

void App::on_dot_clicked(lv_event_t* e) {
    auto* self = static_cast<App*>(lv_event_get_user_data(e));
    auto* hit = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    self->show(int(intptr_t(lv_obj_get_user_data(hit))), true);
}

void App::update(const Snapshot& s) {
    for (auto& sc : screens_) sc->update(s);
}

}  // namespace status
