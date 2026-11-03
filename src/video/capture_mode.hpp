// SPDX-License-Identifier: GPL-3.0-only
// Which Avcap2 mode serves a client's request. Portable, so that the tests can run it.
#pragma once
#include <cstdint>
#include <initializer_list>

namespace prospero {

struct CaptureMode {
    uint32_t resolution;  // for ps_capture_set_video_mode
    int width, height;
};

inline constexpr CaptureMode mode_360p{1, 640, 360}, mode_540p{2, 960, 540}, mode_720p{3, 1280, 720}, mode_1080p{4, 1920, 1080},
                             mode_1440p{5, 2560, 1440}, mode_2160p{6, 3840, 2160};

// From 1080 lines up, the largest mode that doesn't exceed the request. Below that, where the smaller modes are allowed, the
// nearest by height, a tie going to the larger one: 960x544 (PS Vita) gets 960x540, 854x480 960x540, 1280x800 1280x720, and
// 1600x900 keeps 1080p. Without them smaller requests get 1080p and the client scales it.
// A request without a height gets 1080p.
constexpr CaptureMode choose_capture_mode(int requested_height, bool large_modes_allowed, bool small_modes_allowed) {
    if (requested_height <= 0) return mode_1080p;
    if (requested_height < mode_1080p.height && small_modes_allowed) {
        const auto distance = [requested_height](const CaptureMode &mode) {
            return mode.height > requested_height ? mode.height - requested_height : requested_height - mode.height;
        };
        CaptureMode nearest = mode_1080p;
        for (const CaptureMode &mode : {mode_720p, mode_540p, mode_360p})
            if (distance(mode) < distance(nearest)) nearest = mode;
        return nearest;
    }
    if (!large_modes_allowed) return mode_1080p;
    if (requested_height >= mode_2160p.height) return mode_2160p;
    if (requested_height >= mode_1440p.height) return mode_1440p;
    return mode_1080p;
}

// The highest average in bit/s to open a mode with. Too many bits per frame and Start fails below 1080p on 13.00. H.264 at 360p
// is the tightest: it ran at 29 Mbit/s at 60 fps and not at 39, at 30 fps at 15 and not at 20; HEVC takes twice that
// (STATUS.md). 10.x sets no rate, only a QP from it (encoder_qp.h), so the cap costs it nothing at these sizes.
constexpr uint32_t max_average_of(const CaptureMode &mode, bool low_rate) {
    const uint32_t at_60_fps = mode.resolution > mode_720p.resolution    ? UINT32_MAX
                               : mode.resolution == mode_720p.resolution ? 120'000'000u
                               : mode.resolution == mode_540p.resolution ? 60'000'000u
                                                                         : 30'000'000u;
    return low_rate ? at_60_fps / 2 : at_60_fps;
}

constexpr CaptureMode capture_mode_of(uint32_t resolution) {
    for (const CaptureMode &mode : {mode_360p, mode_540p, mode_720p, mode_1440p, mode_2160p})
        if (mode.resolution == resolution) return mode;
    return mode_1080p;
}

}  // namespace prospero
