#pragma once

#include "ui/screen.h"

#include <memory>
#include <vector>

namespace status {

// Horizontal pager: one lv_tileview tile per screen plus page dots at the bottom.
class App {
public:
    void add(std::unique_ptr<Screen> s);
    // Page that is shown at start and returned to after inactivity.
    void set_home(int index) { home_ = index; }
    int home() const { return home_; }
    // Page offsets are relative to home: <0 left, 0 home, >0 right.
    bool valid_offset(int offset) const;
    void create(int start_offset = 0);
    // Slides back to home once the screen has not been touched for 60 s.
    void check_idle(uint32_t inactive_ms);
    static constexpr uint32_t kIdleReturnMs = 60000;
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
    int home_ = 0;
};

}  // namespace status
