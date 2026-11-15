#pragma once

#include "ui/format.h"

#include <lvgl.h>

namespace status {

inline lv_color_t level_color(Level l) {
    switch (l) {
        case Level::Ok: return lv_color_hex(0x2ECC71);
        case Level::Warn: return lv_color_hex(0xF1C40F);
        case Level::Crit: return lv_color_hex(0xE74C3C);
        case Level::Neutral: break;
    }
    return lv_color_hex(0x7F8C8D);
}

inline lv_color_t color_bg() { return lv_color_hex(0x101418); }
inline lv_color_t color_text() { return lv_color_hex(0xECF0F1); }
inline lv_color_t color_dim() { return lv_color_hex(0x8A949E); }

constexpr int32_t kDotsHeight = 18;

}  // namespace status
