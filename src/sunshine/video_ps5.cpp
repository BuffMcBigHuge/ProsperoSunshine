// SPDX-License-Identifier: GPL-3.0-only
// Replaces Sunshine's video.cpp. Avcap2 already delivers H.264, so there is no encoder, display or
// colour conversion: frames from CaptureSession go straight into the stream's packet queue.
#include "capture/capture_session.hpp"
#include "platform/ps5/capture.h"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/video.h"
#include <x86intrin.h>
#include <cstring>
#include <thread>

using namespace std::literals;

namespace video {
// Only H.264 SDR 4:2:0 is offered (see ROADMAP v1.0 scope). In Sunshine 1 means "disabled"; 0 would
// still advertise HEVC/AV1 in the RTSP DESCRIBE reply, and clients that prefer HEVC (Moonlight on
// iOS) then wait for HEVC keyframes in our H.264 stream and give up with "no video frame".
int active_hevc_mode = 1;
int active_av1_mode = 1;
bool last_encoder_probe_supported_ref_frames_invalidation = false;
std::array<bool, 3> last_encoder_probe_supported_yuv444_for_codec = {false, false, false};
std::array<bool, 3> last_encoder_probe_supported_dynamic_range_for_codec = {false, false, false};
std::array<bool, 3> last_encoder_probe_supported_dynamic_range_yuv444_for_codec = {false, false, false};

namespace {
constexpr int width = 1920, height = 1080;

// TSC ticks per steady_clock nanosecond, calibrated once; Avcap2 stamps frames with the TSC
// (descriptor word 9, docs/avcap2-descriptor.md).
double tsc_per_ns() {
    static const double ratio = [] {
        const auto t0 = std::chrono::steady_clock::now();
        const uint64_t c0 = __rdtsc();
        std::this_thread::sleep_for(100ms);
        const uint64_t c1 = __rdtsc();
        const auto t1 = std::chrono::steady_clock::now();
        return static_cast<double>(c1 - c0) / std::chrono::duration<double, std::nano>(t1 - t0).count();
    }();
    return ratio;
}

std::chrono::steady_clock::time_point capture_time(const prospero::CaptureSession::Descriptor &desc,
                                                   std::chrono::steady_clock::time_point read_time) {
    uint64_t stamp;
    std::memcpy(&stamp, desc.data() + 0x48, sizeof stamp);
    const uint64_t now = __rdtsc();
    if (!stamp || stamp > now) return read_time;
    const auto age = std::chrono::nanoseconds(static_cast<int64_t>((now - stamp) / tsc_per_ns()));
    return age < 1s ? read_time - age : read_time;
}
}

int probe_encoders() {
    tsc_per_ns();
    BOOST_LOG(info) << "PS5: Avcap2 H.264 passthrough, "sv << width << 'x' << height << ", TSC "sv << tsc_per_ns()
                    << " ticks/ns"sv;
    return 0;
}

void capture(safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = mail::man->queue<packet_t>(mail::video_packets);
    auto idr_events = mail->event<bool>(mail::idr);
    auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);
    auto hdr_event = mail->event<hdr_info_t>(mail::hdr);

    // Avcap2 encodes 1080p at 30 or 60 fps; other resolutions do not start (mode_spike, STATUS.md).
    const bool low_rate = config.framerate > 0 && config.framerate <= 30;
    BOOST_LOG(info) << "PS5 capture requested "sv << config.width << 'x' << config.height << '@' << config.framerate
                    << ", delivering "sv << width << 'x' << height << '@' << (low_rate ? 30 : 60);
    input::touch_port_t port{};
    port.width = port.logical_width = width;
    port.height = port.logical_height = height;
    touch_port_event->raise(port);
    hdr_event->raise(std::make_unique<hdr_info_raw_t>(false));

    // One encoder channel, one stream: a second session would read frames from the same Avcap2 ring
    // and starve the first. It is ended here; a reconnecting client gets through once the old
    // session has timed out (ping_timeout).
    static std::atomic_bool streaming{false};
    if (streaming.exchange(true)) {
        BOOST_LOG(warning) << "PS5 capture busy with another session; ending the new one"sv;
        shutdown_event->raise(true);
        return;
    }
    const auto release = util::fail_guard([] { streaming = false; });

    ps_capture_set_video_mode(4, low_rate ? 0 : 1);
    prospero::CaptureSession session(prospero::make_ps5_capture_backend());
    if (const int rc = session.start()) {
        BOOST_LOG(error) << "PS5 capture failed to start: "sv << rc;
        shutdown_event->raise(true);
        return;
    }
    // config.bitrate is in kbit/s. Measured on FW 13.00 (R3): averages up to 15 Mbit/s with
    // peak = average * 1.12 (RemotePlay's ratio) work; 16 Mbit/s and above stop the encoder.
    constexpr uint32_t opening_average = 6'000'000, max_average = 15'000'000;
    const uint32_t requested = config.bitrate > 0 ? static_cast<uint32_t>(config.bitrate) * 1000u : opening_average;
    const uint32_t average = std::min(requested, max_average);
    if (average != opening_average && session.set_bitrate(average, average / 100 * 112))
        BOOST_LOG(warning) << "PS5 capture: could not set bitrate to "sv << average;
    BOOST_LOG(info) << "PS5 capture bitrate "sv << average / 1000 << " kbit/s (requested "sv << requested / 1000 << ')';
    prospero::EncodedFrame frame;
    prospero::CaptureSession::Descriptor desc{};
    // Moonlight asks for an IDR after loss; Avcap2 delivers one within 2-3 frames (R1).
    while (!shutdown_event->peek()) {
        if (idr_events->peek()) {
            idr_events->pop();
            if (session.request_keyframe())
                BOOST_LOG(warning) << "PS5 capture: keyframe request failed, next periodic IDR within one second"sv;
        }
        const auto status = session.read(frame, 100ms, {}, &desc);
        if (status == prospero::CaptureStatus::timeout) continue;
        if (status != prospero::CaptureStatus::frame) {
            // Starting or closing a game takes the capture resource away for a moment; reopen
            // instead of ending the stream. The first frame after a reopen is an IDR.
            BOOST_LOG(warning) << "PS5 capture lost ("sv << static_cast<int>(status) << "), reopening"sv;
            const auto lost_at = std::chrono::steady_clock::now();
            bool recovered = false;
            while (!shutdown_event->peek() && std::chrono::steady_clock::now() - lost_at < 20s) {
                std::this_thread::sleep_for(250ms);
                if (session.start() == 0) {
                    recovered = true;
                    break;
                }
            }
            if (!recovered) {
                BOOST_LOG(error) << "PS5 capture could not be reopened; ending the session"sv;
                shutdown_event->raise(true);
                break;
            }
            if (average != opening_average) session.set_bitrate(average, average / 100 * 112);
            BOOST_LOG(info) << "PS5 capture reopened after "sv
                            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                     lost_at).count()
                            << " ms"sv;
            continue;
        }
        auto packet = std::make_unique<packet_raw_generic>(std::move(frame.bytes), static_cast<int64_t>(frame.index),
                                                           frame.idr);
        packet->channel_data = channel_data;
        packet->frame_timestamp = capture_time(desc, frame.read_time);
        packets->raise(std::move(packet));
        frame.bytes = {};
    }
    session.stop();
}
}
