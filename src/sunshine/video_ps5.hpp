// SPDX-License-Identifier: GPL-3.0-only
// What video_ps5.cpp tells the rest of the host about the capture: which stream holds each channel, which joined it,
// and what it delivers to all of them (the control socket's STREAMS).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace video {

struct running_capture_t {
    int channel = 0;  // 0 the first capture channel, 1 the second (capture.h's slots)
    std::string cert;  // the client certificate of the stream that holds the channel
    std::vector<std::string> joined;  // those of the streams that get its picture
    int video_format = 0;  // 0 H.264, 1 HEVC
    bool hdr = false;
    int width = 0, height = 0, fps = 0;  // what Avcap2 delivers now
    uint32_t average = 0;  // bit/s the channel opened with
};

// One entry per channel a stream holds, the first channel first; empty while none.
std::vector<running_capture_t> running_captures();

}
