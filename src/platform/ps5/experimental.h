// SPDX-License-Identifier: GPL-3.0-only
// The app's Experimental switches: what they unlock on firmware nobody verified that on, and what follows from an attempt.
// Portable, tested in tests/experimental_test.cpp; capture.c holds the one instance. Background: docs/firmware-porting.md.
//
// A switch counts only where the function isn't verified. Each codec and bit depth tries its variants in order until one
// delivers a frame, and that one stays. When all fail, a mode above 1080p falls back to 1080p and the variants start over.
// When 1080p fails too, the function is off until the host restarts: the client's stream ends, because it set up its
// decoder for this codec, and its next stream gets what the host offers then.
#ifndef PROSPERO_EXPERIMENTAL_H
#define PROSPERO_EXPERIMENTAL_H
#include <stdint.h>
#include <string.h>

enum ps_exp_variant {
    PS_EXP_STANDARD,  // what the host does without a switch
    PS_EXP_MINIMAL,   // HEVC with cfg+0x24 alone, as on 10.20
    PS_EXP_FULL,      // HEVC with 13.00's Remote Play fields and encoder memory
};

enum ps_exp_next {
    PS_EXP_GIVE_UP,       // the open has failed
    PS_EXP_NEXT_VARIANT,  // open again with the next variant
    PS_EXP_FALL_BACK,     // open again at 1080p
};

typedef struct {
    uint8_t hevc, hdr, large;                         // switches that count on this firmware
    uint8_t hevc_failed, hdr_failed, large_failed;    // until the host restarts
    uint8_t large_confirmed, large_pending;           // pending: a larger mode failed, and 1080p has yet to show that it was the mode
    uint8_t last_variant;
    struct {
        uint8_t variant, confirmed;
    } trial[2];  // by bit depth: 8 and 10 bit
} ps_experiment;

static inline int ps_exp_hevc_verified(uint32_t firmware)
{
    const uint32_t version = firmware & 0xffff0000u;
    return version == 0x13000000u || version == 0x10200000u;
}

static inline int ps_exp_large_verified(uint32_t firmware) { return (firmware & 0xffff0000u) == 0x13000000u; }

static inline void ps_exp_init(ps_experiment *e, uint32_t firmware, int hevc, int hdr, int large)
{
    memset(e, 0, sizeof *e);
    e->hevc = hevc && !ps_exp_hevc_verified(firmware);
    e->hdr = hdr && !ps_exp_hevc_verified(firmware);
    e->large = large && !ps_exp_large_verified(firmware);
    // 10.x's validator refuses 13.00's inner fields (docs/re/hevc.md), and configure() leaves them out there: both are the same.
    e->last_variant = (firmware & 0xff000000u) == 0x10000000u ? PS_EXP_MINIMAL : PS_EXP_FULL;
    e->trial[0].variant = e->trial[1].variant = PS_EXP_MINIMAL;
}

static inline int ps_exp_any(const ps_experiment *e) { return e->hevc || e->hdr || e->large; }

// What a switch adds to what the firmware has anyway. HDR is HEVC at 10 bit and can't be offered without it.
static inline int ps_exp_hevc_offered(const ps_experiment *e)
{
    return (e->hevc || (e->hdr && !e->hdr_failed)) && !e->hevc_failed;
}
static inline int ps_exp_hdr_offered(const ps_experiment *e) { return e->hdr && !e->hdr_failed && !e->hevc_failed; }
static inline int ps_exp_large_offered(const ps_experiment *e) { return e->large && !e->large_failed; }

static inline int ps_exp_codec_on_trial(const ps_experiment *e, uint32_t codec, uint32_t ten_bit)
{
    return codec == 1 && (ten_bit ? ps_exp_hdr_offered(e) : ps_exp_hevc_offered(e));
}

static inline int ps_exp_variant_for(const ps_experiment *e, uint32_t codec, uint32_t ten_bit)
{
    return ps_exp_codec_on_trial(e, codec, ten_bit) ? e->trial[ten_bit != 0].variant : PS_EXP_STANDARD;
}

// Whether this open still has to show that it works, so the first frame counts as a step of it.
static inline int ps_exp_unproven_codec(const ps_experiment *e, uint32_t codec, uint32_t ten_bit)
{
    return ps_exp_codec_on_trial(e, codec, ten_bit) && !e->trial[ten_bit != 0].confirmed;
}
static inline int ps_exp_unproven_mode(const ps_experiment *e, uint32_t resolution)
{
    return resolution >= 5 && ps_exp_large_offered(e) && !e->large_confirmed;
}

static inline void ps_exp_succeeded(ps_experiment *e, uint32_t codec, uint32_t ten_bit, uint32_t resolution)
{
    if (ps_exp_codec_on_trial(e, codec, ten_bit)) e->trial[ten_bit != 0].confirmed = 1;
    if (resolution >= 5 && e->large) e->large_confirmed = 1;
    if (e->large_pending && resolution < 5) e->large_failed = 1;
    e->large_pending = 0;
}

// `attributable`: the failure came from the encoder's steps, not from the capture being in use or the system refusing the
// resource, which says nothing about the function. `can_fall_back`: ps_capture_fall_back_mode would act.
static inline enum ps_exp_next ps_exp_failed(ps_experiment *e, uint32_t codec, uint32_t ten_bit, int attributable, int can_fall_back)
{
    const int bits = ten_bit != 0;
    const int trial = attributable && ps_exp_unproven_codec(e, codec, ten_bit);
    if (trial && e->trial[bits].variant < e->last_variant) {
        e->trial[bits].variant++;
        return PS_EXP_NEXT_VARIANT;
    }
    if (can_fall_back) {
        if (trial) e->trial[bits].variant = PS_EXP_MINIMAL;
        if (attributable && e->large && !e->large_confirmed) e->large_pending = 1;
        return PS_EXP_FALL_BACK;
    }
    if (trial) {
        if (bits)
            e->hdr_failed = 1;
        else
            e->hevc_failed = 1;
    }
    e->large_pending = 0;
    return PS_EXP_GIVE_UP;
}

#endif
