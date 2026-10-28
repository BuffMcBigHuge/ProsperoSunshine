// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace prospero {
// True if the Annex-B buffer contains an H.264 NAL unit of the given type.
inline bool has_nal(std::span<const uint8_t> bytes, uint8_t type) {
    for (size_t i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] != 0 || bytes[i + 1] != 0) continue;
        size_t nal = i + 2;
        if (bytes[nal] == 0) ++nal;
        if (bytes[nal] == 1 && nal + 1 < bytes.size() && (bytes[nal + 1] & 0x1f) == type) return true;
    }
    return false;
}

inline bool has_idr(std::span<const uint8_t> bytes) { return has_nal(bytes, 5); }
inline bool has_sps(std::span<const uint8_t> bytes) { return has_nal(bytes, 7); }
}
