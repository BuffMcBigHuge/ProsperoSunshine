// SPDX-License-Identifier: GPL-3.0-only
#include "input/pad_touch.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace prospero;

int main() {
    assert(touch_axis(0, touch_max_x) == 0);
    assert(touch_axis(1, touch_max_x) == 1919);
    assert(touch_axis(0.5f, touch_max_y) == 540);
    assert(touch_axis(-0.1f, touch_max_y) == 0);
    assert(touch_axis(1.5f, touch_max_y) == 1079);
    assert(touch_axis(std::numeric_limits<float>::quiet_NaN(), touch_max_x) == 0);
    assert(touch_axis(std::numeric_limits<float>::infinity(), touch_max_x) == 1919);

    PadTouch pad;
    assert(pad.count() == 0);
    // Not about contact, or a finger it doesn't know.
    assert(!pad.update(0x00, 1, 0.5f, 0.5f));  // hover
    assert(!pad.update(0x05, 1, 0.5f, 0.5f));  // button only
    assert(!pad.update(0x06, 1, 0.5f, 0.5f));  // hover leave
    assert(!pad.update(moonlight::touch_up, 1, 0, 0));
    assert(!pad.update(moonlight::touch_cancel_all, 0, 0, 0));

    assert(pad.update(moonlight::touch_down, 0xdeadbeef, 0, 1));
    assert(pad.count() == 1);
    const uint8_t first = pad.points()[0].id;
    assert(pad.points()[0].x == 0 && pad.points()[0].y == 1079);

    // A move keeps the id, a second finger gets its own.
    assert(pad.update(moonlight::touch_move, 0xdeadbeef, 1, 0));
    assert(pad.count() == 1 && pad.points()[0].id == first);
    assert(pad.points()[0].x == 1919 && pad.points()[0].y == 0);
    assert(pad.update(moonlight::touch_down, 7, 0.5f, 0.5f));
    assert(pad.count() == 2 && pad.points()[1].id != first);
    const uint8_t second = pad.points()[1].id;

    // The pad holds two fingers.
    assert(!pad.update(moonlight::touch_down, 8, 0, 0));
    assert(pad.count() == 2);

    // The remaining finger moves up and can still be found.
    assert(pad.update(moonlight::touch_up, 0xdeadbeef, 0, 0));
    assert(pad.count() == 1 && pad.points()[0].id == second);
    assert(pad.update(moonlight::touch_move, 7, 0, 0));
    assert(pad.count() == 1 && pad.points()[0].x == 0);
    assert(pad.update(moonlight::touch_cancel, 7, 0, 0));
    assert(pad.count() == 0);

    // A move without its down, as after a lost packet, puts the finger down.
    assert(pad.update(moonlight::touch_move, 9, 0, 0));
    assert(pad.update(moonlight::touch_down, 10, 0, 0));
    assert(pad.update(moonlight::touch_cancel_all, 0, 0, 0));
    assert(pad.count() == 0);

    // Ids stay within 7 bits and differ from the last one.
    uint8_t last = 0xff;
    for (int i = 0; i < 300; i++) {
        assert(pad.update(moonlight::touch_down, 1, 0, 0));
        const uint8_t id = pad.points()[0].id;
        assert(id < 0x80 && id != last);
        last = id;
        assert(pad.update(moonlight::touch_up, 1, 0, 0));
    }

    assert(std::fabs(accel_to_g(9.80665f) - 1) < 1e-5f);
    assert(std::fabs(gyro_to_rad(180) - 3.14159265f) < 1e-5f);
    assert(accel_to_g(0) == 0 && gyro_to_rad(0) == 0);

    std::cout << "pad_touch_test ok\n";
}
