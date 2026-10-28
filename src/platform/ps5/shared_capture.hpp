// SPDX-License-Identifier: GPL-3.0-only
// One Avcap2 session per process, shared by the video backend and the audio microphone, which
// Sunshine runs on separate threads. The session closes when the last user releases it.
#pragma once
#include "platform/ps5/capture.h"
#include <memory>
#include <mutex>

namespace prospero {

class SharedCapture {
public:
    // Returns the open session, opening it if needed; on failure null and rc holds the error.
    static std::shared_ptr<SharedCapture> acquire(int &rc);
    ~SharedCapture();
    SharedCapture(const SharedCapture &) = delete;
    SharedCapture &operator=(const SharedCapture &) = delete;

    ps_capture *session() const { return session_; }
    // Serialises calls that change session state (audio open/close, encoder controls).
    std::mutex &control() { return control_; }

private:
    explicit SharedCapture(ps_capture *session) : session_(session) {}
    ps_capture *session_;
    std::mutex control_;
};

}
