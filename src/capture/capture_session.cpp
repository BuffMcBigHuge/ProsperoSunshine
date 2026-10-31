// SPDX-License-Identifier: GPL-3.0-only
#include "capture/capture_session.hpp"
#include <thread>

namespace prospero {
namespace {
constexpr int error_revoked = -5;  // PS_CAPTURE_REVOKED
constexpr auto poll_interval = std::chrono::milliseconds(1);
}

CaptureSession::CaptureSession(std::unique_ptr<CaptureBackend> backend, Codec codec)
    : backend_(std::move(backend)), codec_(codec) {}

CaptureSession::~CaptureSession() { stop(); }

int CaptureSession::start() {
    if (running_) return 0;
    if (!backend_) return -1;
    if (const int rc = backend_->open()) return rc;
    running_ = true;
    buffer_.resize(max_frame_bytes);
    const int size = backend_->headers(buffer_.data(), buffer_.size());
    if (size <= 0) {
        stop();
        return size < 0 ? size : -1;
    }
    headers_.assign(buffer_.begin(), buffer_.begin() + size);
    // Frame numbers continue across a restart, so a stream that recovers from a lost capture
    // keeps counting forwards.
    return 0;
}

CaptureStatus CaptureSession::read(EncodedFrame &frame, std::chrono::milliseconds timeout, std::stop_token stop_token,
                                   Descriptor *descriptor) {
    if (!running_) return CaptureStatus::stopped;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    Descriptor local{};
    Descriptor &desc = descriptor ? *descriptor : local;
    for (;;) {
        if (stop_token.stop_requested()) return CaptureStatus::stopped;
        const int rc = backend_->read(buffer_.data(), buffer_.size(), desc.data());
        if (rc < 0) {
            stop();
            return rc == error_revoked ? CaptureStatus::revoked : CaptureStatus::error;
        }
        if (rc > 0) {
            const std::span<const uint8_t> bytes(buffer_.data(), static_cast<size_t>(rc));
            frame.index = next_index_++;
            frame.read_time = std::chrono::steady_clock::now();
            frame.idr = has_idr(bytes, codec_);
            frame.bytes.clear();
            if (frame.idr && !has_sps(bytes, codec_)) frame.bytes.insert(frame.bytes.end(), headers_.begin(), headers_.end());
            frame.bytes.insert(frame.bytes.end(), bytes.begin(), bytes.end());
            return CaptureStatus::frame;
        }
        if (std::chrono::steady_clock::now() >= deadline) return CaptureStatus::timeout;
        std::this_thread::sleep_for(poll_interval);
    }
}

int CaptureSession::stop() {
    if (!running_) return 0;
    running_ = false;
    return backend_->close();
}

}
