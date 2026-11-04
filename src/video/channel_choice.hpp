// SPDX-License-Identifier: GPL-3.0-only
// Which capture channel a new stream gets, what the host offers while channels run, and whom /resume turns away.
// Portable, so that the tests can run it; video_ps5.cpp holds the channels.
//
// The console runs up to two capture channels at once (STATUS.md): the first on ResourceArbitrator profile 180, 182 above
// 1080p, the second on 175, up to 1080p. Two channels with the same codec and size deliver the same stream twice, so
// streams that want the same codec, dynamic range and size share one.
#pragma once
#include "video/capture_mode.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace prospero {

inline constexpr int capture_channels = 2;

// One channel as a new stream sees it.
struct ChannelState {
    bool streaming = false;  // a stream holds it
    int video_format = 0;    // 0 H.264, 1 HEVC
    bool hdr = false;
    int height = 0;  // what it delivers now
};
using Channels = std::array<ChannelState, capture_channels>;

enum class ChannelPick { join, open, refuse };
struct ChannelChoice {
    ChannelPick kind;
    int channel;  // -1 with refuse
};

// The channel for a new stream:
//  1. A channel with the same codec, dynamic range and size: the stream joins it.
//  2. The first channel while no stream holds it: the stream opens it.
//  3. The second channel while it is free (and didn't fail for this stream), up to 1080p: the stream opens it.
//  4. A channel with the same codec and dynamic range: the stream joins it and gets its size, as every device did before.
//  5. The second channel while it is free, at 1080p for a larger request.
//  Otherwise the stream is turned away.
constexpr ChannelChoice choose_channel(const Channels &channels, int video_format, bool hdr, const CaptureMode &wanted,
                                       bool second_usable) {
    const auto same_kind = [&](const ChannelState &ch) { return ch.streaming && ch.video_format == video_format && ch.hdr == hdr; };
    for (int i = 0; i < capture_channels; i++)
        if (same_kind(channels[i]) && channels[i].height == wanted.height) return {ChannelPick::join, i};
    if (!channels[0].streaming) return {ChannelPick::open, 0};
    const bool second_free = second_usable && !channels[1].streaming;
    if (second_free && wanted.resolution <= mode_1080p.resolution) return {ChannelPick::open, 1};
    for (int i = 0; i < capture_channels; i++)
        if (same_kind(channels[i])) return {ChannelPick::join, i};
    if (second_free) return {ChannelPick::open, 1};
    return {ChannelPick::refuse, -1};
}

// A device that asks for this dynamic range at /resume: nothing while it can stream (a free channel, or a running one
// with its dynamic range to join), otherwise whether the running streams are in HDR. The codec is not known there yet.
constexpr std::optional<bool> hdr_conflict(const Channels &channels, bool hdr) {
    for (const ChannelState &ch : channels)
        if (!ch.streaming || ch.hdr == hdr) return std::nullopt;
    return !hdr;
}

// Sunshine's active_hevc_mode (1 H.264 only, 2 also HEVC, 3 also HEVC Main 10) from what the host may offer at all: with a
// channel free everything, as a device that wants something else gets a channel of its own; with both busy only their
// codecs and dynamic ranges, since a further device joins one of them.
constexpr int offered_hevc_mode(int allowed, const Channels &channels) {
    bool any_hevc = false, any_hdr = false;
    for (const ChannelState &ch : channels) {
        if (!ch.streaming) return allowed;
        any_hevc |= ch.video_format == 1;
        any_hdr |= ch.video_format == 1 && ch.hdr;
    }
    return !any_hevc ? 1 : !any_hdr ? std::min(allowed, 2) : allowed;
}

}  // namespace prospero
