// SPDX-License-Identifier: GPL-3.0-only
#include "video/channel_choice.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

namespace {
constexpr int h264 = 0, hevc = 1;

ChannelState running(int video_format, bool hdr, const CaptureMode &mode) { return {true, video_format, hdr, mode.height}; }

bool is(const ChannelChoice &choice, ChannelPick kind, int channel) { return choice.kind == kind && choice.channel == channel; }
}

int main() {
    const Channels none{};
    // Nothing runs: the first channel, whatever is asked for, also 4K.
    assert(is(choose_channel(none, hevc, true, mode_2160p, true), ChannelPick::open, 0));
    assert(is(choose_channel(none, h264, false, mode_720p, true), ChannelPick::open, 0));

    // H.264 1080p runs on the first channel.
    Channels one{running(h264, false, mode_1080p), ChannelState{}};
    // The same mode joins it.
    assert(is(choose_channel(one, h264, false, mode_1080p, true), ChannelPick::join, 0));
    // Another codec, dynamic range or size up to 1080p gets the second channel.
    assert(is(choose_channel(one, hevc, false, mode_1080p, true), ChannelPick::open, 1));
    assert(is(choose_channel(one, hevc, true, mode_1080p, true), ChannelPick::open, 1));
    assert(is(choose_channel(one, h264, false, mode_720p, true), ChannelPick::open, 1));
    assert(is(choose_channel(one, h264, false, mode_360p, true), ChannelPick::open, 1));
    // More than 1080p with the same codec: the first channel's picture, as before.
    assert(is(choose_channel(one, h264, false, mode_2160p, true), ChannelPick::join, 0));
    // More than 1080p with another codec: the second channel, at 1080p, rather than a refusal.
    assert(is(choose_channel(one, hevc, true, mode_2160p, true), ChannelPick::open, 1));
    // The second channel failed for this stream: what the first channel alone allows, as before.
    assert(is(choose_channel(one, h264, false, mode_720p, false), ChannelPick::join, 0));
    assert(is(choose_channel(one, hevc, false, mode_1080p, false), ChannelPick::refuse, -1));

    // Both run: H.264 1080p and HEVC HDR 720p.
    Channels both{running(h264, false, mode_1080p), running(hevc, true, mode_720p)};
    assert(is(choose_channel(both, hevc, true, mode_720p, true), ChannelPick::join, 1));
    assert(is(choose_channel(both, h264, false, mode_1080p, true), ChannelPick::join, 0));
    // A third device with a known codec and dynamic range but another size joins that channel.
    assert(is(choose_channel(both, hevc, true, mode_1080p, true), ChannelPick::join, 1));
    assert(is(choose_channel(both, h264, false, mode_540p, true), ChannelPick::join, 0));
    // One that matches neither is turned away.
    assert(is(choose_channel(both, hevc, false, mode_1080p, true), ChannelPick::refuse, -1));
    // A stream that delivers 1080p after a fall back is joined by a 1080p request with its codec, not by a 4K one first.
    Channels fell_back{running(hevc, false, mode_1080p), running(h264, false, mode_720p)};
    assert(is(choose_channel(fell_back, hevc, false, mode_1080p, true), ChannelPick::join, 0));
    assert(is(choose_channel(fell_back, h264, false, mode_720p, true), ChannelPick::join, 1));

    // Only the second channel runs (its first stream's device left): the first is free for anything else.
    Channels second_only{ChannelState{}, running(h264, false, mode_720p)};
    assert(is(choose_channel(second_only, h264, false, mode_720p, true), ChannelPick::join, 1));
    assert(is(choose_channel(second_only, h264, false, mode_1080p, true), ChannelPick::open, 0));
    assert(is(choose_channel(second_only, hevc, true, mode_2160p, true), ChannelPick::open, 0));

    // /resume: nothing runs, or a channel is free, or one runs in the asked dynamic range.
    assert(!hdr_conflict(none, true) && !hdr_conflict(one, true) && !hdr_conflict(second_only, true));
    assert(!hdr_conflict(both, true) && !hdr_conflict(both, false));
    const Channels both_sdr{running(h264, false, mode_1080p), running(hevc, false, mode_720p)};
    assert(hdr_conflict(both_sdr, true) == std::optional<bool>(false) && !hdr_conflict(both_sdr, false));
    const Channels both_hdr{running(hevc, true, mode_1080p), running(hevc, true, mode_720p)};
    assert(hdr_conflict(both_hdr, false) == std::optional<bool>(true));

    // Offers: everything while a channel is free, else the running codecs and dynamic ranges.
    for (int allowed = 1; allowed <= 3; allowed++) {
        assert(offered_hevc_mode(allowed, none) == allowed);
        assert(offered_hevc_mode(allowed, one) == allowed);
        assert(offered_hevc_mode(allowed, second_only) == allowed);
    }
    const Channels both_h264{running(h264, false, mode_1080p), running(h264, false, mode_720p)};
    assert(offered_hevc_mode(3, both_h264) == 1);
    assert(offered_hevc_mode(3, both_sdr) == 2 && offered_hevc_mode(2, both_sdr) == 2);
    assert(offered_hevc_mode(3, both) == 3 && offered_hevc_mode(3, both_hdr) == 3);
    // HEVC switched off since (a failure, hevc_mode = 1): never more than allowed.
    assert(offered_hevc_mode(1, both_sdr) == 1);

    std::cout << "PASS: capture channels (same mode joins, own channel up to 1080p, fallback, offers, /resume)\n";
    return 0;
}
