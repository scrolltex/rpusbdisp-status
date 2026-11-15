#pragma once

#include "backend/partial_backend.h"

#include <chrono>
#include <string>
#include <vector>

namespace status {

// Emulates the device framebuffer in memory and writes it to
// <dir>/frame.ppm about once a second. For checking the UI without hardware.
class DumpBackend : public PartialBackend {
public:
    explicit DumpBackend(std::string dir);
    void poll() override;
    bool write_ppm(const std::string& path) const;
    // Test hook: simulate a physical touch.
    void touch(int32_t x, int32_t y, bool pressed) { set_touch(x, y, pressed); }

protected:
    bool blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) override;

private:
    std::string dir_;
    std::vector<uint16_t> fb_;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point next_dump_{};
};

}  // namespace status
