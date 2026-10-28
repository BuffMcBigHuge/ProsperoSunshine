// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace prospero {
inline bool has_idr(std::span<const uint8_t> bytes) {
    for (size_t i = 0; i + 3 < bytes.size(); ++i) {
        if (bytes[i] != 0 || bytes[i + 1] != 0) continue;
        size_t nal = i + 2;
        if (bytes[nal] == 0) ++nal;
        if (bytes[nal] == 1 && nal + 1 < bytes.size() && (bytes[nal + 1] & 0x1f) == 5) return true;
    }
    return false;
}
}
