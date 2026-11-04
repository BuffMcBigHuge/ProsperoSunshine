// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's platf:: layer for the PS5: networking, game audio from Avcap2, the native virtual pad, keyboard
// and mouse, and placeholders for what is not forwarded (touch screen, pen).
#include "input/gamepad_map.hpp"
#include "input/keyboard_map.hpp"
#include "input/pad_touch.hpp"
#include "platform/ps5/kbm.h"
#include "platform/ps5/pad.h"
#include "platform/ps5/shared_capture.hpp"
#include "src/config.h"
#include "src/file_handler.h"
#include "src/logging.h"
#include "src/platform/common.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pthread_np.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <thread>
#include <boost/asio/ip/address.hpp>

using namespace std::literals;

extern "C" int sceNetGetMacAddress(uint8_t address[6], int flags);  // libSceNet, linked by default

namespace platf {

std::unique_ptr<deinit_t> init() { return std::make_unique<deinit_t>(); }

namespace publish {
// Upstream calls this from main.cpp, which the PS5 host replaces; mDNS runs from main_ps5.cpp
// (src/platform/ps5/mdns_responder.cpp, ROADMAP P9.2).
std::unique_ptr<deinit_t> start() { return nullptr; }
}

std::filesystem::path appdata() { return "/data/prosperosunshine"; }

std::string get_host_name() {
    char name[256] = {};
    if (gethostname(name, sizeof name - 1) == 0 && name[0]) return name;
    return "PS5";
}

std::string get_mac_address(const std::string_view &address) {
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return "00:00:00:00:00:00";
    std::string interface_name, mac = "00:00:00:00:00:00";
    for (auto *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || (a->ifa_addr->sa_family != AF_INET && a->ifa_addr->sa_family != AF_INET6)) continue;
        if (from_sockaddr(a->ifa_addr) == address) interface_name = a->ifa_name;
    }
    for (auto *a = list; a && !interface_name.empty(); a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_LINK || interface_name != a->ifa_name) continue;
        const auto *dl = reinterpret_cast<const sockaddr_dl *>(a->ifa_addr);
        if (dl->sdl_alen != 6) continue;
        const auto *b = reinterpret_cast<const unsigned char *>(LLADDR(dl));
        char buf[18];
        std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
        mac = buf;
    }
    freeifaddrs(list);
    // Without a match above, ask the network library.
    if (mac == "00:00:00:00:00:00") {
        uint8_t b[6] = {};
        if (sceNetGetMacAddress(b, 0) == 0) {
            char buf[18];
            std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
            mac = buf;
        }
    }
    return mac;
}

std::string from_sockaddr(const sockaddr *const addr) { return from_sockaddr_ex(addr).second; }

std::pair<std::uint16_t, std::string> from_sockaddr_ex(const sockaddr *const addr) {
    char text[INET6_ADDRSTRLEN] = {};
    std::uint16_t port = 0;
    if (addr->sa_family == AF_INET6) {
        const auto *in6 = reinterpret_cast<const sockaddr_in6 *>(addr);
        inet_ntop(AF_INET6, &in6->sin6_addr, text, sizeof text);
        port = ntohs(in6->sin6_port);
    } else if (addr->sa_family == AF_INET) {
        const auto *in = reinterpret_cast<const sockaddr_in *>(addr);
        inet_ntop(AF_INET, &in->sin_addr, text, sizeof text);
        port = ntohs(in->sin_port);
    }
    return {port, text};
}

void set_thread_name(std::string_view name) {
    const std::string copy(name.substr(0, 15));
    pthread_set_name_np(pthread_self(), copy.c_str());
}

void adjust_thread_priority(thread_priority_e) {}
void streaming_will_start() {}
void streaming_will_stop() {}
void open_url(const std::string &) {}

std::unique_ptr<deinit_t> enable_socket_qos(uintptr_t, boost::asio::ip::address &, uint16_t, qos_data_type_e, bool) {
    return nullptr;
}

