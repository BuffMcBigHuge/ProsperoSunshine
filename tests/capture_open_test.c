// SPDX-License-Identifier: GPL-3.0-only
// The Experimental switches through capture.c's real open path, against fake_avcap.h: which records go out in which order,
// what the host offers afterwards, and what host_diag.log gets.
#define PS_FIRST_FRAME_MS 20
#include "fake_avcap.h"
#include <assert.h>

#define START_FAILED ((int)0x80c78000)
#define INVALID ((int)0x81950001)

static const char *const marker = "/tmp/prospero_experimental_attempt";

static void setup(uint32_t firmware, int hevc, int hdr, int large, uint32_t codec, uint32_t ten_bit, uint32_t resolution)
{
    fake_firmware = firmware;
    fake_decide = NULL;
    fake_opens = 0;
    fake_log[0] = 0;
    unlink(marker);
    ps_capture_set_experimental(hevc, hdr, large, marker);
    ps_capture_set_codec(codec);
    ps_capture_set_ten_bit(ten_bit);
    ps_capture_set_video_mode(resolution, 1);
    ps_capture_set_opening_bitrate(20000000, 22000000);
    fake_log[0] = 0;
}

static int full_fields(const uint8_t *record) { return record[0x5c] == 9; }

static int open_and_close(void)
{
    ps_capture *session = NULL;
    const int rc = ps_capture_open_or_fall_back(&session);
    assert(!rc == (session != NULL));
    ps_capture_close(session);
    return rc;
}

static int logged(const char *text) { return strstr(fake_log, text) != NULL; }

static int start_needs_full(const char *step, const uint8_t *record)
{
    return !strcmp(step, "Start") && !full_fields(record) ? START_FAILED : 0;
}
static int open_always_fails(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "OpenVideo") ? INVALID : 0;
}
static int no_frame_unless_full(const char *step, const uint8_t *record)
{
    return !strcmp(step, "read") && !full_fields(record) ? AGAIN : 0;
}
static int read_fails(const char *step, const uint8_t *record)
{
    (void)record;
    return !strcmp(step, "read") ? START_FAILED : 0;
}
static int large_fails(const char *step, const uint8_t *record)
{
    uint32_t resolution;
    memcpy(&resolution, record + 0x18, sizeof resolution);
    return !strcmp(step, "OpenVideo") && resolution >= 5 ? INVALID : 0;
}
static int reads;
static int count_reads(const char *step, const uint8_t *record)
{
    (void)record;
    if (!strcmp(step, "read")) reads++;
    return 0;
}
static int marker_seen;
static int look_for_marker(const char *step, const uint8_t *record)
{
    (void)record;
    if (!strcmp(step, "OpenVideo")) marker_seen = access(marker, F_OK) == 0;
    return 0;
}

