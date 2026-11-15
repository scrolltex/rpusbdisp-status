#pragma once

#include "data/snapshot.h"

#include <lvgl.h>

namespace status {

// One swipeable page. To add a screen: subclass, then App::add() it in main.cpp.
class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* name() const = 0;
    // Build widgets inside `parent` (a 320x240 tile; keep the bottom 18 px free for page dots).
    virtual void create(lv_obj_t* parent) = 0;
    // Called every 500 ms from the UI thread.
    virtual void update(const Snapshot& s) = 0;
};

}  // namespace status
