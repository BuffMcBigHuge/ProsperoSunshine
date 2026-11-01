// SPDX-License-Identifier: GPL-3.0-only
// Moonlight touchpad and motion events → what the PS5 virtual DualSense takes.
// Portable so it can be unit-tested on the build host; docs/re/pad-input.md has the layouts.
#pragma once
#include <array>
#include <cstdint>

namespace prospero {

// moonlight-common-c's Limelight.h.
namespace moonlight {
inline constexpr uint8_t touch_down = 0x01, touch_up = 0x02, touch_move = 0x03, touch_cancel = 0x04, touch_cancel_all = 0x07;
inline constexpr uint8_t motion_accel = 0x01, motion_gyro = 0x02;
}

inline constexpr uint16_t touch_max_x = 1919, touch_max_y = 1079;

// A client's 0..1 on the pad's scale. Written so that anything else off the network, NaN too, ends up in range.
constexpr uint16_t touch_axis(float value, uint16_t max) {
    if (!(value > 0)) return 0;
    if (value >= 1) return max;
    return static_cast<uint16_t>(value * max + 0.5f);
}

// Sunshine gives m/s² and deg/s; the pad takes g and rad/s.
constexpr float accel_to_g(float value) { return value / 9.80665f; }
constexpr float gyro_to_rad(float value) { return value * (3.14159265f / 180.0f); }

struct TouchPoint {
    uint8_t id;
    uint16_t x, y;
};

// The fingers on one pad. Clients name a finger with a 32-bit pointer; the pad wants 7 bits and at most two, and a lifted
// finger simply leaves the list.
class PadTouch {
public:
    // True if the list changed. A third finger and events that aren't about contact (hover, button only) change nothing.
    bool update(uint8_t event, uint32_t pointer, float x, float y) {
        switch (event) {
        case moonlight::touch_down:
        case moonlight::touch_move: {
            const TouchPoint scaled{0, touch_axis(x, touch_max_x), touch_axis(y, touch_max_y)};
            if (const int i = find(pointer); i >= 0) {
                points_[i].x = scaled.x;
                points_[i].y = scaled.y;
                return true;
            }
            if (count_ == points_.size()) return false;
            pointers_[count_] = pointer;
            points_[count_] = scaled;
            points_[count_].id = next_id_;
            next_id_ = (next_id_ + 1) & 0x7f;
            count_++;
            return true;
        }
        case moonlight::touch_up:
        case moonlight::touch_cancel: {
            const int i = find(pointer);
            if (i < 0) return false;
            if (i == 0 && count_ == 2) {
                pointers_[0] = pointers_[1];
                points_[0] = points_[1];
            }
            count_--;
            return true;
        }
        case moonlight::touch_cancel_all: {
            const bool had = count_;
            clear();
            return had;
        }
        default:
            return false;
        }
    }

    void clear() { count_ = 0; }
    uint8_t count() const { return count_; }
    const std::array<TouchPoint, 2> &points() const { return points_; }

private:
    int find(uint32_t pointer) const {
        for (int i = 0; i < count_; i++) {
            if (pointers_[i] == pointer) return i;
        }
        return -1;
    }

    std::array<TouchPoint, 2> points_{};
    std::array<uint32_t, 2> pointers_{};
    uint8_t count_ = 0;
    uint8_t next_id_ = 0;
};

}