namespace {
socklen_t to_sockaddr(const boost::asio::ip::address &address, uint16_t port, sockaddr_storage &out) {
    std::memset(&out, 0, sizeof out);
    if (address.is_v6()) {
        auto *in6 = reinterpret_cast<sockaddr_in6 *>(&out);
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(port);
        const auto bytes = address.to_v6().to_bytes();
        std::memcpy(&in6->sin6_addr, bytes.data(), bytes.size());
        return sizeof(sockaddr_in6);
    }
    auto *in = reinterpret_cast<sockaddr_in *>(&out);
    in->sin_family = AF_INET;
    in->sin_port = htons(port);
    const auto bytes = address.to_v4().to_bytes();
    std::memcpy(&in->sin_addr, bytes.data(), bytes.size());
    return sizeof(sockaddr_in);
}

bool send_one(int fd, const sockaddr_storage &to, socklen_t to_len, const char *header, size_t header_size,
              const char *payload, size_t payload_size) {
    iovec iov[2];
    int count = 0;
    if (header_size) iov[count++] = {const_cast<char *>(header), header_size};
    iov[count++] = {const_cast<char *>(payload), payload_size};
    msghdr msg{};
    msg.msg_name = const_cast<sockaddr_storage *>(&to);
    msg.msg_namelen = to_len;
    msg.msg_iov = iov;
    msg.msg_iovlen = count;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (sendmsg(fd, &msg, 0) >= 0) return true;
        // A full send buffer on a nonblocking socket: give the NIC a moment instead of dropping.
        if (errno != EAGAIN && errno != ENOBUFS) return false;
        std::this_thread::sleep_for(100us);
    }
    return false;
}
}

bool send_batch(const batched_send_info_t &info) {
    sockaddr_storage to;
    const socklen_t to_len = to_sockaddr(info.target_address, info.target_port, to);
    for (size_t i = 0; i < info.block_count; ++i) {
        const auto block = info.buffer_for_payload_offset((info.block_offset + i) * info.payload_size);
        const size_t size = std::min(info.payload_size, block.size);
        if (!send_one(static_cast<int>(info.native_socket), to, to_len, info.headers ? info.headers + (info.block_offset + i) * info.header_size : nullptr,
                      info.headers ? info.header_size : 0, block.buffer, size))
            return false;
    }
    return true;
}

bool send(send_info_t &info) {
    sockaddr_storage to;
    const socklen_t to_len = to_sockaddr(info.target_address, info.target_port, to);
    return send_one(static_cast<int>(info.native_socket), to, to_len, info.header, info.header_size, info.payload,
                    info.payload_size);
}

namespace {
class sleep_timer final : public high_precision_timer {
public:
    void sleep_for(const std::chrono::nanoseconds &duration) override { std::this_thread::sleep_for(duration); }
    explicit operator bool() override { return true; }
};
}

std::unique_ptr<high_precision_timer> create_high_precision_timer() { return std::make_unique<sleep_timer>(); }

// Silent microphone: keeps the audio stream alive with correctly paced zero frames.
namespace {
class silent_mic final : public mic_t {
public:
    silent_mic(int channels, std::uint32_t rate, std::uint32_t frame_size)
        : samples_(static_cast<size_t>(channels) * frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / rate)),
          next_(std::chrono::steady_clock::now()) {}
    capture_e sample(std::vector<float> &frame_buffer) override {
        next_ += period_;
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(samples_, 0.0f);
        return capture_e::ok;
    }

private:
    size_t samples_;
    std::chrono::nanoseconds period_;
    std::chrono::steady_clock::time_point next_;
};

// Game audio from Avcap2 as raw PCM: float32, stereo, 48 kHz, 1024 frames per packet (R2). Sunshine's
// own Opus encoder then cuts the frames Moonlight negotiated (5 ms by default).
//
// When the capture session is lost (a game start takes the resource), replaced by a reopen, or not
// there yet as the stream starts, the audio channel is gone; the microphone then delivers paced
// silence and reopens its channel on the current session every half second, so the stream itself
// never ends because of audio.
//
// The console has one audio channel. With several streams (a second player), the microphone that opened it reads it
// and hands every packet to all microphones; the others only wait for theirs. When the reading one goes, the next to
// notice opens the channel anew.
class avcap_mic;
struct audio_hub {
    std::mutex mutex;
    std::condition_variable arrived;
    std::vector<avcap_mic *> mics;
    avcap_mic *reader = nullptr;
};

class avcap_mic final : public mic_t {
public:
    static std::unique_ptr<mic_t> open(int channels, std::uint32_t sample_rate, std::uint32_t frame_size) {
        int rc = 0;
        auto shared = prospero::SharedCapture::acquire(rc);
        if (!shared) BOOST_LOG(warning) << "PS5 audio: capture session unavailable: "sv << rc << ", retrying"sv;
        auto mic = std::unique_ptr<avcap_mic>(new avcap_mic(std::move(shared), channels, sample_rate, frame_size));
        // A title started with the stream can hold the capture for a moment. Returning no microphone here left the
        // whole stream silent: Sunshine only asks for a new one after a reinit, which the silent fallback never sends.
        if (mic->another_reads()) BOOST_LOG(info) << "PS5 audio: sharing the channel another stream reads"sv;
        else if (!mic->shared_ || !mic->open_channel()) mic->retry_at_ = std::chrono::steady_clock::now() + 500ms;
        return mic;
    }

