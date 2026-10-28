// SPDX-License-Identifier: GPL-3.0-only
// Development control socket (ROADMAP P2.3). One line per request, one line per answer:
//   STATUS                      -> "OK running=<0|1> app=<name> pending=<n>"
//   PAIRINGS                    -> "OK <id> <client name>" per pending request, then "END"
//   PIN <pin> [name]            -> approves the only pending request ("OK paired" / "ERR ...")
//   PIN <id> <pin> [name]       -> approves the given request
//   QUIT                        -> raises the host's shutdown event
// It listens on all interfaces, so it is for trusted development networks only; the product path
// is the Host API for the PS5 app (P2.5).
#pragma once
#include "src/thread_safe.h"
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

namespace prospero {

class ControlServer {
public:
    static constexpr uint16_t port = 47995;
    explicit ControlServer(std::shared_ptr<safe::event_t<bool>> shutdown) : shutdown_(std::move(shutdown)) {}
    void run(std::stop_token stop);
    // Handles one request line; public for tests.
    std::string handle(const std::string &line);

private:
    std::shared_ptr<safe::event_t<bool>> shutdown_;
};

}
