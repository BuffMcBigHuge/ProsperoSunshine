// SPDX-License-Identifier: GPL-3.0-only
// The HDR metadata announced for an HDR stream: BT.2020, D65, no field left at zero.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "video/hdr10_metadata.hpp"

static bool near(uint16_t value, double chromaticity) { return std::fabs(value / 50000.0 - chromaticity) < 0.0005; }

int main() {
    constexpr auto m = prospero::hdr10_default_metadata();
    // ITU-R BT.2020 primaries and the D65 white point.
    assert(near(m.primaries[0].x, 0.708) && near(m.primaries[0].y, 0.292));
    assert(near(m.primaries[1].x, 0.170) && near(m.primaries[1].y, 0.797));
    assert(near(m.primaries[2].x, 0.131) && near(m.primaries[2].y, 0.046));
    assert(near(m.white_point.x, 0.3127) && near(m.white_point.y, 0.3290));
    // A zero here is what the client got before; every field must carry a value.
    assert(m.max_display_luminance == 1000 && m.min_display_luminance == 50);
    assert(m.max_content_light_level == 1000 && m.max_frame_average_light_level == 400);
    assert(m.max_frame_average_light_level <= m.max_content_light_level);
    assert(m.max_full_frame_luminance > 0 && m.max_full_frame_luminance <= m.max_display_luminance);
    std::puts("PASS: HDR streams announce BT.2020, D65 and HDR10 luminance");
    return 0;
}
