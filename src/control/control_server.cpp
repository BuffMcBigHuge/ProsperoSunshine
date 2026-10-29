// SPDX-License-Identifier: GPL-3.0-only
#include "control/control_server.hpp"
#include "src/logging.h"
#include "src/nvhttp.h"
#include "src/process.h"
#include "src/rtsp.h"
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace std::literals;

namespace prospero {

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
        std::string id, pin, name;
        if (!args.empty() && args[0].size() <= 8) {  // PIN <pin> [name]: the only pending request
            if (pending.size() != 1) return "ERR pending=" + std::to_string(pending.size()) + ", use PIN <id> <pin>";
            id = pending.front().id;
            pin = args[0];
            name = args.size() > 1 ? args[1] : pending.front().name;
        } else if (args.size() >= 2) {
            id = args[0];
            pin = args[1];
            name = args.size() > 2 ? args[2] : "moonlight";
        } else {
            return "ERR usage: PIN <pin> [name] | PIN <id> <pin> [name]";
        }
        BOOST_LOG(info) << "Control: PIN submitted for a pending pairing"sv;
        return nvhttp::pin(id, pin, name) ? "OK paired" : "ERR pairing failed";
    }
    if (command == "QUIT") {
        shutdown_->raise(true);
        return "OK bye";
    }
    return "ERR unknown command";
}

void ControlServer::run(std::stop_token stop) {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return;
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) || listen(listener, 2)) {
        BOOST_LOG(error) << "Control socket failed to bind port "sv << port;
        close(listener);
        return;
    }
    BOOST_LOG(info) << "Control socket on port "sv << port;
    while (!stop.stop_requested()) {
        pollfd pfd{listener, POLLIN, 0};
        if (poll(&pfd, 1, 250) <= 0) continue;
        const int client = accept(listener, nullptr, nullptr);
        if (client < 0) continue;
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
