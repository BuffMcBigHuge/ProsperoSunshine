// SPDX-License-Identifier: GPL-3.0-only
#include "platform/ps5/shared_capture.hpp"

namespace prospero {
namespace {
std::mutex registry;
std::weak_ptr<SharedCapture> current;
}

std::shared_ptr<SharedCapture> SharedCapture::acquire(int &rc) {
    std::lock_guard lock(registry);
    rc = 0;
    if (auto existing = current.lock()) return existing;
    ps_capture *session = nullptr;
    rc = ps_capture_open(&session);
    if (rc) return nullptr;
    std::shared_ptr<SharedCapture> created(new SharedCapture(session));
    current = created;
    return created;
}

SharedCapture::~SharedCapture() { ps_capture_close(session_); }

}
