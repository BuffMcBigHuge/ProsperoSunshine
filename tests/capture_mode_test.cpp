// SPDX-License-Identifier: GPL-3.0-only
#include "video/capture_mode.hpp"
#include <cstdio>

using prospero::choose_capture_mode;

static_assert(choose_capture_mode(720, true, false).resolution == 4);
static_assert(choose_capture_mode(1080, true, false).resolution == 4);
static_assert(choose_capture_mode(1200, true, false).resolution == 4);
static_assert(choose_capture_mode(1440, true, false).resolution == 5);
static_assert(choose_capture_mode(1600, true, false).resolution == 5);
static_assert(choose_capture_mode(2160, true, false).resolution == 6);
static_assert(choose_capture_mode(4320, true, false).resolution == 6);
static_assert(choose_capture_mode(2160, true, false).width == 3840);
static_assert(choose_capture_mode(2160, false, false).resolution == 4);  // unverified firmware
// Below 1080p where those modes are offered (10.20): the nearest by height, a tie going to the larger one.
static_assert(choose_capture_mode(1079, false, true).resolution == 4);
static_assert(choose_capture_mode(1050, false, true).resolution == 4);  // 1680x1050
static_assert(choose_capture_mode(900, false, true).resolution == 4);   // 1600x900: as near to 720 as to 1080
static_assert(choose_capture_mode(899, false, true).resolution == 3);
static_assert(choose_capture_mode(800, false, true).resolution == 3);   // Steam Deck, 1280x800
static_assert(choose_capture_mode(768, false, true).resolution == 3);   // 1366x768
static_assert(choose_capture_mode(720, false, true).resolution == 3);
static_assert(choose_capture_mode(720, false, true).width == 1280);
static_assert(choose_capture_mode(630, false, true).resolution == 3);   // a tie
static_assert(choose_capture_mode(629, false, true).resolution == 2);
static_assert(choose_capture_mode(544, false, true).resolution == 2);   // PS Vita, 960x544
static_assert(choose_capture_mode(544, false, true).width == 960);
static_assert(choose_capture_mode(540, false, true).resolution == 2);
static_assert(choose_capture_mode(480, false, true).resolution == 2);   // Moonlight's 480p, 854x480
static_assert(choose_capture_mode(450, false, true).resolution == 2);   // a tie
static_assert(choose_capture_mode(449, false, true).resolution == 1);
static_assert(choose_capture_mode(360, false, true).resolution == 1);
static_assert(choose_capture_mode(240, false, true).resolution == 1);
static_assert(choose_capture_mode(360, false, true).width == 640);
static_assert(choose_capture_mode(1080, false, true).resolution == 4);
static_assert(choose_capture_mode(2160, false, true).resolution == 4);
static_assert(choose_capture_mode(2160, true, true).resolution == 6);
static_assert(choose_capture_mode(0, true, true).resolution == 4);      // no height asked
// Nothing changes from 1080 lines up, nor anywhere without the smaller modes: the rule as it was before them.
constexpr prospero::CaptureMode rule_before(int requested_height, bool large_modes_allowed) {
    if (!large_modes_allowed) return prospero::mode_1080p;
    if (requested_height >= 2160) return prospero::mode_2160p;
    if (requested_height >= 1440) return prospero::mode_1440p;
    return prospero::mode_1080p;
}
constexpr bool unchanged(int from, int to, bool small_modes_allowed) {
    for (int height = from; height <= to; ++height)
        for (const bool large : {false, true}) {
            const auto now = choose_capture_mode(height, large, small_modes_allowed), was = rule_before(height, large);
            if (now.resolution != was.resolution || now.width != was.width || now.height != was.height) return false;
        }
    return true;
}
static_assert(unchanged(1080, 4400, true));
static_assert(unchanged(-1, 4400, false));
static_assert(prospero::capture_mode_of(6).height == 2160);
static_assert(prospero::capture_mode_of(5).height == 1440);
static_assert(prospero::capture_mode_of(4).height == 1080);  // also what a fallback sets
static_assert(prospero::capture_mode_of(3).height == 720);
static_assert(prospero::capture_mode_of(2).height == 540);
static_assert(prospero::capture_mode_of(1).width == 640);

// Modes below 1080p have a highest rate, per frame; from 1080p on the host's own limit of 150 Mbit/s comes first.
static_assert(prospero::max_average_of(prospero::mode_360p, false) == 30'000'000u);
static_assert(prospero::max_average_of(prospero::mode_360p, true) == 15'000'000u);
static_assert(prospero::max_average_of(prospero::mode_540p, false) == 60'000'000u);
static_assert(prospero::max_average_of(prospero::mode_540p, true) == 30'000'000u);
static_assert(prospero::max_average_of(prospero::mode_720p, false) == 120'000'000u);
static_assert(prospero::max_average_of(prospero::mode_720p, true) == 60'000'000u);
static_assert(prospero::max_average_of(prospero::mode_1080p, false) > 150'000'000u);
static_assert(prospero::max_average_of(prospero::mode_1080p, true) > 150'000'000u);
static_assert(prospero::max_average_of(prospero::mode_2160p, true) > 150'000'000u);

int main() {
    std::puts("PASS: capture mode (1440p, 2160p, the nearest below 1080p where offered, else a 1080p floor, a rate cap below 1080p)");
    return 0;
}
