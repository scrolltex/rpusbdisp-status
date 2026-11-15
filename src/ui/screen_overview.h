#pragma once

#include "ui/screen.h"

namespace status {

class OverviewScreen : public Screen {
public:
    const char* name() const override { return "Overview"; }
    void create(lv_obj_t* parent) override;
    void update(const Snapshot& s) override;

private:
    struct Meter {
        lv_obj_t* value = nullptr;
        lv_obj_t* detail = nullptr;
        lv_obj_t* bar = nullptr;
    };
    Meter make_meter(lv_obj_t* parent, const char* title);
    static void set_meter(Meter& m, double pct, const char* detail);

    lv_obj_t* host_ = nullptr;
    lv_obj_t* uptime_ = nullptr;
    Meter cpu_, ram_;
    lv_obj_t* temp_ = nullptr;
};

}  // namespace status