    ~avcap_mic() override {
        if (open_) close_channel();
        std::lock_guard lock(hub_.mutex);
        std::erase(hub_.mics, this);
    }

    capture_e sample(std::vector<float> &frame_buffer) override {
        const size_t needed = static_cast<size_t>(frame_size_) * 2;
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            bool follower = false;
            {
                std::unique_lock lock(hub_.mutex);
                follower = hub_.reader && hub_.reader != this;
                if (follower) hub_.arrived.wait_for(lock, 100ms, [&] { return pending_.size() >= needed; });
                if (pending_.size() >= needed) return take(frame_buffer, needed);
            }
            // A reader that sends nothing for a while is treated like a lost channel: silence, and a new try later.
            if (follower) {
                if (std::chrono::steady_clock::now() - start > 500ms) return silence(frame_buffer);
                continue;
            }
            if (open_ && generation_ != shared_->generation()) set_open(false);  // session was reopened
            if (!open_) return silence(frame_buffer);
            const int rc = shared_->with_session(
                [&](ps_capture *s) { return ps_capture_audio_read(s, packet_.data(), packet_.size(), header_); });
            if (rc < 0) {
                BOOST_LOG(warning) << "PS5 audio: ReadAudio failed: "sv << rc << ", reopening"sv;
                close_channel();
                return silence(frame_buffer);
            }
            if (rc == 0) {
                // A stalled channel (no packet for a while) is treated like a lost one.
                if (std::chrono::steady_clock::now() - start > 500ms) {
                    close_channel();
                    return silence(frame_buffer);
                }
                std::this_thread::sleep_for(1ms);
                continue;
            }
            hand_out(reinterpret_cast<const float *>(packet_.data()), static_cast<size_t>(rc) / sizeof(float));
        }
    }

private:
    avcap_mic(std::shared_ptr<prospero::SharedCapture> shared, int channels, std::uint32_t sample_rate,
              std::uint32_t frame_size)
        : shared_(std::move(shared)), channels_(channels), frame_size_(frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / sample_rate)), packet_(0xC000) {
        std::lock_guard lock(hub_.mutex);
        hub_.mics.push_back(this);
    }

    bool another_reads() {
        std::lock_guard lock(hub_.mutex);
        return hub_.reader && hub_.reader != this;
    }

    void set_open(bool open) {
        open_ = open;
        std::lock_guard lock(hub_.mutex);
        if (open) {
            hub_.reader = this;
            pending_.clear();
        } else if (hub_.reader == this) {
            hub_.reader = nullptr;
            hub_.arrived.notify_all();
        }
    }

    // Every microphone gets the packet. One that falls behind keeps the last 200 ms, so its stream doesn't lag.
    void hand_out(const float *samples, size_t count) {
        constexpr size_t most = 48000 * 2 / 5;
        std::lock_guard lock(hub_.mutex);
        for (avcap_mic *mic : hub_.mics) {
            mic->pending_.insert(mic->pending_.end(), samples, samples + count);
            if (mic->pending_.size() > most)
                mic->pending_.erase(mic->pending_.begin(),
                                    mic->pending_.begin() + static_cast<std::ptrdiff_t>(mic->pending_.size() - most));
        }
        hub_.arrived.notify_all();
    }

    // Caller holds hub_.mutex.
    capture_e take(std::vector<float> &frame_buffer, size_t needed) {
        next_ = std::chrono::steady_clock::now();
        // Stereo from the console; any further requested channels stay silent.
        frame_buffer.assign(static_cast<size_t>(frame_size_) * channels_, 0.0f);
        for (std::uint32_t i = 0; i < frame_size_; ++i)
            for (int c = 0; c < std::min(channels_, 2); ++c)
                frame_buffer[i * channels_ + c] = pending_[i * 2 + c];
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(needed));
        return capture_e::ok;
    }

    bool open_channel() {
        // The generation is read under the lock that opens the channel. Read before it, a reopen in between (a game
        // starting) left the channel on the new session marked with the old generation: the next sample() then dropped
        // it without closing it, and every later open failed on the channel already there.
        unsigned generation = 0;
        const int rc = shared_->exclusive([&](ps_capture *s) {
            generation = shared_->generation();
            return ps_capture_audio_open(s, 0, 0, 0);
        });
        if (rc) {
            BOOST_LOG(warning) << "PS5 audio: OpenAudio failed: "sv << rc;
            return false;
        }
        generation_ = generation;
        set_open(true);
        return true;
    }

    void close_channel() {
        // After a reopen the channel died with the old session; closing would hit the new one. Checked under the lock,
        // so a reopen can't slip in between.
        shared_->exclusive([&](ps_capture *s) { return generation_ == shared_->generation() ? ps_capture_audio_close(s) : 0; });
        set_open(false);
    }

    capture_e silence(std::vector<float> &frame_buffer) {
        if (std::chrono::steady_clock::now() >= retry_at_ && !another_reads()) {
            // Only the session video opened: if video failed too, the stream is ending, and opening one here would hold
            // up its teardown.
            if (!shared_) shared_ = prospero::SharedCapture::attach();
            if (shared_ && open_channel()) BOOST_LOG(info) << "PS5 audio: channel reopened"sv;
            retry_at_ = std::chrono::steady_clock::now() + 500ms;
        }
        {
            // What arrives during the silence would only play late.
            std::lock_guard lock(hub_.mutex);
            pending_.clear();
        }
        next_ = std::max(next_ + period_, std::chrono::steady_clock::now() - period_);
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(static_cast<size_t>(frame_size_) * channels_, 0.0f);
        return capture_e::ok;
    }

    static inline audio_hub hub_;
    std::shared_ptr<prospero::SharedCapture> shared_;
    int channels_;
    std::uint32_t frame_size_;
    std::chrono::nanoseconds period_;
    std::vector<uint8_t> packet_;
    uint8_t header_[0x10] = {};
    std::vector<float> pending_;  // guarded by hub_.mutex
    bool open_ = false;
    unsigned generation_ = 0;
    std::chrono::steady_clock::time_point next_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point retry_at_{};
};

