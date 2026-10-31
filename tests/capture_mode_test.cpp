// SPDX-License-Identifier: GPL-3.0-only
#include "video/capture_mode.hpp"
#include <cstdio>

using prospero::choose_capture_mode;

static_assert(choose_capture_mode(720, true).resolution == 4);
static_assert(choose_capture_mode(1080, true).resolution == 4);
static_assert(choose_capture_mode(1200, true).resolution == 4);
static_assert(choose_capture_mode(1440, true).resolution == 5);
static_assert(choose_capture_mode(1600, true).resolution == 5);
static_assert(choose_capture_mode(2160, true).resolution == 6);
static_assert(choose_capture_mode(4320, true).resolution == 6);
static_assert(choose_capture_mode(2160, true).width == 3840);
static_assert(choose_capture_mode(2160, false).resolution == 4);  // unverified firmware
static_assert(prospero::capture_mode_of(6).height == 2160);
static_assert(prospero::capture_mode_of(5).height == 1440);
static_assert(prospero::capture_mode_of(4).height == 1080);  // also what a fallback sets

int main() {
    std::puts("PASS: capture mode (1080p floor, 1440p, 2160p, unverified firmware stays at 1080p)");
    return 0;
}
