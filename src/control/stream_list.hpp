// SPDX-License-Identifier: GPL-3.0-only
// The control socket's STREAMS line for one stream. Sunshine knows each session's client and what it
// asked for; the PS5 capture knows what is delivered: up to two capture channels, and a device that
// joined a channel gets the mode, codec and bit rate of the stream that holds it (video_ps5.cpp).
#pragma once
#include <algorithm>
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

// A PS5 capture channel while a stream holds it.
struct CaptureFacts {
    int channel = 0;  // 0 the first, 1 the second
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
//  role=<capture|joined|starting> channel=<0|1|-> name=<name>". The name comes last because it may hold spaces; "-"
// stands for an unknown uuid, address, mode or channel. role: capture holds a capture channel, joined gets its picture,
// starting has neither yet (its values are what it asked for). channel: the capture channel it holds or joined, whose
// codec, dynamic range, mode and bit rate the line shows then.
inline std::string stream_line(const StreamFacts &stream, const std::vector<CaptureFacts> &captures, const std::string &uuid,
                               const std::string &name) {
    std::string role = "starting", channel = "-";
    int width = stream.width, height = stream.height, fps = stream.fps, kbps = stream.kbps;
    int video_format = stream.video_format;
    bool hdr = stream.hdr && stream.video_format != 0;
    for (const CaptureFacts &capture : captures) {
        if (stream.cert.empty()) break;
        const bool holds = capture.cert == stream.cert;
        if (!holds && std::find(capture.joined.begin(), capture.joined.end(), stream.cert) == capture.joined.end()) continue;
        role = holds ? "capture" : "joined";
        channel = std::to_string(capture.channel);
        width = capture.width;
        height = capture.height;
        fps = capture.fps;
        kbps = capture.kbps;
        video_format = capture.video_format;
        hdr = capture.hdr;
        break;
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
           " pads=" + std::to_string(std::max(0, stream.pads)) + " role=" + role + " channel=" + channel + " name=" + clean_name;
}

}
