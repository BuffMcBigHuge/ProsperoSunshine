// SPDX-License-Identifier: GPL-3.0-only
// Moonlight gamepad state (Xbox layout, platf::gamepad_state_t) → PS5 virtual DualSense state.
// Portable so it can be unit-tested on the build host; docs/input-mapping.md lists the table.
#pragma once
#include <cstdint>

namespace prospero {

// Moonlight button bits (Sunshine's platform/common.h).
namespace moonlight {
inline constexpr uint32_t dpad_up = 0x0001, dpad_down = 0x0002, dpad_left = 0x0004, dpad_right = 0x0008;
inline constexpr uint32_t start = 0x0010, back = 0x0020, left_stick = 0x0040, right_stick = 0x0080;
inline constexpr uint32_t left_button = 0x0100, right_button = 0x0200, home = 0x0400;
inline constexpr uint32_t a = 0x1000, b = 0x2000, x = 0x4000, y = 0x8000;
inline constexpr uint32_t touchpad = 0x100000;
}

// ScePadButton bits (src/platform/ps5/pad.h).
namespace dualsense {
inline constexpr uint32_t create = 0x1, l3 = 0x2, r3 = 0x4, options = 0x8;
inline constexpr uint32_t up = 0x10, right = 0x20, down = 0x40, left = 0x80;
inline constexpr uint32_t l2 = 0x100, r2 = 0x200, l1 = 0x400, r1 = 0x800;
inline constexpr uint32_t triangle = 0x1000, circle = 0x2000, cross = 0x4000, square = 0x8000;
inline constexpr uint32_t ps = 0x10000, touchpad = 0x100000;
}

struct PadState {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
};

// A trigger counts as pressed (the digital L2/R2 bit) above this analog value.
inline constexpr uint8_t trigger_threshold = 8;

// Moonlight sticks are signed 16-bit with +Y up; DualSense sticks are 0..255 with 0 at the top.
constexpr uint8_t stick_axis(int16_t value, bool invert) {
    int v = (static_cast<int>(value) + 32768) >> 8;  // 0..255
    if (invert) v = 255 - v;
    return static_cast<uint8_t>(v);
}

constexpr PadState map_gamepad(uint32_t flags, uint8_t lt, uint8_t rt, int16_t ls_x, int16_t ls_y, int16_t rs_x,
                               int16_t rs_y) {
    struct Pair { uint32_t from, to; };
    constexpr Pair table[] = {
        {moonlight::a, dualsense::cross},         {moonlight::b, dualsense::circle},
        {moonlight::x, dualsense::square},        {moonlight::y, dualsense::triangle},
        {moonlight::dpad_up, dualsense::up},      {moonlight::dpad_down, dualsense::down},
        {moonlight::dpad_left, dualsense::left},  {moonlight::dpad_right, dualsense::right},
        {moonlight::left_button, dualsense::l1},  {moonlight::right_button, dualsense::r1},
        {moonlight::left_stick, dualsense::l3},   {moonlight::right_stick, dualsense::r3},
        {moonlight::start, dualsense::options},   {moonlight::back, dualsense::create},
        {moonlight::home, dualsense::ps},         {moonlight::touchpad, dualsense::touchpad},
    };
    uint32_t buttons = 0;
    for (const auto &p : table)
        if (flags & p.from) buttons |= p.to;
    if (lt >= trigger_threshold) buttons |= dualsense::l2;
    if (rt >= trigger_threshold) buttons |= dualsense::r2;
    return {buttons, stick_axis(ls_x, false), stick_axis(ls_y, true), stick_axis(rs_x, false), stick_axis(rs_y, true),
            lt, rt};
}

}