class silent_audio_control final : public audio_control_t {
public:
    int set_sink(const std::string &) override { return 0; }
    std::unique_ptr<mic_t> microphone(const std::uint8_t *, int channels, std::uint32_t sample_rate,
                                      std::uint32_t frame_size, bool, bool) override {
        if (sample_rate == 48000) {
            if (auto mic = avcap_mic::open(channels, sample_rate, frame_size)) {
                BOOST_LOG(info) << "PS5 audio: game audio via Avcap2, "sv << channels << " channels, "sv << frame_size
                                << " frames per packet"sv;
                return mic;
            }
        }
        BOOST_LOG(warning) << "PS5 audio: sending silence (rate "sv << sample_rate << ')';
        return std::make_unique<silent_mic>(channels, sample_rate, frame_size);
    }
    bool is_sink_available(const std::string &) override { return true; }
    std::optional<sink_t> sink_info() override {
        sink_t sink;
        sink.host = "ps5";
        return sink;
    }
};
}

std::unique_ptr<audio_control_t> audio_control() { return std::make_unique<silent_audio_control>(); }

// Input: one native virtual DualSense per Moonlight gamepad slot, and one keyboard and one mouse shared by all clients.
struct input_raw_t {
    std::mutex mutex;
    std::array<ps_pad *, MAX_GAMEPADS> pads{};
    std::array<bool, MAX_GAMEPADS> active{};  // last state had something pressed or deflected
    std::array<bool, MAX_GAMEPADS> back{};    // Select is down
    std::array<bool, MAX_GAMEPADS> ps_held{}; // a PS press that was held back, logged once per press
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> back_since{};
    std::array<feedback_queue_t, MAX_GAMEPADS> feedback{};
    std::array<uint8_t, MAX_GAMEPADS> client_index{};
    std::array<std::array<uint8_t, 2>, MAX_GAMEPADS> motors{};
    std::array<std::array<uint8_t, 3>, MAX_GAMEPADS> colors{};
    std::array<prospero::TriggerBlock, MAX_GAMEPADS> triggers{};
    std::array<prospero::PadTouch, MAX_GAMEPADS> touch{};
    // Added on a client's first key or mouse event, removed when no session is left (client_session).
    ps_keyboard *keyboard = nullptr;
    ps_mouse *mouse = nullptr;
    bool keyboard_failed = false, mouse_failed = false;
    prospero::KeyboardState keys;
    uint8_t mouse_buttons = 0;
    prospero::WheelSteps wheel, tilt;
    std::chrono::steady_clock::time_point key_down_at, button_down_at;
    std::optional<std::pair<float, float>> pointer;  // the last absolute position, for abs_mouse
    int sessions = 0;
    uint32_t user = 0;
    bool ps_all_players = false;  // ps_button_all_players: every pad may press PS, not only pad 0
    std::jthread output_thread;
};

