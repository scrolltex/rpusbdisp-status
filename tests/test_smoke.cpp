#include <doctest/doctest.h>
#include <rp/drivers/display/rpusbdisp/rpusbdisp.h>

TEST_CASE("sdk links") {
    CHECK(rp::drivers::display::RoboPeakUsbDisplayDevice::ScreenWidth == 320);
}
