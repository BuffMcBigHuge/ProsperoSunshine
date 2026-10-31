// SPDX-License-Identifier: GPL-3.0-only
#include "platform/ps5/shared_capture.hpp"
#include <mutex>

namespace prospero {
namespace {
std::mutex registry;
std::weak_ptr<SharedCapture> current;

// Here and not in the video thread, because audio may open the session first and would then stay silent.
int open_session(ps_capture **session) {
    const int rc = ps_capture_open(session);
    return rc && ps_capture_fall_back_mode() ? ps_capture_open(session) : rc;
}
}

std::shared_ptr<SharedCapture> SharedCapture::acquire(int &rc) {
    std::lock_guard lock(registry);
    rc = 0;
    if (auto existing = current.lock()) {
        std::unique_lock session_lock(existing->mutex_);
        if ((existing->failed_ || !ps_capture_mode_current(existing->session_)) && (rc = existing->reopen())) return nullptr;
        return existing;
    }
    ps_capture *session = nullptr;
    rc = open_session(&session);
    if (rc) return nullptr;
    std::shared_ptr<SharedCapture> created(new SharedCapture(session));
    current = created;
    return created;
}

int SharedCapture::reopen() {
    ps_capture_close(session_);  // also closes an audio channel on it
    session_ = nullptr;
    const int rc = open_session(&session_);
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
