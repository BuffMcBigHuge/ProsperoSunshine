// SPDX-License-Identifier: GPL-3.0-only
// What the Experimental switches unlock on which firmware, and the order of attempts (platform/ps5/experimental.h).
// tests/capture_open_test.c takes the same rules through capture.c.
#include "platform/ps5/experimental.h"
#include "video/capture_mode.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace {
constexpr uint32_t fw1300 = 0x13000040u, fw1020 = 0x10200006u, fw1040 = 0x10400000u, fw1360 = 0x13600000u, fw1240 = 0x12400000u;

ps_experiment make(uint32_t firmware, int hevc, int hdr, int large) {
    ps_experiment e;
    ps_exp_init(&e, firmware, hevc, hdr, large);
    return e;
}
}

int main() {
    // Verified firmware: the switches don't count, so they can't change anything.
    for (const uint32_t firmware : {fw1300, fw1020}) {
        const ps_experiment e = make(firmware, 1, 1, 0);
        assert(!e.hevc && !e.hdr && !ps_exp_any(&e));
        assert(!ps_exp_codec_on_trial(&e, 1, 0) && !ps_exp_codec_on_trial(&e, 1, 1));
        assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_STANDARD);
    }
    assert(!make(fw1300, 0, 0, 1).large);
    assert(make(fw1020, 0, 0, 1).large);  // 10.20 has only HEVC and HDR verified
    assert(ps_exp_hevc_verified(fw1300) && ps_exp_hevc_verified(fw1020) && !ps_exp_hevc_verified(fw1040) && !ps_exp_hevc_verified(fw1360));
    assert(ps_exp_large_verified(fw1300) && !ps_exp_large_verified(fw1020) && !ps_exp_large_verified(fw1360));

    // All off: nothing is offered, nothing is on trial, H.264 and HEVC keep the standard record.
    for (const uint32_t firmware : {fw1360, fw1240, fw1040}) {
        const ps_experiment e = make(firmware, 0, 0, 0);
        assert(!ps_exp_any(&e) && !ps_exp_hevc_offered(&e) && !ps_exp_hdr_offered(&e) && !ps_exp_large_offered(&e));
        assert(ps_exp_variant_for(&e, 0, 0) == PS_EXP_STANDARD && ps_exp_variant_for(&e, 1, 0) == PS_EXP_STANDARD);
        assert(!ps_exp_unproven_codec(&e, 1, 0) && !ps_exp_unproven_mode(&e, 6));
    }

    // Each switch alone. HDR needs HEVC to be offered at all.
    ps_experiment e = make(fw1360, 1, 0, 0);
    assert(ps_exp_hevc_offered(&e) && !ps_exp_hdr_offered(&e) && !ps_exp_large_offered(&e));
    e = make(fw1360, 0, 1, 0);
    assert(ps_exp_hevc_offered(&e) && ps_exp_hdr_offered(&e));
    e = make(fw1360, 0, 0, 1);
    assert(!ps_exp_hevc_offered(&e) && ps_exp_large_offered(&e) && ps_exp_unproven_mode(&e, 5) && !ps_exp_unproven_mode(&e, 4));

    // H.264 at 1080p is never on trial.
    e = make(fw1360, 1, 1, 1);
    assert(!ps_exp_unproven_codec(&e, 0, 0) && !ps_exp_unproven_mode(&e, 4) && ps_exp_variant_for(&e, 0, 0) == PS_EXP_STANDARD);

    // Minimal first, then full; a variant that ran stays, also through later failures.
    e = make(fw1360, 1, 0, 0);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_MINIMAL);
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_NEXT_VARIANT);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_FULL);
    ps_exp_succeeded(&e, 1, 0, 4);
    assert(!ps_exp_unproven_codec(&e, 1, 0));
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_GIVE_UP);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_FULL && ps_exp_hevc_offered(&e));

    // Both fail: off until the host restarts.
    e = make(fw1360, 1, 0, 0);
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_NEXT_VARIANT);
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_GIVE_UP);
    assert(e.hevc_failed && !ps_exp_hevc_offered(&e) && ps_exp_variant_for(&e, 1, 0) == PS_EXP_STANDARD);

    // 8 and 10 bit try on their own; losing 8 bit takes HDR along.
    e = make(fw1360, 1, 1, 0);
    assert(ps_exp_failed(&e, 1, 1, 1, 0) == PS_EXP_NEXT_VARIANT);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_MINIMAL && ps_exp_variant_for(&e, 1, 1) == PS_EXP_FULL);
    assert(ps_exp_failed(&e, 1, 1, 1, 0) == PS_EXP_GIVE_UP);
    assert(e.hdr_failed && !e.hevc_failed && !ps_exp_hdr_offered(&e) && ps_exp_hevc_offered(&e));
    e = make(fw1360, 1, 1, 0);
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_NEXT_VARIANT);
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_GIVE_UP);
    assert(!ps_exp_hevc_offered(&e) && !ps_exp_hdr_offered(&e));

    // Failures that say nothing about the function: no next variant, nothing goes off.
    e = make(fw1360, 1, 0, 1);
    assert(ps_exp_failed(&e, 1, 0, 0, 0) == PS_EXP_GIVE_UP);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_MINIMAL && ps_exp_hevc_offered(&e));
    // The system refusing 4K for a title falls back as always, and doesn't take the switch away.
    assert(ps_exp_failed(&e, 0, 0, 0, 1) == PS_EXP_FALL_BACK);
    ps_exp_succeeded(&e, 0, 0, 4);
    assert(ps_exp_large_offered(&e));

    // The mode failed and 1080p runs: the mode goes off, in the same stream.
    e = make(fw1360, 0, 0, 1);
    assert(ps_exp_failed(&e, 0, 0, 1, 1) == PS_EXP_FALL_BACK);
    assert(ps_exp_large_offered(&e));
    ps_exp_succeeded(&e, 0, 0, 4);
    assert(e.large_failed && !ps_exp_large_offered(&e));
    // 1080p fails as well: nothing shows that the mode was at fault.
    e = make(fw1360, 0, 0, 1);
    assert(ps_exp_failed(&e, 0, 0, 1, 1) == PS_EXP_FALL_BACK);
    assert(ps_exp_failed(&e, 0, 0, 1, 0) == PS_EXP_GIVE_UP);
    assert(!e.large_failed && !e.large_pending);
    // A mode that ran once stays: later refusals are the title's.
    e = make(fw1360, 0, 0, 1);
    ps_exp_succeeded(&e, 0, 0, 6);
    assert(!ps_exp_unproven_mode(&e, 6));
    assert(ps_exp_failed(&e, 0, 0, 1, 1) == PS_EXP_FALL_BACK);
    ps_exp_succeeded(&e, 0, 0, 4);
    assert(ps_exp_large_offered(&e));

    // HEVC at 4K: both variants, then 1080p from the first variant on.
    e = make(fw1360, 1, 0, 1);
    assert(ps_exp_failed(&e, 1, 0, 1, 1) == PS_EXP_NEXT_VARIANT);
    assert(ps_exp_failed(&e, 1, 0, 1, 1) == PS_EXP_FALL_BACK);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_MINIMAL);
    ps_exp_succeeded(&e, 1, 0, 4);
    assert(ps_exp_hevc_offered(&e) && !ps_exp_unproven_codec(&e, 1, 0) && !ps_exp_large_offered(&e));

    // 10.x has one variant: configure() leaves 13.00's fields out there.
    e = make(fw1040, 1, 0, 0);
    assert(ps_exp_variant_for(&e, 1, 0) == PS_EXP_MINIMAL);
    assert(ps_exp_failed(&e, 1, 0, 1, 0) == PS_EXP_GIVE_UP);
    assert(!ps_exp_hevc_offered(&e));

    // What the video thread makes of it.
    assert(prospero::choose_capture_mode(2160, ps_exp_large_offered(&e)).resolution == 4);
    e = make(fw1360, 0, 0, 1);
    assert(prospero::choose_capture_mode(2160, ps_exp_large_offered(&e)).resolution == 6);

    std::puts("PASS: experimental switches (verified firmware unchanged, variant order, what goes off and when)");
    return 0;
}
