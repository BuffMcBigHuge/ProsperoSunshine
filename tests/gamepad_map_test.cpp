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
    // As Teardown sends it: resistance on the second block only.
    const TriggerBlock feedback = {0x03, 0x05, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x21, 0xfe, 0x03, 0x48, 0x92, 0x24, 0x09};
    const auto on = map_trigger_effects(feedback, triggers_off);
    assert(on.flags == trigger_flag_right);
    assert(on.type_left == 0x05 && on.type_right == 0x21);
    assert(on.right[0] == 0xfe && on.right[5] == 0x09 && on.right[9] == 0 && on.left[0] == 0);
    assert(map_trigger_effects(triggers_off, feedback).flags == trigger_flag_right);
    assert(map_trigger_effects(triggers_off, TriggerBlock{}).flags == (trigger_flag_left | trigger_flag_right));
    TriggerBlock left_only = triggers_off;
    left_only[11] = 1;  // last parameter of the first block
    assert(map_trigger_effects(left_only, triggers_off).flags == trigger_flag_left);
    TriggerBlock mask_only = triggers_off;
    mask_only[0] = 0x01;
    mask_only[23] = 0xff;
    assert(map_trigger_effects(mask_only, triggers_off).flags == 0);
    // Only player 1 has PS unless every player may.
    constexpr uint32_t with_home = moonlight::home | moonlight::a | moonlight::back;
    static_assert(allowed_buttons(with_home, 0, false) == with_home);
    static_assert(allowed_buttons(with_home, 1, false) == (moonlight::a | moonlight::back));
    static_assert(allowed_buttons(with_home, 3, false) == (moonlight::a | moonlight::back));
    static_assert(allowed_buttons(with_home, 2, true) == with_home);
    static_assert(allowed_buttons(0, 1, false) == 0);
    std::cout << "PASS: gamepad mapping (face, system, sticks, triggers, trigger effects, PS for player 1 only)\n";
}
