// SPDX-License-Identifier: GPL-3.0-only
#include "capture/capture_session.hpp"
#include "video/annex_b.hpp"
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
using namespace std::chrono_literals;
using prospero::CaptureSession;
using prospero::CaptureStatus;
using prospero::EncodedFrame;

namespace {
struct Calls {
    int opens = 0, closes = 0;
};

// Scripted backend: each read pops the next entry; an empty vector means "no frame yet",
// a vector holding one negative byte count is an error code.
class FakeBackend final : public prospero::CaptureBackend {
public:
    FakeBackend(Calls &calls, int open_rc, std::deque<std::vector<int>> script)
        : calls_(calls), open_rc_(open_rc), script_(std::move(script)) {}
    int open() override { ++calls_.opens; return open_rc_; }
    int headers(void *destination, size_t) override {
        const uint8_t sps_pps[] = {0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x68, 2};
        std::memcpy(destination, sps_pps, sizeof sps_pps);
        return sizeof sps_pps;
    }
    int read(void *destination, size_t, uint8_t descriptor[0x70]) override {
        descriptor[8] = 0x42;
        if (script_.empty()) return 0;
        auto next = std::move(script_.front());
        script_.pop_front();
        if (next.size() == 1 && next[0] < 0) return next[0];
        for (size_t i = 0; i < next.size(); ++i) static_cast<uint8_t *>(destination)[i] = static_cast<uint8_t>(next[i]);
        return static_cast<int>(next.size());
    }
    int close() override { ++calls_.closes; return 0; }

private:
    Calls &calls_;
    int open_rc_;
    std::deque<std::vector<int>> script_;
};

auto session(Calls &calls, int open_rc, std::deque<std::vector<int>> script) {
    return CaptureSession(std::make_unique<FakeBackend>(calls, open_rc, std::move(script)));
}
}

int main() {
    const std::vector<int> idr = {0, 0, 0, 1, 0x65, 9};
    const std::vector<int> idr_with_sps = {0, 0, 0, 1, 0x67, 1, 0, 0, 0, 1, 0x65, 9};
    const std::vector<int> p = {0, 0, 0, 1, 0x41, 7};
    {
        Calls calls;
        auto s = session(calls, -2, {});
        assert(s.start() == -2);  // Busy profile: nothing to close, nothing running.
        assert(!s.running() && calls.closes == 0);
        EncodedFrame f;
        assert(s.read(f, 1ms) == CaptureStatus::stopped);
    }
    {
        Calls calls;
        {
            auto s = session(calls, 0, {{}, idr, p, idr_with_sps, {}});
            assert(s.start() == 0 && s.running());
            assert(s.parameter_sets().size() == 12);
            EncodedFrame f;
            CaptureSession::Descriptor desc{};
            assert(s.read(f, 50ms, {}, &desc) == CaptureStatus::frame);
            assert(f.index == 0 && f.idr && desc[8] == 0x42);
            assert(prospero::has_sps(f.bytes) && f.bytes.size() == 12 + idr.size());  // SPS/PPS prepended.
            assert(s.read(f, 50ms) == CaptureStatus::frame);
            assert(f.index == 1 && !f.idr && f.bytes.size() == p.size());
            assert(s.read(f, 50ms) == CaptureStatus::frame);
            assert(f.idr && f.bytes.size() == idr_with_sps.size());  // Already self-contained.
            assert(s.read(f, 5ms) == CaptureStatus::timeout);
            std::stop_source stop;
            stop.request_stop();
            assert(s.read(f, 50ms, stop.get_token()) == CaptureStatus::stopped);
            assert(s.stop() == 0 && s.stop() == 0);
        }
        assert(calls.opens == 1 && calls.closes == 1);  // Exactly one close, destructor included.
    }
    {
        Calls calls;
        {
            auto s = session(calls, 0, {p, {-5}});
            assert(s.start() == 0);
            EncodedFrame f;
            assert(s.read(f, 50ms) == CaptureStatus::frame);
            assert(s.read(f, 50ms) == CaptureStatus::revoked);
            assert(!s.running() && s.read(f, 1ms) == CaptureStatus::stopped);
        }
        assert(calls.closes == 1);  // Revocation cleans up immediately, not again on destruction.
    }
    {
        Calls calls;
        auto s = session(calls, 0, {{-1}});
        assert(s.start() == 0);
        EncodedFrame f;
        assert(s.read(f, 50ms) == CaptureStatus::error && calls.closes == 1);
    }
    {
        // After a lost capture the stream reopens; frame numbers must keep counting forwards.
        Calls calls;
        auto s = session(calls, 0, {idr, p, {-5}, idr, p});
        assert(s.start() == 0);
        EncodedFrame f;
        assert(s.read(f, 50ms) == CaptureStatus::frame && f.index == 0);
        assert(s.read(f, 50ms) == CaptureStatus::frame && f.index == 1);
        assert(s.read(f, 50ms) == CaptureStatus::revoked && !s.running());
        assert(s.start() == 0 && calls.opens == 2);
        assert(s.read(f, 50ms) == CaptureStatus::frame && f.index == 2 && f.idr);
        assert(s.read(f, 50ms) == CaptureStatus::frame && f.index == 3);
    }
    std::cout << "PASS: capture session start/busy, SPS/PPS on IDR, timeout, stop token, revocation, single close, "
                 "frame numbers across reopen\n";
}
