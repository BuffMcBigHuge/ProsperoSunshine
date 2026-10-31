// SPDX-License-Identifier: GPL-3.0-only
// Sunshine's platf:: layer for the PS5: networking, game audio from Avcap2, the native virtual pad
// and placeholders for what v1.0 does not forward (mouse, keyboard, touch, pen).
#include "input/gamepad_map.hpp"
#include "platform/ps5/pad.h"
#include "platform/ps5/shared_capture.hpp"
#include "src/config.h"
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
#include <array>
#include <cstring>
#include <mutex>
#include <string>
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
    // Without a match above, for example on an address the client reached through a proxy, ask the network library.
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
// When the capture session is lost (a game start takes the resource) or replaced by a reopen, the
// audio channel is gone; the microphone then delivers paced silence and reopens its channel on the
// current session every half second, so the stream itself never ends because of audio.
class avcap_mic final : public mic_t {
public:
    static std::unique_ptr<mic_t> open(int channels, std::uint32_t sample_rate, std::uint32_t frame_size) {
        int rc = 0;
        auto shared = prospero::SharedCapture::acquire(rc);
        if (!shared) {
            BOOST_LOG(error) << "PS5 audio: capture session unavailable: "sv << rc;
            return nullptr;
        }
        auto mic = std::unique_ptr<avcap_mic>(new avcap_mic(std::move(shared), channels, sample_rate, frame_size));
        if (!mic->open_channel()) return nullptr;
        return mic;
    }

    ~avcap_mic() override {
        if (open_) close_channel();
    }

    capture_e sample(std::vector<float> &frame_buffer) override {
        const size_t needed = static_cast<size_t>(frame_size_) * 2;
        const auto start = std::chrono::steady_clock::now();
        while (pending_.size() < needed) {
            if (open_ && generation_ != shared_->generation()) open_ = false;  // session was reopened
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
            const auto *samples = reinterpret_cast<const float *>(packet_.data());
            pending_.insert(pending_.end(), samples, samples + rc / sizeof(float));
        }
        next_ = std::chrono::steady_clock::now();
        // Stereo from the console; any further requested channels stay silent.
        frame_buffer.assign(static_cast<size_t>(frame_size_) * channels_, 0.0f);
        for (std::uint32_t i = 0; i < frame_size_; ++i)
            for (int c = 0; c < std::min(channels_, 2); ++c)
                frame_buffer[i * channels_ + c] = pending_[i * 2 + c];
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(needed));
        return capture_e::ok;
    }

