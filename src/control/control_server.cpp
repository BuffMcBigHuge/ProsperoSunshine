// SPDX-License-Identifier: GPL-3.0-only
#include "control/control_server.hpp"
#include "control/autoload_host.hpp"
#include "src/logging.h"
#include "src/platform/common.h"
#include "src/nvhttp.h"
#include "src/process.h"
#include "src/rtsp.h"
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::literals;

namespace prospero {

ControlServer::ControlServer(std::shared_ptr<safe::event_t<bool>> shutdown)
    : shutdown_(std::move(shutdown)), modes_(platf::appdata() / "device_modes.txt") {
    nvhttp::set_launch_listener([this](const nvhttp::launch_client_t &client) {
        modes_.remember(client.uuid, format_mode(client.width, client.height, client.fps));
    });
}

ControlServer::~ControlServer() {
    nvhttp::set_launch_listener(nullptr);
}

std::string ControlServer::handle(const std::string &line) {
    std::istringstream in(line);
    std::string command;
    in >> command;
    if (command == "STATUS") {
        const auto pending = nvhttp::get_pending_pairings().size();
        std::string answer = "OK running="s + (proc::proc.running() > 0 ? "1" : "0") +
                             " app=" + proc::proc.get_last_run_app_name() + " pending=" + std::to_string(pending);
        // The client of the live stream; the name comes last because it may contain spaces.
        const auto client = rtsp_stream::session_count() > 0 ? nvhttp::get_last_launch_client() : nvhttp::launch_client_t{};
        return answer + " address=" + client.address + " client=" + client.name;
    }
    if (command == "PAIRINGS") {
        std::string out;
        for (const auto &p : nvhttp::get_pending_pairings()) out += "OK " + p.id + ' ' + p.name + " " + p.address + '\n';
        return out + "END";
    }
    if (command == "PIN") {
        std::vector<std::string> args;
        for (std::string a; in >> a;) args.push_back(a);
        const auto pending = nvhttp::get_pending_pairings();
        // The name is the rest of the line, as it may hold spaces.
        const auto name_from = [&args](std::size_t first) {
            std::string joined;
            for (std::size_t i = first; i < args.size(); ++i) joined += (i > first ? " " : "") + args[i];
            return joined;
        };
        std::string id, pin, name;
        if (!args.empty() && args[0].size() <= 8) {  // PIN <pin> [name]: the only pending request
            if (pending.size() != 1) return "ERR pending=" + std::to_string(pending.size()) + ", use PIN <id> <pin>";
            id = pending.front().id;
            pin = args[0];
            name = args.size() > 1 ? name_from(1) : pending.front().name;
        } else if (args.size() >= 2) {
            id = args[0];
            pin = args[1];
            name = args.size() > 2 ? name_from(2) : "moonlight";
        } else {
            return "ERR usage: PIN <pin> [name] | PIN <id> <pin> [name]";
        }
        BOOST_LOG(info) << "Control: PIN submitted for a pending pairing"sv;
        return nvhttp::pin(id, pin, name) ? "OK paired" : "ERR pairing failed";
    }
    if (command == "DEVICES") {
        const auto live = rtsp_stream::session_count() > 0 ? nvhttp::get_last_launch_client().uuid : std::string();
        std::string out;
        for (const auto &client : nvhttp::get_all_clients()) {
            const std::string uuid = client.value("uuid", "");
            const std::string mode = modes_.mode(uuid);
            out += "OK " + uuid + (!live.empty() && uuid == live ? " 1 " : " 0 ") + (mode.empty() ? "-" : mode) + ' ' +
                   client.value("name", "") + '\n';
        }
        return out + "END";
    }
    if (command == "RENAME") {
        std::string uuid, name;
        in >> uuid;
        std::getline(in >> std::ws, name);  // the rest of the line, as it may hold spaces
        if (uuid.empty() || name.empty()) return "ERR usage: RENAME <uuid> <name>";
        if (!nvhttp::rename_client(uuid, name)) return "ERR no such device";
        BOOST_LOG(info) << "Control: a paired device was renamed"sv;
        return "OK renamed";
    }
    if (command == "UNPAIR") {
        std::string uuid;
        in >> uuid;
        const std::string cert = nvhttp::get_cert_by_uuid(uuid);
        if (cert.empty() || !nvhttp::unpair_client(uuid)) return "ERR no such device";
        // Removing the pairing only stops new connections; a running stream of this device ends here.
        rtsp_stream::terminate_sessions_by_cert(cert);
        modes_.forget(uuid);
        BOOST_LOG(info) << "Control: a paired device was removed"sv;
        return "OK removed";
    }
    if (command == "REPORT") {
        if (!report_listening_) return "ERR report port not open";
        report_until_ = std::chrono::steady_clock::now().time_since_epoch().count() +
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(report_window).count();
        return "OK port=" + std::to_string(report_port);
    }
    if (command == "AUTOLOAD") {
        std::string action;
        in >> action;
        if (action.empty()) return autoload_host::status();
        if (action != "ON" && action != "OFF") return "ERR usage: AUTOLOAD [ON|OFF]";
        BOOST_LOG(info) << "Control: autoload "sv << action;
        return autoload_host::set(action == "ON");
    }
    if (command == "RELISTEN") {
        relisten_ = true;
        return "OK";
    }
    if (command == "QUIT") {
        shutdown_->raise(true);
        return "OK bye";
    }
    return "ERR unknown command";
}

// The app's diagnostic report for a phone or PC. A title may not listen on the network (bind fails with
// EACCES), so the host does, and only answers while the app's page keeps asking for it.
void ControlServer::run_report(std::stop_token stop) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return;
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(report_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) || listen(listener, 4)) {
        BOOST_LOG(warning) << "Report port "sv << report_port << " failed to bind"sv;
        close(listener);
        return;
    }
    report_listening_ = true;
    while (!stop.stop_requested()) {
        pollfd pfd{listener, POLLIN, 0};
        if (poll(&pfd, 1, 250) <= 0) continue;
        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            std::this_thread::sleep_for(100ms);  // no busy loop on a socket lost to rest mode
            continue;
        }
        if (std::chrono::steady_clock::now().time_since_epoch().count() < report_until_) {
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
            // The request does not matter; reading it keeps the close from resetting the answer.
            pollfd cfd{client, POLLIN, 0};
            char request[2048];
            if (poll(&cfd, 1, 500) > 0) (void)!recv(client, request, sizeof request, 0);
            std::ostringstream body;
            body << std::ifstream(platf::appdata() / "diagnostics.txt", std::ios::binary).rdbuf();
            const std::string text = body.str();
            const std::string answer = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                                       std::to_string(text.size()) + "\r\nConnection: close\r\n\r\n" + text;
            for (size_t sent = 0; sent < answer.size();) {
                const ssize_t n = send(client, answer.data() + sent, answer.size() - sent, 0);
                if (n <= 0) break;
                sent += static_cast<size_t>(n);
            }
        }
        close(client);
    }
    close(listener);
}

