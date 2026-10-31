// SPDX-License-Identifier: GPL-3.0-only
// Which Avcap2 mode serves a client's request. Portable, so that the tests can run it.
#pragma once
#include <cstdint>

namespace prospero {

struct CaptureMode {
    uint32_t resolution;  // for ps_capture_set_video_mode
    int width, height;
};

inline constexpr CaptureMode mode_1080p{4, 1920, 1080}, mode_1440p{5, 2560, 1440}, mode_2160p{6, 3840, 2160};

// The largest mode that doesn't exceed the request, at least 1080p: smaller requests get 1080p and the client scales it,
// because Avcap2 doesn't start below that.
constexpr CaptureMode choose_capture_mode(int requested_height, bool large_modes_verified) {
    if (!large_modes_verified) return mode_1080p;
    if (requested_height >= mode_2160p.height) return mode_2160p;
    if (requested_height >= mode_1440p.height) return mode_1440p;
    return mode_1080p;
}

constexpr CaptureMode capture_mode_of(uint32_t resolution) {
    return resolution == mode_2160p.resolution ? mode_2160p : resolution == mode_1440p.resolution ? mode_1440p : mode_1080p;
}

}  // namespace prospero
