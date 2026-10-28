// SPDX-License-Identifier: GPL-3.0-only
#include "capture/capture_session.hpp"
#include "platform/ps5/shared_capture.hpp"

namespace prospero {
namespace {
class Ps5CaptureBackend final : public CaptureBackend {
public:
    int open() override {
        int rc = 0;
        shared_ = SharedCapture::acquire(rc);
        return rc;
    }
    int headers(void *destination, size_t capacity) override {
        return ps_capture_headers(shared_->session(), destination, capacity);
    }
    int read(void *destination, size_t capacity, uint8_t descriptor[0x70]) override {
        return ps_capture_read(shared_->session(), destination, capacity, descriptor);
    }
    int request_keyframe() override {
        std::lock_guard lock(shared_->control());
        return ps_capture_request_keyframe(shared_->session());
    }
    int set_bitrate(uint32_t average, uint32_t peak) override {
        std::lock_guard lock(shared_->control());
        return ps_capture_set_bitrate(shared_->session(), average, peak);
    }
    // Releases this user's reference; the session closes once audio has released it too.
    int close() override {
        shared_.reset();
        return 0;
    }

private:
    std::shared_ptr<SharedCapture> shared_;
};
}

std::unique_ptr<CaptureBackend> make_ps5_capture_backend() { return std::make_unique<Ps5CaptureBackend>(); }
}
