// SPDX-License-Identifier: GPL-3.0-only
#include "video/annex_b.hpp"
#include "video/frame_queue.hpp"
#include <cassert>
#include <iostream>
using namespace std::chrono_literals;
using prospero::EncodedFrame;
using prospero::FrameQueue;

int main() {
    const uint8_t idr[] = {0, 0, 0, 1, 0x65, 3};
    const uint8_t short_idr[] = {0, 0, 1, 0x65};
    const uint8_t predicted[] = {0, 0, 1, 0x41, 5};
    const uint8_t truncated[] = {0, 0, 0, 1};
    assert(prospero::has_idr(idr));
    assert(prospero::has_idr(short_idr));
    assert(!prospero::has_idr(predicted));
    assert(!prospero::has_idr(truncated));
    const auto now = std::chrono::steady_clock::now();
    auto frame = [&](uint64_t i, bool key, size_t n = 4) {
        return EncodedFrame{i, now, key, std::vector<uint8_t>(n)};
    };
    FrameQueue q(2, 12, 100ms);
    assert(!q.push(frame(0, false))); // A new decoder needs a random access point.
    assert(q.push(frame(1, true)));
    assert(q.push(frame(2, false)));
    assert(!q.push(frame(3, false))); // Count overflow loses references.
    assert(!q.pop(now));
    assert(q.needs_idr());
    assert(!q.push(frame(4, false)));
    assert(q.push(frame(5, true)));
    assert(q.pop(now)->index == 5);
    assert(!q.push(frame(7, false))); // Missing frame 6 invalidates the chain.
    assert(q.push(frame(8, true, 10)));
    assert(!q.push(frame(9, false, 4))); // Byte limit, even with spare frame slots.
    assert(q.bytes() == 0 && q.needs_idr());
    assert(q.push(frame(10, true)));
    assert(!q.pop(now + 101ms)); // Never deliver a frame older than the queue budget.
    assert(q.needs_idr());
    assert(!q.push(frame(11, true, 13))); // A single oversized frame cannot grow the queue.
    assert(q.bytes() == 0);
    std::cout << "PASS: Annex B, decoder startup, frame loss, count/byte/age limits and IDR recovery\n";
}