static void set_motors(input_raw_t &input, int nr, uint8_t large, uint8_t small) {
    if (input.motors[nr] == std::array<uint8_t, 2>{large, small}) return;
    input.motors[nr] = {large, small};
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " motors "sv << int(large) << ',' << int(small);
    if (input.feedback[nr]) input.feedback[nr]->raise(gamepad_feedback_msg_t::make_rumble(input.client_index[nr], large * 257, small * 257));
}

static void set_color(input_raw_t &input, int nr, uint8_t r, uint8_t g, uint8_t b) {
    if (input.colors[nr] == std::array<uint8_t, 3>{r, g, b}) return;
    input.colors[nr] = {r, g, b};
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " light bar "sv << int(r) << ',' << int(g) << ',' << int(b);
    if (input.feedback[nr]) input.feedback[nr]->raise(gamepad_feedback_msg_t::make_rgb_led(input.client_index[nr], r, g, b));
}

static void set_triggers(input_raw_t &input, int nr, const prospero::TriggerBlock &block) {
    if (input.triggers[nr] == block) return;
    const auto effects = prospero::map_trigger_effects(block, input.triggers[nr]);
    input.triggers[nr] = block;
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (const uint8_t byte : block) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " triggers "sv << hex;
    if (effects.flags && input.feedback[nr]) {
        input.feedback[nr]->raise(gamepad_feedback_msg_t::make_adaptive_triggers(input.client_index[nr], effects.flags, effects.type_left, effects.type_right, effects.left, effects.right));
    }
}

// Reads what the system wants from each pad at the cadence of Remote Play's own reader and passes on
// changes of the motors, the light bar and the trigger effects. Only titles using the classic two-motor rumble set the motors;
// see docs/re/pad-output.md.
static void poll_pad_output(std::stop_token stop, input_raw_t *input) {
    std::array<bool, MAX_GAMEPADS> failed{};
    while (!stop.stop_requested()) {
        {
            std::lock_guard lock(input->mutex);
            for (int nr = 0; nr < MAX_GAMEPADS; ++nr) {
                if (!input->pads[nr]) {
                    failed[nr] = false;
                    continue;
                }
                ps_pad_output output{};
                if (const int rc = ps_pad_read_output(input->pads[nr], &output); rc) {
                    if (!failed[nr]) BOOST_LOG(warning) << "PS5 virtual pad "sv << nr << " output read failed: "sv << rc;
                    failed[nr] = true;
                    continue;
                }
                set_motors(*input, nr, output.motors[0], output.motors[1]);
                set_color(*input, nr, output.color[0], output.color[1], output.color[2]);
                prospero::TriggerBlock triggers;
                std::memcpy(triggers.data(), output.triggers, sizeof output.triggers);
                set_triggers(*input, nr, triggers);
            }
        }
        std::this_thread::sleep_for(10ms);
    }
}

static void close_keyboard_mouse(input_raw_t &input) {
    if (input.keyboard) {
        const int rc = ps_keyboard_close(input.keyboard);
        BOOST_LOG(info) << "PS5 virtual keyboard detached ("sv << rc << ')';
    }
    if (input.mouse) {
        const int rc = ps_mouse_close(input.mouse);
        BOOST_LOG(info) << "PS5 virtual mouse detached ("sv << rc << ')';
    }
    input.keyboard = nullptr;
    input.mouse = nullptr;
    input.keyboard_failed = input.mouse_failed = false;
    input.keys.clear();
    input.mouse_buttons = 0;
    input.wheel.clear();
    input.tilt.clear();
    input.pointer.reset();
}

void freeInput(input_raw_t *input) {
    if (!input) return;
    input->output_thread.request_stop();
    if (input->output_thread.joinable()) input->output_thread.join();
    for (auto *&pad : input->pads) {
        ps_pad_close(pad);
        pad = nullptr;
    }
    close_keyboard_mouse(*input);
    delete input;
}

input_t input() {
    auto *raw = new input_raw_t;
    raw->user = ps_pad_default_user();
    // Not the user's id: this log goes into the report people post (app/src/psapp/diag.cpp).
    BOOST_LOG(info) << (raw->user ? "PS5 input: virtual pads ready"sv : "PS5 input: no user for virtual pads"sv);
    // The app's "PS for all players" (off by default). Sunshine doesn't know the key and leaves it alone.
    const auto settings = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
    const auto all = settings.find("ps_button_all_players");
    raw->ps_all_players = all != settings.end() && all->second == "1";
    BOOST_LOG(info) << (raw->ps_all_players ? "PS5 input: every player can press PS"sv
                                            : "PS5 input: only player 1 can press PS"sv);
    raw->output_thread = std::jthread(poll_pad_output, raw);
    return input_t{raw};
}

