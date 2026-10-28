// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace prospero {
struct EncodedFrame {
    uint64_t index;
    std::chrono::steady_clock::time_point read_time;
    bool idr;
    std::vector<uint8_t> bytes;
};

// Single-threaded queue policy, externally synchronized by its owner. On an
// overflow/discontinuity, discard dependent pictures until a complete IDR.
// This never drops bytes from the middle of a frame. It does not request IDRs;
// the capture adapter must supply that capability before network recovery works.
class FrameQueue {
public:
    FrameQueue(size_t frames, size_t bytes, std::chrono::milliseconds max_age)
        : max_frames_(frames), max_bytes_(bytes), max_age_(max_age) {}

    bool push(EncodedFrame frame) {
        if (frame.bytes.empty() || frame.bytes.size() > max_bytes_ || max_frames_ == 0) {
            reset();
            return false;
        }
        if (last_index_ && frame.index != *last_index_ + 1) reset();
        last_index_ = frame.index;
        if ((!queue_.empty() && frame.read_time - queue_.front().read_time > max_age_)
                || queue_.size() >= max_frames_ || bytes_ > max_bytes_ - frame.bytes.size()) {
            clear();
            needs_idr_ = true;
        }
        if (needs_idr_ && !frame.idr) return false;
        needs_idr_ = false;
        bytes_ += frame.bytes.size();
        queue_.push_back(std::move(frame));
        return true;
    }

    std::optional<EncodedFrame> pop(std::chrono::steady_clock::time_point now) {
        if (!queue_.empty() && now - queue_.front().read_time > max_age_) {
            clear();
            needs_idr_ = true;
        }
        if (queue_.empty()) return std::nullopt;
        EncodedFrame frame = std::move(queue_.front());
        queue_.pop_front();
        bytes_ -= frame.bytes.size();
        return frame;
    }

    void reset() { clear(); needs_idr_ = true; last_index_.reset(); }
    bool needs_idr() const { return needs_idr_; }
    size_t bytes() const { return bytes_; }
private:
    void clear() { queue_.clear(); bytes_ = 0; }
    size_t max_frames_, max_bytes_, bytes_ = 0;
    std::chrono::milliseconds max_age_;
    std::deque<EncodedFrame> queue_;
    std::optional<uint64_t> last_index_;
    bool needs_idr_ = true;
};
}
