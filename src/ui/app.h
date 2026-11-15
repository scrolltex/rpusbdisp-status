#pragma once

#include "ui/screen.h"

#include <memory>
#include <vector>

namespace status {

// Horizontal pager: one lv_tileview tile per screen plus page dots at the bottom.
class App {
public:
    void add(std::unique_ptr<Screen> s);
    void create(int start_tile = 0);
    void update(const Snapshot& s);
    void show(int index, bool animate);
    int current() const { return current_; }

private:
    static void on_tile_changed(lv_event_t* e);
    static void on_dot_clicked(lv_event_t* e);
    void refresh_dots();

    std::vector<std::unique_ptr<Screen>> screens_;
    lv_obj_t* tileview_ = nullptr;
    std::vector<lv_obj_t*> tiles_;
    std::vector<lv_obj_t*> dots_;
    int current_ = 0;
};

}  // namespace status