// A client only sends the sensors it is asked for, and a new client has to be asked anew. One without sensors ignores this.
static void request_motion(const input_raw_t &input, int nr) {
    if (!input.feedback[nr]) return;
    for (const uint8_t type : {prospero::moonlight::motion_accel, prospero::moonlight::motion_gyro}) {
        input.feedback[nr]->raise(gamepad_feedback_msg_t::make_motion_event_state(input.client_index[nr], type, 100));
    }
}

int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    auto *&pad = input->pads[id.globalIndex];
    if (!pad) {
        // The first pad plays as the console's user. Every further one, from a second client or a second controller on
        // the same client, is another player: the system asks who uses it, and they pick a user or a guest.
        const bool first = id.globalIndex == 0;
        const int rc = ps_pad_open(first ? input->user : PS_PAD_NO_USER, &pad);
        if (rc) {
            BOOST_LOG(error) << "PS5 virtual pad "sv << id.globalIndex << " failed: "sv << rc;
            pad = nullptr;
            return -1;
        }
        BOOST_LOG(info) << "PS5 virtual pad "sv << id.globalIndex << " attached"sv
                        << (first ? ""sv : ", the console asks who uses it"sv);
    }
    request_motion(*input, id.globalIndex);
    return 0;
}

int rebind_gamepad(input_t &input, const gamepad_id_t &id, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    request_motion(*input, id.globalIndex);
    return 0;
}

void free_gamepad(input_t &input, int nr) {
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    set_motors(*input, nr, 0, 0);
    set_triggers(*input, nr, prospero::triggers_off);
    const int rc = ps_pad_close(input->pads[nr]);  // sends a neutral state before removal
    input->pads[nr] = nullptr;
    input->colors[nr] = {};  // a later pad in this slot reports its colour anew
    input->triggers[nr] = {};
    input->touch[nr].clear();
    input->back[nr] = false;
    input->ps_held[nr] = false;
    BOOST_LOG(info) << "PS5 virtual pad "sv << nr << " detached ("sv << rc << ')';
}

void gamepad_update(input_t &input, int nr, const gamepad_state_t &state) {
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    const auto send = [&](uint32_t flags) {
        const auto mapped = prospero::map_gamepad(flags, state.lt, state.rt, state.lsX, state.lsY, state.rsX, state.rsY);
        const ps_pad_state pad{mapped.buttons, mapped.lx, mapped.ly, mapped.rx, mapped.ry, mapped.l2, mapped.r2};
        ps_pad_send(input->pads[nr], &pad);
    };
    // Sunshine turns a held Select into PS (back_button_timeout), but Select is Create here and pressing it
    // first would open the Create menu. So Create only goes out, as a tap, when Select comes up early. The
    // margin keeps Sunshine's own forced release at the timeout from counting as early.
    uint32_t flags = prospero::allowed_buttons(state.buttonFlags, nr, input->ps_all_players);
    if ((state.buttonFlags & ~flags) && !(input->ps_held[nr]))
        BOOST_LOG(info) << "PS5 virtual pad "sv << nr << ": PS ignored, only player 1 has it (ps_button_all_players)"sv;
    input->ps_held[nr] = state.buttonFlags & ~flags;
    bool create_tap = false;
    if (const auto timeout = config::input.back_button_timeout; timeout >= 0ms) {
        const bool back = flags & BACK;
        const auto now = std::chrono::steady_clock::now();
        if (back && !input->back[nr]) input->back_since[nr] = now;
        create_tap = !back && input->back[nr] && now - input->back_since[nr] < timeout - 20ms;
        input->back[nr] = back;
        flags &= ~BACK;
    }
    send(flags);
    if (create_tap) {
        send(flags | BACK);
        std::this_thread::sleep_for(50ms);
        send(flags);
    }
    // Sunshine neutralises held input with an all-zero state when a session ends (P4.3).
    const bool active = state.buttonFlags || state.lt || state.rt || state.lsX || state.lsY || state.rsX || state.rsY;
    if (input->active[nr] && !active) BOOST_LOG(debug) << "PS5 virtual pad "sv << nr << " released to neutral"sv;
    input->active[nr] = active;
}

