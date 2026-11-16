#include "ui/screen_disks.h"

#include "ui/theme.h"

#include <cmath>
#include <cstdio>

namespace status {

namespace {

const char* hex(lv_color_t c) {
    static char buf[8];
    std::snprintf(buf, sizeof(buf), "%02X%02X%02X", c.red, c.green, c.blue);
    return buf;
}

std::string colored(const std::string& text, lv_color_t c) { return std::string("#") + hex(c) + " " + text + "#"; }

lv_obj_t* hrow(lv_obj_t* parent) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_clickable(r, false);
    lv_obj_set_event_bubble(r, true);
    return r;
}

std::string layout_key(const std::vector<DiskInfo>& disks) {
    std::string k;
    for (const auto& d : disks) k += d.mount_point + (d.nfs ? "|n" : "|l") + (d.available ? "a" : "u") + "\n";
    return k;
}

}  // namespace

void DisksScreen::create(lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 4, 0);
    lv_obj_set_scrollable(parent, false);

    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text(title, "Disks");

    list_ = lv_obj_create(parent);
    lv_obj_remove_style_all(list_);
    lv_obj_set_width(list_, LV_PCT(100));
    lv_obj_set_flex_grow(list_, 1);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_, 6, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(list_, lv_color_hex(0x5A646E), LV_PART_SCROLLBAR);
}

void DisksScreen::rebuild(const std::vector<DiskInfo>& disks) {
    lv_obj_clean(list_);
    rows_.clear();
    if (disks.empty()) {
        lv_obj_t* l = lv_label_create(list_);
        lv_obj_set_style_text_color(l, color_dim(), 0);
        lv_label_set_text(l, "No disks found");
        return;
    }
    for (const auto& d : disks) {
        Row r;
        lv_obj_t* box = lv_obj_create(list_);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, 2, 0);
        lv_obj_set_style_pad_right(box, 6, 0);
        lv_obj_set_clickable(box, false);
        lv_obj_set_event_bubble(box, true);

        lv_obj_t* head = hrow(box);
        r.mount = lv_label_create(head);
        lv_label_set_long_mode(r.mount, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_flex_grow(r.mount, 1);
        r.info = lv_label_create(head);
        lv_label_set_recolor(r.info, true);

        if (d.available) {
            lv_obj_t* mid = hrow(box);
            r.bar = lv_bar_create(mid);
            lv_obj_set_height(r.bar, 10);
            lv_obj_set_flex_grow(r.bar, 1);
            lv_bar_set_range(r.bar, 0, 100);
            lv_obj_set_style_bg_color(r.bar, lv_color_hex(0x2A3139), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(r.bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_radius(r.bar, 3, LV_PART_MAIN);
            lv_obj_set_style_radius(r.bar, 3, LV_PART_INDICATOR);
            r.space = lv_label_create(mid);

            r.io = lv_label_create(box);
            lv_obj_set_style_text_color(r.io, color_dim(), 0);
        }
        rows_.push_back(r);
    }
}

void DisksScreen::fill(Row& r, const DiskInfo& d) {
    lv_label_set_text(r.mount, d.mount_point.c_str());

    if (!d.available) {
        lv_label_set_text(r.info, colored("unavailable", level_color(Level::Crit)).c_str());
        return;
    }

    std::string info;
    if (d.nfs) {
        info = colored("nfs", color_dim());
    } else {
        if (!d.device.empty()) info += colored(d.device, color_dim());
        if (d.temp_c) {
            char t[16];
            std::snprintf(t, sizeof(t), "%.0f\xC2\xB0" "C", *d.temp_c);
            info += "  " + colored(t, level_color(level_disk_temp(*d.temp_c)));
        }
        if (d.smart != SmartState::Unknown) {
            const char* name = d.smart == SmartState::Ok ? "OK" : d.smart == SmartState::Warn ? "WARN" : "FAIL";
            Level lvl = d.smart == SmartState::Ok ? Level::Ok : d.smart == SmartState::Warn ? Level::Warn : Level::Crit;
            std::string badge = std::string(name) + (d.smart_stale ? "?" : "");
            info += "  " + colored(badge, d.smart_stale ? color_dim() : level_color(lvl));
            if (d.wear_pct) info += " " + colored(std::to_string(*d.wear_pct) + "%", color_dim());
        }
    }
    lv_label_set_text(r.info, info.c_str());

    const double pct = d.total ? 100.0 * double(d.used) / double(d.total) : 0.0;
    lv_bar_set_value(r.bar, int32_t(std::lround(pct)), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(r.bar, level_color(level_disk_pct(pct)), LV_PART_INDICATOR);
    char sp[48];
    std::snprintf(sp, sizeof(sp), "%.0f%%  %s/%s", pct, format_size(d.used).c_str(), format_size(d.total).c_str());
    lv_label_set_text(r.space, sp);

    if (d.io)
        lv_label_set_text(r.io, ("R " + format_rate(d.io->rx_bps) + "   W " + format_rate(d.io->tx_bps)).c_str());
    else
        lv_label_set_text(r.io, "R --   W --");
}

void DisksScreen::update(const Snapshot& s) {
    const auto& disks = s.sys.disks;
    std::string key = layout_key(disks);
    if (key != layout_ || rows_.empty() != disks.empty()) {
        rebuild(disks);
        layout_ = key;
    }
    for (size_t i = 0; i < rows_.size() && i < disks.size(); ++i) fill(rows_[i], disks[i]);
}

}  // namespace status
