#pragma once

#include "ui/screen.h"

#include <string>

namespace status {

class DockerScreen : public Screen {
public:
    const char* name() const override { return "Docker"; }
    void create(lv_obj_t* parent) override;
    void update(const Snapshot& s) override;

private:
    void add_row(const ContainerInfo& c);

    lv_obj_t* summary_ = nullptr;
    lv_obj_t* list_ = nullptr;
    std::string last_signature_;
};

}  // namespace status
