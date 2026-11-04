// SPDX-License-Identifier: GPL-3.0-only
// HDR metadata the host announces for an HDR stream. Avcap2 reports none, and the stream carries no mastering SEI, only
// BT.2020 and PQ in its VUI. Sent as all zero, Moonlight hands zero primaries and a zero peak to the client's display
// (LiGetHdrMetadata reports them as valid), which on Windows came out oversaturated. Upstream Sunshine on Windows always
// sends the BT.2020 primaries, D65 and the display's luminance, so this sends the usual HDR10 values. Portable for tests.
#pragma once
#include <cstdint>

namespace prospero {

// Same fields and units as moonlight-common-c's SS_HDR_METADATA: chromaticities in 1/50000, luminance in nits, the
// minimum in 1/10000 nit.
struct Hdr10Metadata {
    struct Point { uint16_t x, y; };
    Point primaries[3];  // red, green, blue
    Point white_point;
    uint16_t max_display_luminance, min_display_luminance;
    uint16_t max_content_light_level, max_frame_average_light_level;
    uint16_t max_full_frame_luminance;
};

// BT.2020 primaries and D65 (ITU-R BT.2020, as Sunshine's Windows code sets them), a 1000-nit mastering display down to
// 0.005 nit, and content light levels of 1000 and 400 nits, the common HDR10 defaults. The console's own HDR peak setting
// isn't read yet.
inline constexpr Hdr10Metadata hdr10_default_metadata() {
    return Hdr10Metadata{
        {{35400, 14600}, {8500, 39850}, {6550, 2300}},
        {15635, 16450},
        1000, 50,
        1000, 400,
        400,
    };
}

}  // namespace prospero
