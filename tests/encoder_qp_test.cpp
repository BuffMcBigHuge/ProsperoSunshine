// SPDX-License-Identifier: GPL-3.0-only
// The QP firmware 10.x encodes with, from the client's bitrate (platform/ps5/encoder_qp.h). tests/capture_open_test.c
// takes it through configure().
#include "platform/ps5/encoder_qp.h"
#include <cassert>
#include <cstdio>

int main() {
    const auto at = [](uint64_t kbit) { return ps_qp_for_bitrate(kbit * 1000); };
    assert(at(5000).idr == 30 && at(5000).other == 34);
    assert(at(10000).idr == 27 && at(10000).other == 31);
    assert(at(16364).idr == 24 && at(16364).other == 28);  // Moonlight for PC at 1920x1200, 60 fps
    assert(at(20000).idr == 21 && at(20000).other == 25);
    assert(at(46988).idr == 21);  // Moonlight for iOS
    // The edges, and a higher bitrate never gets a coarser picture.
    assert(at(7999).idr == 30 && at(8000).idr == 27);
    assert(at(11999).idr == 27 && at(12000).idr == 24);
    assert(at(19999).idr == 24 && at(20000).idr == 21);
    for (uint64_t bitrate = 0; bitrate < 200000000u; bitrate += 250000u) {
        const ps_qp qp = ps_qp_for_bitrate(bitrate), next = ps_qp_for_bitrate(bitrate + 250000u);
        assert(next.idr <= qp.idr && next.other <= qp.other);
        assert(qp.idr >= 1 && qp.other <= 51 && qp.other > qp.idr);  // 52 and above make Start fail
    }
    std::puts("PASS: encoder QP on 10.x (from the client's bitrate, edges, never coarser for more)");
    return 0;
}