void gamepad_touch(input_t &input, const gamepad_touch_t &touch) {
    const int nr = touch.id.globalIndex;
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    auto &fingers = input->touch[nr];
    // Sunshine sends this when a session ends and the pad stays. A last motion sample left in place would keep turning a
    // game's camera; a client sending it mid-stream only loses the 10 ms to its next sample.
    if (touch.eventType == prospero::moonlight::touch_cancel_all) {
        fingers.clear();
        ps_pad_rest(input->pads[nr]);
        return;
    }
    if (!fingers.update(touch.eventType, touch.pointerId, touch.x, touch.y)) return;
    std::array<ps_pad_touch, 2> points{};
    for (int i = 0; i < fingers.count(); i++) {
        const auto &point = fingers.points()[i];
        points[i] = {point.id, point.x, point.y};
    }
    ps_pad_set_touch(input->pads[nr], fingers.count(), points.data());
}

void gamepad_motion(input_t &input, const gamepad_motion_t &motion) {
    const int nr = motion.id.globalIndex;
    if (nr < 0 || nr >= MAX_GAMEPADS) return;
    std::lock_guard lock(input->mutex);
    if (!input->pads[nr]) return;
    if (motion.motionType == prospero::moonlight::motion_accel) {
        const float xyz[3] = {prospero::accel_to_g(motion.x), prospero::accel_to_g(motion.y), prospero::accel_to_g(motion.z)};
        ps_pad_set_acceleration(input->pads[nr], xyz);
    } else if (motion.motionType == prospero::moonlight::motion_gyro) {
        const float xyz[3] = {prospero::gyro_to_rad(motion.x), prospero::gyro_to_rad(motion.y), prospero::gyro_to_rad(motion.z)};
        ps_pad_set_angular_velocity(input->pads[nr], xyz);
    }
}

std::vector<supported_gamepad_t> &supported_gamepads(input_t *) {
    static std::vector<supported_gamepad_t> pads{{"ds5", true, ""}};
    return pads;
}

platform_caps::caps_t get_capabilities() { return platform_caps::controller_touch; }

// Sunshine keeps one of these per stream session, and keeps it for a reconnect like its pads. The keyboard and mouse
// go when the last one does, so the console doesn't list them (nor games show their prompts) once nobody streams.
class client_session: public client_input_t {
public:
    explicit client_session(input_raw_t *input): input_(input) {
        std::lock_guard lock(input_->mutex);
        ++input_->sessions;
    }

    ~client_session() override {
        std::lock_guard lock(input_->mutex);
        if (--input_->sessions == 0) close_keyboard_mouse(*input_);
    }

private:
    input_raw_t *input_;
};

std::unique_ptr<client_input_t> allocate_client_input_context(input_t &input) {
    return std::make_unique<client_session>(input.get());
}

// Each device is added on first use; one that fails is not tried again until the sessions end. The system drops what
// a device reports right after it was added (a key at 0 ms was lost, at 50 ms taken, kbm_probe on 13.00), so the
// first report waits a little, once per session.
constexpr auto device_settle = 150ms;

// A key or button let go within a few ms of going down is not seen: the first key after the settle wait above went
// down and up 1 ms apart and was lost. So a release waits until its press has been in effect this long.
constexpr auto min_hold = 30ms;

static void hold_from(std::chrono::steady_clock::time_point down) { std::this_thread::sleep_until(down + min_hold); }

static ps_keyboard *keyboard_of(input_raw_t &input) {
    if (!input.keyboard && !input.keyboard_failed) {
        if (const int rc = ps_keyboard_open(input.user, &input.keyboard); rc) {
            BOOST_LOG(error) << "PS5 virtual keyboard failed: "sv << rc;
            input.keyboard_failed = true;
        } else {
            BOOST_LOG(info) << "PS5 virtual keyboard attached"sv;
            std::this_thread::sleep_for(device_settle);
        }
    }
    return input.keyboard;
}

static ps_mouse *mouse_of(input_raw_t &input) {
    if (!input.mouse && !input.mouse_failed) {
        if (const int rc = ps_mouse_open(input.user, &input.mouse); rc) {
            BOOST_LOG(error) << "PS5 virtual mouse failed: "sv << rc;
            input.mouse_failed = true;
        } else {
            BOOST_LOG(info) << "PS5 virtual mouse attached"sv;
            std::this_thread::sleep_for(device_settle);
        }
    }
    return input.mouse;
}

