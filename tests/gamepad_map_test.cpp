// SPDX-License-Identifier: GPL-3.0-only
#include "input/gamepad_map.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    const auto idle = map_gamepad(0, 0, 0, 0, 0, 0, 0);
    assert(idle.buttons == 0);
    assert(idle.lx == 128 && idle.ly == 127 && idle.rx == 128 && idle.ry == 127);  // centre within one step
    const auto face = map_gamepad(moonlight::a | moonlight::b | moonlight::x | moonlight::y, 0, 0, 0, 0, 0, 0);
    assert(face.buttons == (dualsense::cross | dualsense::circle | dualsense::square | dualsense::triangle));
    const auto system = map_gamepad(moonlight::home | moonlight::start | moonlight::back | moonlight::touchpad, 0, 0, 0,
                                    0, 0, 0);
    assert(system.buttons == (dualsense::ps | dualsense::options | dualsense::create | dualsense::touchpad));
    const auto corners = map_gamepad(0, 0, 0, -32768, 32767, 32767, -32768);
    assert(corners.lx == 0 && corners.ly == 0);      // full left, full up
    assert(corners.rx == 255 && corners.ry == 255);  // full right, full down
    const auto triggers = map_gamepad(0, trigger_threshold - 1, 255, 0, 0, 0, 0);
    assert(!(triggers.buttons & dualsense::l2) && (triggers.buttons & dualsense::r2));
    assert(triggers.l2 == trigger_threshold - 1 && triggers.r2 == 255);
    std::cout << "PASS: gamepad mapping (face, system, sticks, triggers)\n";
}
