// SPDX-License-Identifier: GPL-3.0-only
// Slices in one Annex-B frame: H.264 NAL types 1 and 5, HEVC VCL types 0..31. For the log: the hardware decoders on
// Windows take at most 32 H.264 slices per frame (FFmpeg's MAX_SLICES), and 10.x sends 34 at 1080p without the slice
// fields. Tested in tests/video_test.cpp.
#ifndef PROSPERO_NAL_SLICES_H
#define PROSPERO_NAL_SLICES_H
#include <stddef.h>
#include <stdint.h>

// Most slices a frame may have and still decode in hardware on Windows.
#define PS_DECODER_MAX_SLICES 32

static inline int ps_nal_slices(const uint8_t *data, size_t size, int hevc)
{
    int slices = 0;
    for (size_t i = 0; i + 3 < size; i++) {
        if (data[i] != 0 || data[i + 1] != 0 || data[i + 2] != 1) continue;
        const int type = hevc ? (data[i + 3] >> 1) & 0x3f : data[i + 3] & 0x1f;
        if (hevc ? type < 32 : type == 1 || type == 5) ++slices;
        i += 2;
    }
    return slices;
}
#endif