int main(void)
{
    // 13.60: the minimal variant fails at Start, the full one runs and stays.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    assert(ps_capture_hevc_allowed() && !ps_capture_hdr_allowed() && !ps_capture_large_modes_allowed());
    fake_decide = start_needs_full;
    assert(open_and_close() == 0);
    assert(fake_opens == 2);
    assert(logged("experimental attempt switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 variant=minimal\n"));
    assert(logged("experimental result=failed switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 "
                  "variant=minimal error=-1 rc=0x80c78000 step=Start\n"));
    assert(logged("experimental result=ok switches=hevc firmware=0x13600000 trial=hevc codec=hevc bits=8 mode=1920x1080 fps=60 "
                  "variant=full error=0 rc=0 step=none\n"));
    assert(!logged("disabled"));
    // The full variant on 13.60: what 13.00 keeps from 0x198 on sits 8 higher, also the encoder's memory.
    uint64_t at_shifted;
    memcpy(&at_shifted, fake_record + 0x198 + 8, sizeof at_shifted);
    assert(at_shifted == 0x50 && fake_record[0x1a8 + 8] == 0x11 && fake_record[0x1c8 + 8] == 0x22 && fake_record[0x228] == 0xff);
    uint32_t last_field;
    memcpy(&last_field, fake_record + 0x2c4 + 8, sizeof last_field);
    assert(last_field == 0x6e);
    fake_log[0] = 0;
    fake_opens = 0;
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && logged("trial=none") && logged("variant=full"));
    assert(ps_capture_hevc_allowed());

    // A session that passed its trial counts as current, or every user would reopen it.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    ps_capture *current = NULL;
    assert(ps_capture_open_or_fall_back(&current) == 0 && ps_capture_mode_current(current));
    ps_capture_close(current);

    // The minimal variant alone: nothing but cfg+0x24.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && !full_fields(fake_record) && fake_record[0x24] == 1);
    uint64_t memory_header;
    memcpy(&memory_header, fake_record + 0x198 + 8, sizeof memory_header);
    assert(memory_header != 0x50);
    assert(logged("result=ok") && logged("variant=minimal"));

    // Both variants fail: HEVC is off until the host restarts, and the next stream gets H.264 without any trial.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 2);
    assert(logged("variant=minimal error=-1 rc=0x81950001 step=OpenVideo") && logged("variant=full error=-1 rc=0x81950001 step=OpenVideo"));
    assert(logged("experimental disabled feature=hevc\n"));
    assert(!ps_capture_hevc_allowed() && atomic_load(&video_codec) == 0);
    fake_decide = NULL;
    fake_opens = 0;
    fake_log[0] = 0;
    assert(open_and_close() == 0);
    assert(fake_opens == 1 && logged("codec=h264") && logged("trial=none") && logged("variant=standard"));

    // No first frame in time, then a read that fails: the steps the records can't show.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    fake_decide = no_frame_unless_full;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && logged("variant=minimal error=-1 rc=0 step=first frame") && logged("variant=full error=0"));
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    fake_decide = read_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("rc=0x80c78000 step=first frame") && !ps_capture_hevc_allowed());

    // HDR fails and HEVC stays with its own switch; with the HDR switch alone both go, because HDR was all that offered HEVC.
    setup(0x13600000u, 1, 1, 0, 1, 1, 4);
    assert(ps_capture_hdr_allowed());
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(logged("trial=hdr codec=hevc bits=10") && logged("experimental disabled feature=hdr\n") && !logged("feature=hevc"));
    assert(!ps_capture_hdr_allowed() && ps_capture_hevc_allowed() && atomic_load(&video_ten_bit) == 0 && atomic_load(&video_codec) == 1);
    setup(0x13600000u, 0, 1, 0, 1, 1, 4);
    assert(ps_capture_hevc_allowed());
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(!ps_capture_hdr_allowed() && !ps_capture_hevc_allowed() && atomic_load(&video_codec) == 0);

    // A larger mode that fails gives 1080p in the same stream, and stays off.
    setup(0x13600000u, 0, 0, 1, 0, 0, 6);
    assert(ps_capture_large_modes_allowed());
    fake_decide = large_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && ps_capture_video_resolution() == 4);
    assert(logged("trial=large codec=h264 bits=8 mode=3840x2160") && logged("experimental disabled feature=large\n"));
    assert(!ps_capture_large_modes_allowed());

    // HEVC at 4K: both variants at 4K, then at 1080p, where the first one runs. The mode was at fault, not the codec.
    setup(0x13600000u, 1, 0, 1, 1, 0, 6);
    fake_decide = large_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 3 && ps_capture_video_resolution() == 4);
    assert(logged("mode=1920x1080 fps=60 variant=minimal error=0") && logged("feature=large") && !logged("feature=hevc"));
    assert(ps_capture_hevc_allowed() && !ps_capture_large_modes_allowed());

    // HEVC at 4K fails everywhere: HEVC is off, and 1440p and 4K are still unproven with H.264.
    setup(0x13600000u, 1, 0, 1, 1, 0, 6);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 4 && !ps_capture_hevc_allowed() && ps_capture_large_modes_allowed() && !logged("feature=large"));

    // Other 10.x: 13.00's fields are left out there anyway, so there is one variant to try.
    setup(0x10400000u, 1, 0, 0, 1, 0, 4);
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 1 && !ps_capture_hevc_allowed());

    // A capture in use says nothing about the function.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    ps_capture *held = NULL;
    assert(ps_capture_open(&held) == 0);
    assert(open_and_close() == PS_CAPTURE_BUSY);
    assert(ps_capture_hevc_allowed() && !logged("disabled") && logged("step=in use"));
    ps_capture_close(held);

    // The attempt under way is on file, and gone once it ended.
    setup(0x13600000u, 1, 0, 0, 1, 0, 4);
    marker_seen = 0;
    fake_decide = look_for_marker;
    assert(open_and_close() == 0);
    assert(marker_seen && access(marker, F_OK) != 0);

    // A host that went down inside an attempt: the next one says so and keeps the function off for its run.
    setup(0x13600000u, 1, 0, 1, 1, 0, 4);
    FILE *left = fopen(marker, "w");
    fputs("switches=hevc,large firmware=0x13600000 trial=hevc,large codec=hevc bits=8 mode=3840x2160 fps=60 variant=minimal", left);
    fclose(left);
    fake_log[0] = 0;
    ps_capture_set_experimental(1, 0, 1, marker);
    assert(logged("experimental result=failed switches=hevc,large firmware=0x13600000 trial=hevc,large codec=hevc bits=8 "
                  "mode=3840x2160 fps=60 variant=minimal error=0 rc=0 step=host went down\n"));
    assert(logged("feature=hevc") && logged("feature=large"));
    assert(!ps_capture_hevc_allowed() && !ps_capture_large_modes_allowed() && access(marker, F_OK) != 0);

    // Verified firmware: the switches change nothing, not even the first frame's check.
    setup(0x13000040u, 1, 1, 1, 1, 1, 6);
    assert(ps_capture_hevc_allowed() && ps_capture_hdr_allowed() && ps_capture_large_modes_allowed());
    reads = 0;
    fake_decide = count_reads;
    assert(open_and_close() == 0);
    assert(reads == 0 && fake_log[0] && !logged("experimental"));
    uint8_t with_switches[sizeof fake_record];
    memcpy(with_switches, fake_record, sizeof with_switches);
    setup(0x13000040u, 0, 0, 0, 1, 1, 6);
    assert(open_and_close() == 0);
    assert(!memcmp(with_switches, fake_record, sizeof with_switches));
    fake_log[0] = 0;
    ps_capture_set_experimental(1, 1, 1, marker);
    assert(logged("experimental switches=none set=hevc,hdr,large firmware=0x13000040\n"));

    // 10.20: HEVC and HDR are verified, 1440p and 4K are not.
    setup(0x10200006u, 1, 1, 1, 1, 0, 4);
    assert(ps_capture_hevc_allowed() && ps_capture_hdr_allowed() && ps_capture_large_modes_allowed());
    reads = 0;
    fake_decide = count_reads;
    assert(open_and_close() == 0);
    assert(reads == 0 && logged("switches=large") && logged("trial=none") && logged("variant=standard"));
    setup(0x10200006u, 0, 0, 0, 1, 0, 6);
    assert(!ps_capture_large_modes_allowed());

    // All off on untested firmware: what the host did before, without a line more, and a failure stays a plain failure.
    setup(0x13600000u, 0, 0, 0, 1, 0, 4);
    assert(!ps_capture_hevc_allowed() && !ps_capture_large_modes_allowed());
    fake_decide = open_always_fails;
    assert(open_and_close() == PS_CAPTURE_ERROR);
    assert(fake_opens == 1 && !logged("experimental"));
    setup(0x13600000u, 0, 0, 0, 0, 0, 6);
    fake_decide = large_fails;
    assert(open_and_close() == 0);
    assert(fake_opens == 2 && ps_capture_video_resolution() == 4 && !logged("experimental"));

    // Where 13.00's layout holds, the full variant is the record 13.00 has always sent; on 10.x the minimal one is.
    uint8_t standard[0x2e8], variant[0x2e8];
    setup(0x12400000u, 0, 0, 0, 1, 0, 4);
    memset(standard, 0, sizeof standard);
    memset(variant, 0, sizeof variant);
    configure(standard, 0x13c, PS_EXP_STANDARD);
    configure(variant, 0x13c, PS_EXP_FULL);
    assert(!memcmp(standard, variant, sizeof standard));
    setup(0x10400000u, 0, 0, 0, 1, 0, 4);
    memset(standard, 0, sizeof standard);
    memset(variant, 0, sizeof variant);
    configure(standard, 0x13c, PS_EXP_STANDARD);
    configure(variant, 0x13c, PS_EXP_MINIMAL);
    assert(!memcmp(standard, variant, sizeof standard));
    unlink(marker);

    puts("PASS: experimental switches through the open path (variants, first frame, fallback, off until restart, verified firmware)");
    return 0;
}
