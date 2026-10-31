// SPDX-License-Identifier: GPL-3.0-only
// Development control socket (ROADMAP P2.3). One line per request, one line per answer:
//   STATUS                      -> "OK running=<0|1> app=<name> pending=<n> address=<ip> client=<name>"
//                                  (address and client empty while no stream runs; client is the rest
//                                  of the line and may contain spaces)
//   PAIRINGS                    -> "OK <id> <client name>" per pending request, then "END"
//   PIN <pin> [name]            -> approves the only pending request ("OK paired" / "ERR ...")
//   PIN <id> <pin> [name]       -> approves the given request
//   REPORT                      -> "OK port=<n>": for some seconds, <appdata>/diagnostics.txt is on that port
//   AUTOLOAD [ON|OFF]           -> the Payload Manager's autoload list, for the app (autoload_host.hpp)
//   QUIT                        -> raises the host's shutdown event
// It has no authentication, so it listens on 127.0.0.1 only (the PS5 app); the empty file
// <appdata>/control_lan opens it to the LAN for development on trusted networks (ROADMAP P9.1).
#pragma once
#include "src/thread_safe.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>

namespace prospero {

class ControlServer {
public:
    static constexpr uint16_t port = 47995;
    static constexpr uint16_t report_port = 47996;
    explicit ControlServer(std::shared_ptr<safe::event_t<bool>> shutdown) : shutdown_(std::move(shutdown)) {}
    void run(std::stop_token stop);
    void run_report(std::stop_token stop);
    // Handles one request line; public for tests.
    std::string handle(const std::string &line);

private:
    // The app asks every two seconds while its page is open; this outlasts a missed turn or two.
    static constexpr std::chrono::seconds report_window{8};
    std::shared_ptr<safe::event_t<bool>> shutdown_;
    std::atomic_bool report_listening_{false};
    std::atomic<std::chrono::steady_clock::rep> report_until_{0};
};

}
