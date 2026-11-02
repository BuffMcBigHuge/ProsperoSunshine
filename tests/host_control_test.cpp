// SPDX-License-Identifier: GPL-3.0-only
// How the firmware probe tells from the host's STATUS line that a stream runs (host_control.hpp): it must not start then.
#include "host_control.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using host_control::streaming_in;
    assert(streaming_in("OK running=1 app=Desktop pending=0 address=10.0.0.5 client=Moonlight PC\n"));
    assert(streaming_in("OK running=0 app= pending=0 address=fe80::1 client="));
    assert(!streaming_in("OK running=0 app= pending=0 address= client="));
    assert(!streaming_in("OK running=1 app=Desktop pending=0 address= client=\n"));
    assert(!streaming_in("OK running=0 app= pending=0 address=\r\n"));
    assert(!streaming_in(""));  // no answer: no host, so no stream from it
    assert(!streaming_in("ERR unknown command"));
    std::puts("PASS: host STATUS read as streaming only with a client address");
    return 0;
}
