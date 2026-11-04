// SPDX-License-Identifier: GPL-3.0-only
// What video_ps5.cpp tells the rest of the host about the capture: which stream holds it, which joined it,
// and what it delivers to all of them (the control socket's STREAMS).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace video {

struct running_capture_t {
    std::string cert;  // the client certificate of the stream that holds the capture
    std::vector<std::string> joined;  // those of the streams that get its picture
    int video_format = 0;  // 0 H.264, 1 HEVC
    bool hdr = false;
    int width = 0, height = 0, fps = 0;  // what Avcap2 delivers now
    uint32_t average = 0;  // bit/s the channel opened with
};

// Nothing while no stream holds the capture.
std::optional<running_capture_t> running_capture();

}
