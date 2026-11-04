// SPDX-License-Identifier: GPL-3.0-only
#include "audio/channel_map.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using prospero::map_channels;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }

int main() {
    // Stereo in, stereo out: unchanged, as before surround.
    const float stereo[] = {0.1f, 0.2f, 0.3f, 0.4f};
    std::vector<float> out;
    map_channels(stereo, 2, 2, 2, out);
    assert(out == std::vector<float>(stereo, stereo + 4));
    // Stereo in, 5.1 out: front left and right, the rest silent.
    out.clear();
    map_channels(stereo, 2, 1, 6, out);
    assert((out == std::vector<float>{0.1f, 0.2f, 0, 0, 0, 0}));

    // 12 channels: each speaker carries its index + 1, so the order shows.
    float twelve[12];
    for (int c = 0; c < 12; ++c) twelve[c] = static_cast<float>(c + 1);
    out.clear();
    map_channels(twelve, 12, 1, 8, out);
    assert((out == std::vector<float>{1, 2, 3, 4, 7, 8, 5, 6}));  // FL FR FC LFE BL BR SL SR
    out.clear();
    map_channels(twelve, 12, 1, 6, out);
    assert((out == std::vector<float>{1, 2, 3, 4, 5 + 7, 6 + 8}));
    // Stereo from 12: centre and surrounds at -3 dB, no LFE.
    out.clear();
    map_channels(twelve, 12, 1, 2, out);
    assert(out.size() == 2 && near(out[0], 1 + 0.70710678f * (3 + 5 + 7)) && near(out[1], 2 + 0.70710678f * (3 + 6 + 8)));
    // What a console that outputs stereo delivers: only 0 and 1. Every layout gets just that, in front.
    float front_only[24] = {};
    front_only[0] = 0.5f, front_only[1] = -0.5f, front_only[12] = 0.25f, front_only[13] = -0.25f;
    for (const int channels : {2, 6, 8}) {
        out.clear();
        map_channels(front_only, 12, 2, channels, out);
        assert(out.size() == static_cast<std::size_t>(2 * channels));
        assert(out[0] == 0.5f && out[1] == -0.5f && out[static_cast<std::size_t>(channels)] == 0.25f);
        for (int c = 2; c < channels; ++c) assert(out[static_cast<std::size_t>(c)] == 0 && out[static_cast<std::size_t>(channels + c)] == 0);
    }
    std::cout << "PASS: channel map (stereo unchanged, 5.1 and 7.1 order, downmix, a stereo console in every layout)\n";
}
