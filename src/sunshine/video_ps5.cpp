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
#include "src/rtsp.h"
#include "src/stream.h"
#include "src/video.h"
#include <x86intrin.h>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

using namespace std::literals;

namespace video {
// 4:2:0 only. In Sunshine 1 means "disabled", 2 "Main" and 3 "Main and Main 10"; 0 would still advertise
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

// The HEVC setup is measured on firmware 13.00 and 10.20 only. Elsewhere a client that prefers HEVC would lose the stream
// if it fails, while H.264 may still run, so it takes a switch in the app, and a function that fails is off until the
// host restarts: the client's next stream sees that here (serverinfo and RTSP read these for each stream).
// hevc_mode = 1 in sunshine.conf turns it off, 2 turns off HDR.
void update_offers() {
    const int hevc_mode = config::video.hevc_mode;
    active_hevc_mode = hevc_mode == 1 || !ps_capture_hevc_allowed() ? 1 : hevc_mode == 2 || !ps_capture_hdr_allowed() ? 2 : 3;
    last_encoder_probe_supported_dynamic_range_for_codec[1] = active_hevc_mode == 3;
}
}

int probe_encoders() {
    tsc_per_ns();
    update_offers();
    BOOST_LOG(info) << "PS5: Avcap2 passthrough, "sv
                    << (active_hevc_mode == 3   ? "H.264, HEVC and HEVC HDR, "sv
                        : active_hevc_mode == 2 ? "H.264 and HEVC, "sv
                                                : "H.264, "sv)
                    << (ps_capture_small_modes_allowed() ? "640x360 to "sv : ps_capture_large_modes_allowed() ? "up to "sv : ""sv)
                    << (ps_capture_large_modes_allowed() ? "3840x2160, "sv : "1920x1080, "sv) << "TSC "sv
                    << tsc_per_ns() << " ticks/ns"sv;
    return 0;
}

void capture(safe::mail_t mail, config_t config, stream::session_t *channel_data) {
    auto shutdown_event = mail->event<bool>(mail::shutdown);
    auto packets = mail::man->queue<packet_t>(mail::video_packets);
    auto idr_events = mail->event<bool>(mail::idr);
    auto touch_port_event = mail->event<input::touch_port_t>(mail::touch_port);
    auto hdr_event = mail->event<hdr_info_t>(mail::hdr);

    // Avcap2 encodes at 30 or 60 fps. On 13.00 and 10.20 360p, 540p and 720p start as well (STATUS.md), so a client that
    // asks for less gets less to receive and decode.
    const bool hevc = config.videoFormat == 1;
    const bool low_rate = config.framerate > 0 && config.framerate <= 30;
    const auto mode =
        prospero::choose_capture_mode(config.height, ps_capture_large_modes_allowed(), ps_capture_small_modes_allowed());
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
    // Avcap2 reports no mastering metadata, and all zero means unknown. The stream itself says BT.2020 and PQ in its SPS.
    const bool hdr = hevc && config.dynamicRange > 0 && ps_capture_hdr_allowed();
    hdr_event->raise(std::make_unique<hdr_info_raw_t>(hdr));
    // The session falls back to 1080p when the system refuses a larger or smaller mode.
    const auto check_fallback = [&] {
        const auto delivered = prospero::capture_mode_of(ps_capture_video_resolution());
        if (delivered.height != delivered_height)
            BOOST_LOG(warning) << "PS5 capture: "sv << (delivered_height > delivered.height ? "larger"sv : "smaller"sv)
                               << " mode refused by the system, delivering "sv << delivered.width << 'x' << delivered.height;
        announce_mode(delivered);
    };

    // One encoder channel, one stream: a second session would read frames from the same Avcap2 ring and starve the
    // first. A new stream from the device that holds the capture ends the old one and takes over: a client that
    // retries, like Moonlight Web when its browser didn't get the picture, would otherwise wait until the old
    // session timed out. A new stream from another device is ended instead.
    static struct {
        std::mutex mutex;
        std::condition_variable released;
        bool streaming = false;
        std::string cert;
        safe::mail_raw_t::event_t<bool> shutdown;
    } owner;
    const std::string cert = channel_data ? stream::session::client_cert(*channel_data) : std::string{};
    {
        std::unique_lock lock(owner.mutex);
        if (owner.streaming && !cert.empty() && cert == owner.cert) {
            BOOST_LOG(info) << "PS5 capture: the same device started a new stream, ending its old one"sv;
            owner.shutdown->raise(true);
            owner.released.wait_for(lock, 5s, [] { return !owner.streaming; });
        }
        if (owner.streaming) {
            BOOST_LOG(warning) << "PS5 capture busy with another session; ending the new one"sv;
            shutdown_event->raise(true);
            return;
        }
        owner.streaming = true;
        owner.cert = cert;
        owner.shutdown = shutdown_event;
    }
    const auto release = util::fail_guard([] {
        {
            std::lock_guard lock(owner.mutex);
            owner.streaming = false;
            owner.shutdown.reset();
        }
        owner.released.notify_all();
    });

    // config.bitrate is in kbit/s. The channel opens with its bitrate in the config: raising it later above the
    // opening value stops the encoder (docs/re/hevc.md). Peak as Remote Play: H.264 average * 1.12, HEVC * 1.10.
    // Above 50 Mbit/s is measured on firmware 13.00 only (docs/re/hevc.md); elsewhere it takes a switch in the app.
    const uint32_t default_average = 6'000'000, max_average = ps_capture_high_bitrate_allowed() ? 150'000'000 : 50'000'000;
    const uint32_t requested = config.bitrate > 0 ? static_cast<uint32_t>(config.bitrate) * 1000u : default_average;
    const uint32_t average = std::min({requested, max_average, prospero::max_average_of(mode, low_rate)});
    ps_capture_set_opening_bitrate(average, average / 100 * (hevc ? 110 : 112));
    ps_capture_set_codec(hevc ? 1 : 0);
    ps_capture_set_ten_bit(hdr);
    ps_capture_set_video_mode(mode.resolution, low_rate ? 0 : 1);
    const auto codec = hevc ? prospero::Codec::hevc : prospero::Codec::h264;
    prospero::CaptureSession session(prospero::make_ps5_capture_backend(codec), codec);
    const int start_rc = session.start();
    update_offers();
    if (start_rc) {
        BOOST_LOG(error) << "PS5 capture failed to start: "sv << start_rc;
        shutdown_event->raise(true);
        return;
    }
    check_fallback();
    BOOST_LOG(info) << "PS5 capture "sv << (hdr ? "HEVC HDR"sv : hevc ? "HEVC"sv : "H.264"sv) << " bitrate "sv << ps_capture_opening_average() / 1000 << " kbit/s (requested "sv
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