private:
    avcap_mic(std::shared_ptr<prospero::SharedCapture> shared, int channels, std::uint32_t sample_rate,
              std::uint32_t frame_size)
        : shared_(std::move(shared)), channels_(channels), frame_size_(frame_size),
          period_(std::chrono::nanoseconds(1'000'000'000LL * frame_size / sample_rate)), packet_(0xC000) {}

    bool open_channel() {
        const unsigned generation = shared_->generation();
        const int rc = shared_->exclusive([](ps_capture *s) { return ps_capture_audio_open(s, 0, 0, 0); });
        if (rc) {
            BOOST_LOG(warning) << "PS5 audio: OpenAudio failed: "sv << rc;
            return false;
        }
        open_ = true;
        generation_ = generation;
        pending_.clear();
        return true;
    }

    void close_channel() {
        // After a reopen the channel died with the old session; closing would hit the new one.
        if (generation_ == shared_->generation())
            shared_->exclusive([](ps_capture *s) { return ps_capture_audio_close(s); });
        open_ = false;
    }

    capture_e silence(std::vector<float> &frame_buffer) {
        if (std::chrono::steady_clock::now() >= retry_at_) {
            retry_at_ = std::chrono::steady_clock::now() + 500ms;
            if (open_channel()) BOOST_LOG(info) << "PS5 audio: channel reopened"sv;
        }
        next_ = std::max(next_ + period_, std::chrono::steady_clock::now() - period_);
        std::this_thread::sleep_until(next_);
        frame_buffer.assign(static_cast<size_t>(frame_size_) * channels_, 0.0f);
        return capture_e::ok;
    }

    std::shared_ptr<prospero::SharedCapture> shared_;
    int channels_;
    std::uint32_t frame_size_;
    std::chrono::nanoseconds period_;
    std::vector<uint8_t> packet_;
    uint8_t header_[0x10] = {};
    std::vector<float> pending_;
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

// Input: one native virtual DualSense per Moonlight gamepad slot.
struct input_raw_t {
    std::mutex mutex;
    std::array<ps_pad *, MAX_GAMEPADS> pads{};
    std::array<bool, MAX_GAMEPADS> active{};  // last state had something pressed or deflected
    std::array<bool, MAX_GAMEPADS> back{};    // Select is down
    std::array<std::chrono::steady_clock::time_point, MAX_GAMEPADS> back_since{};
    std::array<feedback_queue_t, MAX_GAMEPADS> feedback{};
    std::array<uint8_t, MAX_GAMEPADS> client_index{};
    std::array<std::array<uint8_t, 2>, MAX_GAMEPADS> motors{};
    std::array<std::array<uint8_t, 3>, MAX_GAMEPADS> colors{};
    std::array<prospero::TriggerBlock, MAX_GAMEPADS> triggers{};
    uint32_t user = 0;
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

void freeInput(input_raw_t *input) {
    if (!input) return;
    input->output_thread.request_stop();
    if (input->output_thread.joinable()) input->output_thread.join();
    for (auto *&pad : input->pads) {
        ps_pad_close(pad);
        pad = nullptr;
    }
    delete input;
}

input_t input() {
    auto *raw = new input_raw_t;
    raw->user = ps_pad_default_user();
    BOOST_LOG(info) << "PS5 input: virtual pads for user "sv << std::hex << raw->user << std::dec;
    raw->output_thread = std::jthread(poll_pad_output, raw);
    return input_t{raw};
}

int alloc_gamepad(input_t &input, const gamepad_id_t &id, const gamepad_arrival_t &, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
    auto *&pad = input->pads[id.globalIndex];
    if (pad) return 0;
    const int rc = ps_pad_open(input->user, &pad);
    if (rc) {
        BOOST_LOG(error) << "PS5 virtual pad "sv << id.globalIndex << " failed: "sv << rc;
        pad = nullptr;
        return -1;
    }
    BOOST_LOG(info) << "PS5 virtual pad "sv << id.globalIndex << " attached"sv;
    return 0;
}

int rebind_gamepad(input_t &input, const gamepad_id_t &id, feedback_queue_t feedback) {
    if (id.globalIndex < 0 || id.globalIndex >= MAX_GAMEPADS) return -1;
    std::lock_guard lock(input->mutex);
    input->feedback[id.globalIndex] = std::move(feedback);
    input->client_index[id.globalIndex] = id.clientRelativeIndex;
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
    input->back[nr] = false;
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
    uint32_t flags = state.buttonFlags;
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

std::vector<supported_gamepad_t> &supported_gamepads(input_t *) {
    static std::vector<supported_gamepad_t> pads{{"ds5", true, ""}};
    return pads;
}

platform_caps::caps_t get_capabilities() { return 0; }

std::unique_ptr<client_input_t> allocate_client_input_context(input_t &) { return std::make_unique<deinit_t>(); }

// Not forwarded in v1.0.
void move_mouse(input_t &, int, int) {}
void move_mouse(input_t &, const touch_port_t &, int, int) {}
void abs_mouse(input_t &, const touch_port_t &, float, float) {}
void button_mouse(input_t &, int, bool) {}
void scroll(input_t &, int) {}
void hscroll(input_t &, int) {}
void keyboard_update(input_t &, uint16_t, bool, uint8_t, bool) {}
void unicode(input_t &, const char *, int) {}
void touch_update(client_input_t *, const touch_port_t &, const touch_input_t &) {}
void pen_update(client_input_t *, const touch_port_t &, const pen_input_t &) {}
void gamepad_touch(input_t &, const gamepad_touch_t &) {}
void gamepad_motion(input_t &, const gamepad_motion_t &) {}
void gamepad_battery(input_t &, const gamepad_battery_t &) {}
}
