// hwcheck — verifies the RoboPeak USB display without LVGL.
//
// What to look for:
//   * Top half: four vertical stripes, left to right RED, GREEN, BLUE, WHITE
//     (drawn with fillrect).
//   * Bottom half: a horizontal gradient black -> RED (drawn with bitblt,
//     exactly how the real program draws).
//   Colours are sent as BGR565 (the RoboPeak display's order). Run with --rgb
//   to send RGB565 instead. If red and blue are swapped in BGR mode, start
//   rpusbdisp-status with --color-order rgb; if colours are garbled, add
//   --swap-bytes.
//   * Touch the four corners: the console prints the coordinates; they should
//     be close to (0,0), (319,0), (0,239), (319,239).
//   * Unplug and re-plug the cable: the program waits and redraws.
#include <rp/drivers/display/rpusbdisp/rpusbdisp.h>
#include <rp/deps/libusbx_wrap/libusbx_wrap.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace rp::drivers::display;

static std::atomic<bool> g_stop{false};
static bool g_bgr = true;

static uint16_t px(uint16_t rgb565) {
    return g_bgr ? uint16_t(((rgb565 & 0x001F) << 11) | (rgb565 & 0x07E0) | (rgb565 >> 11)) : rgb565;
}

static void on_signal(int) { g_stop = true; }

static void draw_pattern(RoboPeakUsbDisplayDevice& d) {
    const uint16_t W = 320, H = 240;
    const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    for (int i = 0; i < 4; ++i) {
        uint16_t x0 = i * W / 4;
        uint16_t x1 = (i + 1) * W / 4 - 1;
        d.fillrect(x0, 0, x1, H / 2 - 1, px(colors[i]), RoboPeakUsbDisplayBitOperationCopy);
    }
    std::vector<uint16_t> buf(W * (H / 2));
    for (int y = 0; y < H / 2; ++y)
        for (int x = 0; x < W; ++x)
            buf[y * W + x] = px(static_cast<uint16_t>(((x * 31) / (W - 1)) << 11));
    d.bitblt(0, H / 2, W, H / 2, RoboPeakUsbDisplayBitOperationCopy, buf.data());
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--rgb") g_bgr = false;
    std::printf("sending colours as %s565\n", g_bgr ? "BGR" : "RGB");
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    while (!g_stop) {
        std::shared_ptr<RoboPeakUsbDisplayDevice> display;
        try {
            display = RoboPeakUsbDisplayDevice::openFirstDevice();
        } catch (rp::util::Exception& e) {
            e.printToConsole();
        }
        if (!display) {
            std::fprintf(stderr, "waiting for display...\n");
            for (int i = 0; i < 20 && !g_stop; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        try {
            std::printf("display S/N %s connected\n", display->getDevice()->getSerialNumber().c_str());
            display->setStatusUpdatedCallback([](const rpusbdisp_status_normal_packet_t& s) {
                std::printf("touch x=%d y=%d pressed=%d\n", (int)s.touch_x, (int)s.touch_y,
                            s.touch_status == RPUSBDISP_TOUCH_STATUS_PRESSED ? 1 : 0);
                std::fflush(stdout);
            });
            display->enable();
            draw_pattern(*display);
            std::printf("pattern drawn: top RED GREEN BLUE WHITE, bottom black->RED gradient\n");
            std::fflush(stdout);

            while (!g_stop && display->isAlive())
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } catch (rp::util::Exception& e) {
            e.printToConsole();
        }
        if (!g_stop) std::fprintf(stderr, "display disconnected\n");
    }
    return 0;
}