void ControlServer::run(std::stop_token stop) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return;
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    // The socket has no authentication: only the PS5 app on the console reaches it, unless the
    // development switch file exists.
    std::error_code ec;
    const bool lan = std::filesystem::exists(platf::appdata() / "control_lan", ec);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(lan ? INADDR_ANY : INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) || listen(listener, 2)) {
        BOOST_LOG(error) << "Control socket failed to bind port "sv << port;
        close(listener);
        return;
    }
    BOOST_LOG(info) << "Control socket on port "sv << port << (lan ? " (LAN, development switch control_lan)"sv : " (local only)"sv);
    while (!stop.stop_requested()) {
        pollfd pfd{listener, POLLIN, 0};
        if (poll(&pfd, 1, 250) <= 0) continue;
        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) {
            std::this_thread::sleep_for(100ms);
            continue;
        }
        std::string buffer;
        char chunk[256];
        // One client at a time; requests are short and rare.
        while (!stop.stop_requested()) {
            pollfd cfd{client, POLLIN, 0};
            if (poll(&cfd, 1, 250) <= 0) continue;
            const ssize_t n = recv(client, chunk, sizeof chunk, 0);
            if (n <= 0) break;
            buffer.append(chunk, static_cast<size_t>(n));
            for (size_t nl; (nl = buffer.find('\n')) != std::string::npos;) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::string answer = handle(line) + '\n';
                send(client, answer.data(), answer.size(), 0);
            }
            if (buffer.size() > 4096) break;
        }
        close(client);
    }
    close(listener);
}

}
