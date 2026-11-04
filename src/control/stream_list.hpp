// SPDX-License-Identifier: GPL-3.0-only
// The control socket's STREAMS line for one stream. Sunshine knows each session's client and what it
// asked for; the PS5 capture knows what is delivered: one encoder for every device, so a device that
// joined gets the mode, codec and bit rate of the stream that holds the capture (video_ps5.cpp).
#pragma once
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace prospero {

// One Sunshine session, as rtsp_stream::session_infos() gives it.
struct StreamFacts {
    std::string cert;
    std::string address;
    int width = 0, height = 0, fps = 0;
    int kbps = 0;  // what the client asked for, kbit/s
    int video_format = 0;  // 0 H.264, 1 HEVC, 2 AV1
    bool hdr = false;  // the client asked for 10 bit
    long long seconds = 0;  // since the session started
    int pads = 0;  // virtual gamepads of the client
};

// The PS5 capture while a stream holds it.
struct CaptureFacts {
    std::string cert;  // the stream that holds it
    std::vector<std::string> joined;  // the streams that get its picture
    int width = 0, height = 0, fps = 0;
    int kbps = 0;  // the average it opened with
    int video_format = 0;
    bool hdr = false;
};

inline std::string codec_name(int video_format) {
    return video_format == 1 ? "HEVC" : video_format == 2 ? "AV1" : "H.264";
}

// "OK uuid=<uuid> address=<ip> codec=<H.264|HEVC|AV1> hdr=<0|1> mode=<WxH@fps> kbps=<n> secs=<n> pads=<n>
//  role=<capture|joined|starting> name=<name>". The name comes last because it may hold spaces; "-" stands for
// an unknown uuid, address or mode. role: capture holds the capture, joined gets its picture, starting has
// neither yet (its values are what it asked for).
inline std::string stream_line(const StreamFacts &stream, const std::optional<CaptureFacts> &capture,
                               const std::string &uuid, const std::string &name) {
    std::string role = "starting";
    int width = stream.width, height = stream.height, fps = stream.fps, kbps = stream.kbps;
    int video_format = stream.video_format;
    bool hdr = stream.hdr && stream.video_format != 0;
    if (capture && !stream.cert.empty()) {
        const bool holds = capture->cert == stream.cert;
        if (holds || std::find(capture->joined.begin(), capture->joined.end(), stream.cert) != capture->joined.end()) {
            role = holds ? "capture" : "joined";
            width = capture->width;
            height = capture->height;
            fps = capture->fps;
            kbps = capture->kbps;
            video_format = capture->video_format;
            hdr = capture->hdr;
        }
    }
    const std::string mode = width > 0 && height > 0 && fps > 0
                                 ? std::to_string(width) + 'x' + std::to_string(height) + '@' + std::to_string(fps)
                                 : "-";
    // A name is one line; the app takes the rest of it.
    std::string clean_name = name;
    std::replace_if(clean_name.begin(), clean_name.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
    // An IPv4 client on a dual-stack socket: its plain address, as STATUS gives it.
    std::string address = stream.address;
    if (address.rfind("::ffff:", 0) == 0 && address.find('.') != std::string::npos) address.erase(0, 7);
    return "OK uuid=" + (uuid.empty() ? "-" : uuid) + " address=" + (address.empty() ? "-" : address) +
           " codec=" + codec_name(video_format) + " hdr=" + (hdr ? "1" : "0") + " mode=" + mode +
           " kbps=" + std::to_string(std::max(0, kbps)) + " secs=" + std::to_string(std::max(0LL, stream.seconds)) +
           " pads=" + std::to_string(std::max(0, stream.pads)) + " role=" + role + " name=" + clean_name;
}

}
