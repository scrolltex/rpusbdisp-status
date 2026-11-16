#include "ui/screen_overview.h"

#include "ui/theme.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <thread>

namespace status {

namespace {
lv_obj_t* row(lv_obj_t* parent) {
    lv_obj_t* r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(r, 6, 0);
    return r;
}

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}
}  // namespace

void OverviewScreen::create(lv_obj_t* parent) {
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 6, 0);
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* head = row(parent);
    host_ = label(head, &lv_font_montserrat_20, color_text(), "");
    lv_label_set_long_mode(host_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(host_, 1);
    uptime_ = label(head, &lv_font_montserrat_14, color_dim(), "");

    cpu_ = make_meter(parent, "CPU");
    ram_ = make_meter(parent, "RAM");

    lv_obj_t* t = row(parent);
    lv_obj_set_style_pad_top(t, 4, 0);
    label(t, &lv_font_montserrat_14, color_dim(), "TEMP");
    temp_ = label(t, &lv_font_montserrat_28, color_text(), "--");

    lv_obj_t* n = row(parent);
    label(n, &lv_font_montserrat_14, color_dim(), "NET");
    net_ = label(n, &lv_font_montserrat_20, color_dim(), "--");
}

OverviewScreen::Meter OverviewScreen::make_meter(lv_obj_t* parent, const char* title) {
    Meter m;
    lv_obj_t* r = row(parent);
    lv_obj_t* left = lv_obj_create(r);
    lv_obj_remove_style_all(left);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(left, 8, 0);
    label(left, &lv_font_montserrat_14, color_dim(), title);
    m.value = label(left, &lv_font_montserrat_20, color_text(), "--");
    m.detail = label(r, &lv_font_montserrat_14, color_dim(), "");

    m.bar = lv_bar_create(parent);
    lv_obj_set_size(m.bar, LV_PCT(100), 14);
    lv_bar_set_range(m.bar, 0, 100);
    lv_obj_set_style_bg_color(m.bar, lv_color_hex(0x2A3139), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(m.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(m.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(m.bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(m.bar, 300, LV_PART_MAIN);
    return m;
}

void OverviewScreen::set_meter(Meter& m, double pct, const char* detail) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
    lv_label_set_text(m.value, buf);
    lv_label_set_text(m.detail, detail);
    lv_bar_set_value(m.bar, int32_t(std::lround(pct)), LV_ANIM_ON);
    lv_obj_set_style_bg_color(m.bar, level_color(level_percent(pct)), LV_PART_INDICATOR);
}

void OverviewScreen::update(const Snapshot& s) {
    lv_label_set_text(host_, s.sys.hostname.c_str());
    lv_label_set_text(uptime_, ("up " + format_uptime(s.sys.uptime_s)).c_str());

    char cores[32];
    std::snprintf(cores, sizeof(cores), "%u cores", std::max(1u, std::thread::hardware_concurrency()));
    set_meter(cpu_, s.sys.cpu, cores);

    const auto& m = s.sys.mem;
    double ram_pct = m.total_kb ? 100.0 * double(m.total_kb - m.available_kb) / double(m.total_kb) : 0.0;
    std::string ram_detail = format_bytes_kb(m.total_kb - m.available_kb) + " / " + format_bytes_kb(m.total_kb);
    set_meter(ram_, ram_pct, ram_detail.c_str());

    if (s.sys.temp_c) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f\xC2\xB0" "C", *s.sys.temp_c);
        lv_label_set_text(temp_, buf);
        lv_obj_set_style_text_color(temp_, level_color(level_temp(*s.sys.temp_c)), 0);
    } else {
        lv_label_set_text(temp_, "--");
        lv_obj_set_style_text_color(temp_, color_dim(), 0);
    }

    if (s.sys.net.ok) {
        const auto& r = s.sys.net.rate;
        std::string text = LV_SYMBOL_DOWN " " + format_rate(r.rx_bps) + "   " LV_SYMBOL_UP " " + format_rate(r.tx_bps);
        lv_label_set_text(net_, text.c_str());
        lv_obj_set_style_text_color(net_, net_active(r) ? color_text() : color_dim(), 0);
    } else {
        lv_label_set_text(net_, "--");
        lv_obj_set_style_text_color(net_, color_dim(), 0);
    }
}

}  // namespace status
