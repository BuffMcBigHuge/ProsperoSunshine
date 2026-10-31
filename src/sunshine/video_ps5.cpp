// SPDX-License-Identifier: GPL-3.0-only
// Replaces Sunshine's video.cpp. Avcap2 already delivers H.264 or HEVC, so there is no encoder, display or
// colour conversion: frames from CaptureSession go straight into the stream's packet queue.
#include "capture/capture_session.hpp"
#include "platform/ps5/capture.h"
#include "video/capture_mode.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/video.h"
#include <x86intrin.h>
#include <cstring>
#include <thread>

using namespace std::literals;

namespace video {
// Only 8-bit SDR 4:2:0 is offered. In Sunshine 1 means "disabled" and 2 "Main"; 0 would still advertise
// HEVC/AV1 in the RTSP DESCRIBE reply. Set in probe_encoders.
int active_hevc_mode = 1;
int active_av1_mode = 1;
bool last_encoder_probe_supported_ref_frames_invalidation = false;
std::array<bool, 3> last_encoder_probe_supported_yuv444_for_codec = {false, false, false};
std::array<bool, 3> last_encoder_probe_supported_dynamic_range_for_codec = {false, false, false};
std::array<bool, 3> last_encoder_probe_supported_dynamic_range_yuv444_for_codec = {false, false, false};

namespace {
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
                                                   std::chrono::steady_clock::time_point read_time, bool hevc) {
    uint64_t stamp;
    std::memcpy(&stamp, desc.data() + (hevc ? 0x58 : 0x48), sizeof stamp);
    const uint64_t now = __rdtsc();
    if (!stamp || stamp > now) return read_time;
    const auto age = std::chrono::nanoseconds(static_cast<int64_t>((now - stamp) / tsc_per_ns()));
    return age < 1s ? read_time - age : read_time;
}
}

int probe_encoders() {
    tsc_per_ns();
    // The HEVC setup is measured on firmware 13.00 only. Elsewhere a client that prefers HEVC would lose the
    // stream if it fails, while H.264 may still run. hevc_mode = 1 in sunshine.conf turns it off.
    active_hevc_mode = config::video.hevc_mode != 1 && ps_capture_hevc_verified() ? 2 : 1;
    BOOST_LOG(info) << "PS5: Avcap2 passthrough, "sv << (active_hevc_mode == 2 ? "H.264 and HEVC, "sv : "H.264, "sv)
                    << (ps_capture_large_modes_verified() ? "up to 3840x2160, "sv : "1920x1080, "sv) << "TSC "sv
                    << tsc_per_ns() << " ticks/ns"sv;
    return 0;
}

void capture(safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = mail::man->queue<packet_t>(mail::video_packets);
    auto idr_events = mail->event<bool>(mail::idr);
    auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);
    auto hdr_event = mail->event<hdr_info_t>(mail::hdr);

    // Avcap2 encodes at 30 or 60 fps, 1080p and up; smaller modes do not start (mode_spike, STATUS.md).
    const bool hevc = config.videoFormat == 1;
    const bool low_rate = config.framerate > 0 && config.framerate <= 30;
    const auto mode = prospero::choose_capture_mode(config.height, ps_capture_large_modes_verified());
    BOOST_LOG(info) << "PS5 capture requested "sv << config.width << 'x' << config.height << '@' << config.framerate
                    << ", delivering "sv << mode.width << 'x' << mode.height << '@' << (low_rate ? 30 : 60);
    int delivered_height = 0;
    const auto announce_mode = [&](const prospero::CaptureMode &delivered) {
        if (delivered.height == delivered_height) return;
        delivered_height = delivered.height;
        input::touch_port_t port{};
        port.width = port.logical_width = delivered.width;
        port.height = port.logical_height = delivered.height;
        touch_port_event->raise(port);
    };
    announce_mode(mode);
    hdr_event->raise(std::make_unique<hdr_info_raw_t>(false));
    // The session falls back to 1080p when the system refuses a larger mode.
    const auto check_fallback = [&] {
        const auto delivered = prospero::capture_mode_of(ps_capture_video_resolution());
        if (delivered.height != delivered_height)
            BOOST_LOG(warning) << "PS5 capture: larger mode refused by the system, delivering "sv << delivered.width << 'x'
                               << delivered.height;
        announce_mode(delivered);
    };

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

    // config.bitrate is in kbit/s. The channel opens with its bitrate in the config: raising it later above the
    // opening value stops the encoder (docs/re/hevc.md). Peak as Remote Play: H.264 average * 1.12, HEVC * 1.10.
    constexpr uint32_t default_average = 6'000'000, max_average = 50'000'000;
    const uint32_t requested = config.bitrate > 0 ? static_cast<uint32_t>(config.bitrate) * 1000u : default_average;
    const uint32_t average = std::min(requested, max_average);
    ps_capture_set_opening_bitrate(average, average / 100 * (hevc ? 110 : 112));
    ps_capture_set_codec(hevc ? 1 : 0);
    ps_capture_set_video_mode(mode.resolution, low_rate ? 0 : 1);
    const auto codec = hevc ? prospero::Codec::hevc : prospero::Codec::h264;
    prospero::CaptureSession session(prospero::make_ps5_capture_backend(codec), codec);
    if (const int rc = session.start()) {
        BOOST_LOG(error) << "PS5 capture failed to start: "sv << rc;
        shutdown_event->raise(true);
        return;
    }
    check_fallback();
    BOOST_LOG(info) << "PS5 capture "sv << (hevc ? "HEVC"sv : "H.264"sv) << " bitrate "sv << average / 1000 << " kbit/s (requested "sv
                    << requested / 1000 << ')';
    // The audio thread may have opened the channel earlier, and the next periodic IDR can be ten seconds away.
    if (session.request_keyframe())
        BOOST_LOG(warning) << "PS5 capture: first keyframe request failed"sv;
    prospero::EncodedFrame frame;
    prospero::CaptureSession::Descriptor desc{};
    // Moonlight asks for an IDR after loss; Avcap2 delivers one within 2-3 frames (R1).
    while (!shutdown_event->peek()) {
        if (idr_events->peek()) {
            idr_events->pop();
            if (session.request_keyframe())
                BOOST_LOG(warning) << "PS5 capture: keyframe request failed, next periodic IDR within ten seconds"sv;
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
            BOOST_LOG(info) << "PS5 capture reopened after "sv
                            << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                     lost_at).count()
                            << " ms"sv;
            check_fallback();
            continue;
        }
        auto packet = std::make_unique<packet_raw_generic>(std::move(frame.bytes), static_cast<int64_t>(frame.index),
                                                           frame.idr);
        packet->channel_data = channel_data;
        packet->frame_timestamp = capture_time(desc, frame.read_time, hevc);
        packets->raise(std::move(packet));
        frame.bytes = {};
    }
    session.stop();
}
}
