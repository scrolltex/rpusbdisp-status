#include "backend/dump_backend.h"

#include <cstdio>

namespace status {

DumpBackend::DumpBackend(std::string dir) : dir_(std::move(dir)), fb_(size_t(kWidth) * kHeight, 0) {}

bool DumpBackend::blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* px) {
    for (int32_t row = 0; row < h; ++row)
        for (int32_t col = 0; col < w; ++col)
            fb_[size_t(y + row) * kWidth + size_t(x + col)] = px[size_t(row) * w + size_t(col)];
    dirty_ = true;
    return true;
}

bool DumpBackend::write_ppm(const std::string& path) const {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", int(kWidth), int(kHeight));
    for (uint16_t c : fb_) {
        unsigned char rgb[3] = {
            static_cast<unsigned char>(((c >> 11) & 0x1F) * 255 / 31),
            static_cast<unsigned char>(((c >> 5) & 0x3F) * 255 / 63),
            static_cast<unsigned char>((c & 0x1F) * 255 / 31),
        };
        std::fwrite(rgb, 1, 3, f);
    }
    return std::fclose(f) == 0;
}

void DumpBackend::poll() {
    flush_pending();
    auto now = std::chrono::steady_clock::now();
    if (!dirty_ || now < next_dump_) return;
    next_dump_ = now + std::chrono::seconds(1);
    std::string tmp = dir_ + "/frame.ppm.tmp";
    if (write_ppm(tmp)) std::rename(tmp.c_str(), (dir_ + "/frame.ppm").c_str());
    dirty_ = false;
}

}  // namespace status
