// SPDX-License-Identifier: GPL-3.0-only
// From what Avcap2 delivers to the layout Sunshine's encoder takes: channel i of a frame is Sunshine's speaker i
// (platform/common.h: FL, FR, FC, LFE, BL, BR, SL, SR; stereo is FL, FR, 5.1 the first six).
//
// Sources: encoder type 0 is stereo, type 3 has 12 channels (STATUS.md, "Für 0.3.0"). Which of the 12 is which speaker is
// not measured: a console that outputs stereo fills only 0 and 1. Assumed here is sceAudioOut's 8-channel order for the
// first eight, L, R, C, LFE, surround L, surround R, back L, back R; the last four are left out. Tested in
// tests/channel_map_test.cpp.
#ifndef PROSPERO_CHANNEL_MAP_HPP
#define PROSPERO_CHANNEL_MAP_HPP
#include <algorithm>
#include <cstddef>

namespace prospero {

inline constexpr int kAvcapSurroundChannels = 12;

// Appends `frames` frames of `out_channels` (2, 6 or 8) to `out`, from interleaved `in` with `in_channels` (2 or 12).
// Speakers the source doesn't have stay silent.
template <typename Out>
void map_channels(const float *in, int in_channels, std::size_t frames, int out_channels, Out &out)
{
    constexpr float half = 0.70710678f;  // -3 dB, as ITU downmixes centre and surrounds
    for (std::size_t i = 0; i < frames; ++i) {
        const float *f = in + i * static_cast<std::size_t>(in_channels);
        float speaker[8] = {f[0], f[1], 0, 0, 0, 0, 0, 0};
        if (in_channels >= 8) {
            if (out_channels == 2) {
                speaker[0] = f[0] + half * (f[2] + f[4] + f[6]);
                speaker[1] = f[1] + half * (f[2] + f[5] + f[7]);
            } else {
                speaker[2] = f[2];
                speaker[3] = f[3];
                if (out_channels == 8) {
                    speaker[4] = f[6];  // back
                    speaker[5] = f[7];
                    speaker[6] = f[4];  // side
                    speaker[7] = f[5];
                } else {
                    // 5.1's surrounds take the console's surround and back channels.
                    speaker[4] = f[4] + f[6];
                    speaker[5] = f[5] + f[7];
                }
            }
        }
        for (int c = 0; c < out_channels; ++c) out.push_back(speaker[std::min(c, 7)]);
    }
}

}  // namespace prospero
#endif
