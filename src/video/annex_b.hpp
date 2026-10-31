// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace prospero {
enum class Codec { h264, hevc };

// True if the Annex-B buffer contains a NAL unit with a type in [first, last].
inline bool has_nal(std::span<const uint8_t> bytes, uint8_t first, uint8_t last, Codec codec = Codec::h264) {
    for (size_t i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] != 0 || bytes[i + 1] != 0) continue;
        size_t nal = i + 2;
        if (bytes[nal] == 0) ++nal;
        if (bytes[nal] != 1 || nal + 1 >= bytes.size()) continue;
        const uint8_t type = codec == Codec::hevc ? (bytes[nal + 1] >> 1) & 0x3f : bytes[nal + 1] & 0x1f;
        if (type >= first && type <= last) return true;
    }
    return false;
}

// HEVC: any random access point (BLA, IDR, CRA).
inline bool has_idr(std::span<const uint8_t> bytes, Codec codec = Codec::h264) {
    return codec == Codec::hevc ? has_nal(bytes, 16, 21, codec) : has_nal(bytes, 5, 5);
}
// HEVC: the VPS, which comes first of its parameter sets.
inline bool has_sps(std::span<const uint8_t> bytes, Codec codec = Codec::h264) {
    return codec == Codec::hevc ? has_nal(bytes, 32, 32, codec) : has_nal(bytes, 7, 7);
}
}
