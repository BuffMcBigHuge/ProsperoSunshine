// SPDX-License-Identifier: GPL-3.0-only
#include "control/listener_watch.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
    int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr = loopback_address(0);
    assert(bind(listener, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0);
    assert(listen(listener, 4) == 0);
    socklen_t size = sizeof addr;
    assert(getsockname(listener, reinterpret_cast<sockaddr *>(&addr), &size) == 0);
    const uint16_t port = ntohs(addr.sin_port);

    assert(!port_refused(port));
    assert(!port_free(port));
    close(listener);
    assert(port_refused(port));
    assert(port_free(port));
    std::cout << "PASS: listener watch (refused only without a listener, free only without one)\n";
}
