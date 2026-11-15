#include "ui/screen_docker.h"

#include "ui/theme.h"

#include <cstdio>

namespace status {

void DockerScreen::create(lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(parent, false);

    summary_ = lv_label_create(parent);
    lv_obj_set_style_text_font(summary_, &lv_font_montserrat_20, 0);
    lv_label_set_recolor(summary_, true);
    lv_label_set_text(summary_, "Docker");

    list_ = lv_obj_create(parent);
    lv_obj_remove_style_all(list_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 2, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list_, lv_color_hex(0x5A646E), LV_PART_SCROLLBAR);
}

void DockerScreen::add_row(const ContainerInfo& c) {
    lv_obj_t* r = lv_obj_create(list_);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), 22);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_style_pad_right(r, 6, 0);
    lv_obj_set_clickable(r, false);
    lv_obj_set_event_bubble(r, true);

    lv_obj_t* dot = lv_obj_create(r);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, level_color(level_container(c)), 0);

    lv_obj_t* name = lv_label_create(r);
    lv_label_set_text(name, c.name.c_str());
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(name, 1);

    lv_obj_t* st = lv_label_create(r);
    lv_label_set_text(st, short_status(c.status).c_str());
    lv_obj_set_style_text_color(st, color_dim(), 0);
}

void DockerScreen::update(const Snapshot& s) {
    const auto& d = s.docker;
    std::string sig = d.ok ? "ok" : "err:" + d.error;
    for (const auto& c : d.containers) sig += "|" + c.name + ":" + c.state + ":" + short_status(c.status);
    if (sig == last_signature_) return;
    last_signature_ = sig;

    lv_obj_clean(list_);
    if (!d.ok) {
        lv_label_set_text(summary_, "Docker");
        lv_obj_t* l = lv_label_create(list_);
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_color(l, level_color(Level::Crit), 0);
        lv_label_set_text(l, ("Docker unavailable: " + d.error).c_str());
        return;
    }

    int running = 0;
    for (const auto& c : d.containers) running += c.state == "running";
    const int other = int(d.containers.size()) - running;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Docker  #2ECC71 %d running#  #8A949E %d other#", running, other);
    lv_label_set_text(summary_, buf);

    if (d.containers.empty()) {
        lv_obj_t* l = lv_label_create(list_);
        lv_obj_set_style_text_color(l, color_dim(), 0);
        lv_label_set_text(l, "No containers");
        return;
    }
    for (const auto& c : d.containers) add_row(c);
}

}  // namespace status