static void send_mouse(input_raw_t &input, int dx, int dy, int8_t wheel = 0, int8_t tilt = 0) {
    ps_mouse *mouse = mouse_of(input);
    if (!mouse) return;
    // A large jump goes out in several reports rather than wrapping around.
    do {
        const auto x = static_cast<int16_t>(std::clamp(dx, -32767, 32767));
        const auto y = static_cast<int16_t>(std::clamp(dy, -32767, 32767));
        ps_mouse_send(mouse, x, y, input.mouse_buttons, wheel, tilt);
        dx -= x;
        dy -= y;
        wheel = tilt = 0;
    } while (dx || dy);
}

void move_mouse(input_t &input, int deltaX, int deltaY) {
    std::lock_guard lock(input->mutex);
    input->pointer.reset();
    // Sunshine nudges the pointer by a pixel and back at every stream start; that alone does not add a mouse.
    if (!input->mouse && std::abs(deltaX) <= 1 && std::abs(deltaY) <= 1) return;
    send_mouse(*input, deltaX, deltaY);
}

void move_mouse(input_t &input, const touch_port_t &, int deltaX, int deltaY) { move_mouse(input, deltaX, deltaY); }

// The console's pointer is relative only, so a client in absolute mode moves it by the difference to its last position.
void abs_mouse(input_t &input, const touch_port_t &, float x, float y) {
    std::lock_guard lock(input->mutex);
    const auto last = std::exchange(input->pointer, std::pair{x, y});
    if (!last) return;
    send_mouse(*input, static_cast<int>(std::lround(x - last->first)), static_cast<int>(std::lround(y - last->second)));
}

void button_mouse(input_t &input, int button, bool release) {
    const uint8_t bit = prospero::mouse_button_bit(button);
    if (!bit) return;
    std::lock_guard lock(input->mutex);
    const uint8_t buttons = release ? input->mouse_buttons & ~bit : input->mouse_buttons | bit;
    if (buttons == input->mouse_buttons) return;
    input->mouse_buttons = buttons;
    if (release) hold_from(input->button_down_at);
    send_mouse(*input, 0, 0);
    if (!release) input->button_down_at = std::chrono::steady_clock::now();
}

void scroll(input_t &input, int distance) {
    std::lock_guard lock(input->mutex);
    if (const int8_t steps = input->wheel.add(distance)) send_mouse(*input, 0, 0, steps, 0);
}

void hscroll(input_t &input, int distance) {
    std::lock_guard lock(input->mutex);
    if (const int8_t steps = input->tilt.add(distance)) send_mouse(*input, 0, 0, 0, steps);
}

void keyboard_update(input_t &input, uint16_t modcode, bool release, uint8_t, bool) {
    std::lock_guard lock(input->mutex);
    if (!input->keys.update(modcode, release)) return;
    ps_keyboard *keyboard = keyboard_of(*input);
    if (!keyboard) return;
    if (release) hold_from(input->key_down_at);
    ps_keyboard_send(keyboard, input->keys.modifiers(), input->keys.usages(), input->keys.count());
    if (!release) input->key_down_at = std::chrono::steady_clock::now();
}

// Text from a client's on-screen keyboard: printable ASCII is typed key by key as on a US keyboard, and what the
// client holds stays held. Other characters have no key and are dropped.
void unicode(input_t &input, const char *utf8, int size) {
    std::lock_guard lock(input->mutex);
    ps_keyboard *keyboard = keyboard_of(*input);
    if (!keyboard) return;
    const auto &held = input->keys;
    int dropped = 0;
    for (int i = 0; i < size; i++) {
        const auto key = prospero::ascii_key(utf8[i]);
        if (!key.usage) {
            dropped += (static_cast<uint8_t>(utf8[i]) & 0xC0) != 0x80;  // a UTF-8 sequence counts once
            continue;
        }
        std::array<uint8_t, prospero::KeyboardState::max_keys> usages{};
        const auto count = std::min<uint8_t>(held.count(), prospero::KeyboardState::max_keys - 1);
        std::copy_n(held.usages(), count, usages.begin());
        usages[count] = key.usage;
        const uint8_t modifiers = held.modifiers() | (key.shift ? 0x02 : 0);
        ps_keyboard_send(keyboard, modifiers, usages.data(), count + 1);
        std::this_thread::sleep_for(min_hold);
        ps_keyboard_send(keyboard, held.modifiers(), held.usages(), held.count());
        std::this_thread::sleep_for(min_hold);
    }
    if (dropped) BOOST_LOG(debug) << "PS5 virtual keyboard: "sv << dropped << " characters without a key"sv;
}

void touch_update(client_input_t *, const touch_port_t &, const touch_input_t &) {}
void pen_update(client_input_t *, const touch_port_t &, const pen_input_t &) {}
void gamepad_battery(input_t &, const gamepad_battery_t &) {}
}
