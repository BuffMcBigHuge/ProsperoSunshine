// SPDX-License-Identifier: GPL-3.0-only
#include "platform/ps5/shared_capture.hpp"
#include <mutex>

namespace prospero {
namespace {
std::mutex registry[PS_CAPTURE_SLOTS];
std::weak_ptr<SharedCapture> current[PS_CAPTURE_SLOTS];

bool valid(int slot) { return slot >= 0 && slot < PS_CAPTURE_SLOTS; }

// Here and not in the video thread, because audio may open the session first and would then stay silent.
int open_session(int slot, ps_capture **session) { return ps_capture_slot_open_or_fall_back(slot, session); }
}

std::shared_ptr<SharedCapture> SharedCapture::acquire(int &rc, int slot) {
    rc = PS_CAPTURE_ERROR;
    if (!valid(slot)) return nullptr;
    std::lock_guard lock(registry[slot]);
    rc = 0;
    if (auto existing = current[slot].lock()) {
        std::unique_lock session_lock(existing->mutex_);
        if ((existing->failed_ || !ps_capture_mode_current(existing->session_)) && (rc = existing->reopen())) return nullptr;
        return existing;
    }
    ps_capture *session = nullptr;
    rc = open_session(slot, &session);
    if (rc) return nullptr;
    std::shared_ptr<SharedCapture> created(new SharedCapture(session, slot));
    current[slot] = created;
    return created;
}

std::shared_ptr<SharedCapture> SharedCapture::attach(int slot) {
    if (!valid(slot)) return nullptr;
    std::unique_lock lock(registry[slot], std::try_to_lock);
    return lock ? current[slot].lock() : nullptr;
}

int SharedCapture::reopen() {
    ps_capture_close(session_);  // also closes an audio channel on it
    session_ = nullptr;
    const int rc = open_session(slot_, &session_);
    if (rc) {
        session_ = nullptr;
        failed_ = true;
        return rc;
    }
    failed_ = false;
    ++generation_;
    return 0;
}

SharedCapture::~SharedCapture() { ps_capture_close(session_); }

}
