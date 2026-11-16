#pragma once

#include "ui/screen.h"

#include <string>
#include <vector>

namespace status {

// Space, temperature, SMART and activity per mounted filesystem.
class DisksScreen : public Screen {
public:
    const char* name() const override { return "Disks"; }
    void create(lv_obj_t* parent) override;
    void update(const Snapshot& s) override;

    // For tests.
    size_t row_count() const { return rows_.size(); }
    bool row_has_bar(size_t i) const { return i < rows_.size() && rows_[i].bar != nullptr; }

private:
    struct Row {
        lv_obj_t* mount = nullptr;
        lv_obj_t* info = nullptr;   // "nvme0  48°C  OK 3%" / "nfs" / "unavailable"
        lv_obj_t* bar = nullptr;
        lv_obj_t* space = nullptr;  // "62%  290/465G"
        lv_obj_t* io = nullptr;     // "R 12 MB/s  W 3 MB/s"
    };
    void rebuild(const std::vector<DiskInfo>& disks);
    void fill(Row& r, const DiskInfo& d);

    lv_obj_t* list_ = nullptr;
    std::vector<Row> rows_;
    std::string layout_;  // rows are rebuilt only when this changes
};

}  // namespace status
