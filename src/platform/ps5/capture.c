// SPDX-License-Identifier: GPL-3.0-only
// Derived from ps5-mcp/payload/avcap_probe/avcap_probe.c, FW 13.00 research.
#include "capture.h"
#include "diag.h"
#include "ra_resolve.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ps5/kernel.h>

// The resource the next open asks for, and the one of the session opened last, which events and the config refer to.
static _Atomic int resource_id = 180, session_resource = 180;
#define AGAIN ((int)0x81950002)
#define RESOURCE_NOT_ACTIVE ((int)0x81950008)
#define LOG(...) ps_diag_log("ProsperoSunshine: capture " __VA_ARGS__)
typedef int (*load_fn)(const char *, size_t, const void *, uint32_t, void *, int *);
typedef int (*handle_fn)(intptr_t);

struct ps_capture {
    intptr_t handle;
    int registered, requested, initialized, opened, started;
    int resource;  // close and audio must use what this session registered, whatever is set for the next open
    int (*unregister)(int);
    int (*request)(int, const char *, void *, int);
    int (*terminate)(void);
    handle_fn stop, close;
    int (*headers)(intptr_t, void *);
    int (*read)(intptr_t, void *, size_t, void *, uint32_t);
    int (*read2)(intptr_t, void *, size_t, void *, uint32_t);
    // Optional calls (docs/re/avcap2-exports.md), resolved once at open: kernel_dynlib_dlsym is not
    // safe to call from several threads at once, and the video, audio and control threads all use these.
    handle_fn request_keyframe, start;
    int (*set_bitrate)(intptr_t, void *);
    int (*open_audio)(intptr_t *, const void *, uint8_t);
    int (*read_audio)(intptr_t, void *, size_t, void *, uint32_t);
    // Audio channel (docs/re/avcap2-exports.md, R2); shares the video session's resource grant.
    intptr_t audio;
    int audio_opened, audio_started;
    // HEVC only: encoder memory reserved like Remote Play does.
    int (*free_memory)(void *, void *);
    uint8_t memory[0x40];
    int has_memory;
    uint32_t mode[6];  // what mode_snapshot() returned at open
};

// Callback ABI currently has no verified instance context, so resource ownership
// is explicitly exclusive. The callback touches only process-lifetime atomics.
static atomic_flag owned = ATOMIC_FLAG_INIT;
static atomic_int granted;
static atomic_int revoked;
static atomic_int refused;
static int resource_event(int id, int event, const uint8_t *data)
{
    if (id == atomic_load(&session_resource) && event == 1 && data) {
        atomic_store(&granted, data[0] == 1);
        if (data[0] != 1) atomic_store(&refused, 1);
    }
    if (id == atomic_load(&session_resource) && event == 2) {
        atomic_store(&revoked, 1);
        atomic_store(&granted, 0);
    }
    return 0;
}

static uint32_t module(load_fn load, const char *name)
{
    uint32_t handle = 0;
    if (kernel_dynlib_handle(-1, name, &handle) == 0 && handle) return handle;
    char path[128];
    int result = -1;
    snprintf(path, sizeof path, "/system/common/lib/%s", name);
    int rc = load(path, 0, NULL, 0, NULL, &result);
    LOG("load %s rc=%#x result=%#x\n", name, rc, result);
    if (kernel_dynlib_handle(-1, name, &handle)) return 0;
    return handle;
}

static void u32(uint8_t *p, size_t at, uint32_t value) { memcpy(p + at, &value, 4); }
static void u64(uint8_t *p, size_t at, uint64_t value) { memcpy(p + at, &value, 8); }

static _Atomic uint32_t video_resolution = 4, video_timing = 1, video_codec = 0, video_ten_bit = 0;
static _Atomic int last_open_rc = 0;

static _Atomic uint32_t opening_average = 6000000, opening_peak = 0;

void ps_capture_set_opening_bitrate(uint32_t average, uint32_t peak)
{
    atomic_store(&opening_average, average);
    atomic_store(&opening_peak, peak);
}

int ps_capture_set_codec(uint32_t codec)
{
    if (codec > 1) return PS_CAPTURE_ERROR;
    atomic_store(&video_codec, codec);
    return 0;
}

void ps_capture_set_ten_bit(uint32_t ten_bit) { atomic_store(&video_ten_bit, ten_bit != 0); }

int ps_capture_last_open_rc(void) { return atomic_load(&last_open_rc); }

int ps_capture_hevc_verified(void) { return (kernel_get_fw_version() & 0xffff0000u) == 0x13000000u; }

int ps_capture_large_modes_verified(void) { return (kernel_get_fw_version() & 0xffff0000u) == 0x13000000u; }

static void mode_snapshot(uint32_t mode[6])
{
    mode[5] = atomic_load(&video_ten_bit);
    mode[0] = atomic_load(&video_resolution);
    mode[1] = atomic_load(&video_timing);
    mode[2] = atomic_load(&video_codec);
    mode[3] = atomic_load(&opening_average);
    mode[4] = atomic_load(&opening_peak);
}

int ps_capture_mode_current(const ps_capture *s)
{
    uint32_t mode[6];
    mode_snapshot(mode);
    return s && !memcmp(mode, s->mode, sizeof mode);
}

int ps_capture_set_video_mode(uint32_t resolution, uint32_t timing)
{
    if (resolution < 1 || resolution > 6 || timing > 1) return PS_CAPTURE_ERROR;
    atomic_store(&video_resolution, resolution);
    atomic_store(&video_timing, timing);
    // Profile 180 grants scanin[2K], 182 scanin[4K] and more of the encoder (docs/re/avcap2-exports.md).
    atomic_store(&resource_id, resolution >= 5 ? 182 : 180);
    return 0;
}

uint32_t ps_capture_video_resolution(void) { return atomic_load(&video_resolution); }

int ps_capture_fall_back_mode(void)
{
    if (!atomic_load(&refused) || atomic_load(&video_resolution) < 5) return 0;
    ps_capture_set_video_mode(4, atomic_load(&video_timing));
    return 1;
}

static _Atomic size_t override_field;
static _Atomic uint32_t override_value;

void ps_capture_set_resource(int id) { atomic_store(&resource_id, id); }

void ps_capture_override_config(size_t field, uint32_t value)
{
    atomic_store(&override_value, value);
    atomic_store(&override_field, field);
}

// Firmware 13.60: the block at 0xb8 is 8 bytes larger, and what 13.00 keeps from 0x198 on sits 8 higher. Not measured
// here, taken from two public projects that run on it (docs/firmware-porting.md). HEVC isn't offered there, so its fields stay.
static size_t record_shift(void) { return (kernel_get_fw_version() & 0xffff0000u) == 0x13600000u ? 8 : 0; }

static void configure(uint8_t *p, uint32_t mode)
{
    static const struct { size_t at; uint64_t size; } headers[] = {
        {0, 0x2e0}, {8, 0x28}, {0x30, 0x58}, {0x38, 0x18},
        {0x50, 0x10}, {0x88, 0x30}, {0xb8, 0xe0}, {0xc0, 0x20},
        {0xe0, 0x20}, {0x100, 0x20}, {0x120, 0x18}, {0x1e8, 0xf8},
        {0x1f0, 0x28}, {0x218, 0x88}, {0x2a8, 0x10}
    };
    const size_t shift = record_shift();
    memset(p, 0, 0x2e0 + shift);
    for (size_t i = 0; i < sizeof headers / sizeof headers[0]; i++) {
        const size_t at = headers[i].at;
        u64(p, at >= 0x198 ? at + shift : at, at == 0 || at == 0xb8 ? headers[i].size + shift : headers[i].size);
    }
    u32(p, 0x10, 0);
    // Purpose: Remote Play. HEVC above 1080p with it needs the service's memory pools, which are missing (docs/re/hevc.md);
    // video chat has the same encoder preset and allocates per request.
    u32(p, 0x14, atomic_load(&video_codec) == 1 && atomic_load(&video_resolution) >= 5 ? 6 : 2);
    u32(p, 0x18, atomic_load(&video_resolution));
    u32(p, 0x1c, atomic_load(&video_timing));
    u32(p, 0x20, atomic_load(&video_ten_bit));
    u32(p, 0x24, atomic_load(&video_codec));
    u32(p, 0x28, atomic_load(&session_resource));
    const uint64_t peak = atomic_load(&opening_peak), fps = atomic_load(&video_timing) ? 60 : 30;
    // Frames between periodic IDRs. Clients ask for one after a loss, so these are only a safety net, and each one
    // costs bits and, with HEVC, skipped frames (docs/re/hevc.md).
    u32(p, 0x94, (uint32_t)(fps * 10));
    // Rate fields as SceRemotePlay 0x70130 computes them: average, peak, bits per frame.
    u32(p, 0xc8, atomic_load(&opening_average));
    u32(p, 0xcc, (uint32_t)peak);
    u32(p, 0xd0, (uint32_t)(peak * 1001 / (fps * 1000)));
    u32(p, 0x2a0 + shift, mode);
    u32(p, 0x2a4 + shift, 2); // composited screen, including home menu
    u32(p, 0x2b0 + shift, 1);
    p[0x2b4 + shift] = 1;
    if (atomic_load(&video_codec) == 1) {
        // Remote Play's values (SceRemotePlay 0x6f7f0). With only the fields it sets for HEVC alone,
        // sceAvcap2Start returns 0x80c78000. The set is not minimised.
        u32(p, 0x44, 1);
        u32(p, 0x48, 1);
        p[0x58] = 1;
        u32(p, 0x5c, 9);
        u64(p, 0x60, 0x18);
        p[0x68] = 1;
        u32(p, 0x98, 1);
        u32(p, 0xd4, 35);
        u32(p, 0xd8, 0x33);
        p[0x108] = 1;
        // Bytes a frame may cost before the encoder skips frames to catch up. Remote Play's one frame's share of the peak
        // lost a frame per IDR. These are the largest values measured to start; above them, Start returns 0x80c78000
        // (docs/re/hevc.md).
        const uint32_t resolution = atomic_load(&video_resolution);
        u32(p, 0x10c, resolution >= 6 ? 0x100000 : resolution == 5 ? 0xc0000 : 0x60000);
        p[0x128] = 1;
        u32(p, 0x12c, 0x19);
        u64(p, 0x188, 0x10);
        p[0x190] = 1;
        u32(p, 0x194, 1);
        memset(p + 0x220, 0xff, 0x80);
        u64(p, 0x2b8, 0x10);
        u32(p, 0x2c0, 1);
        u32(p, 0x2c4, 0x6e);
    }
    if (atomic_load(&override_field)) u32(p, atomic_load(&override_field), atomic_load(&override_value));
}

int ps_capture_close(ps_capture *s)
{
    if (!s) return 0;
    int failed = 0, rc;
#define CLEANUP(flag, expression, name) do { \
    if (flag) { rc = (expression); LOG(name " rc=%#x\n", rc); if (rc < 0) failed = 1; } \
} while (0)
    if (ps_capture_audio_close(s)) failed = 1;
    CLEANUP(s->started, s->stop(s->handle), "Stop");
    CLEANUP(s->opened, s->close(s->handle), "Close");
    CLEANUP(s->has_memory, s->free_memory(s->memory, s->memory + 0x20), "free encoder memory");
    CLEANUP(s->initialized, s->terminate(), "Terminate");
    CLEANUP(s->requested, s->request(s->resource, "STOP", NULL, getpid()), "STOP");
    int unregistered = 1;
    if (s->registered) {
        rc = s->unregister(s->resource);
        LOG("unregister rc=%#x\n", rc);
        if (rc) { failed = 1; unregistered = 0; }
    }
#undef CLEANUP
    free(s);
    // If unregister failed, keep the process guard closed: do not start a new
    // session while an old callback registration might still exist.
    if (unregistered && !failed) atomic_flag_clear(&owned);
    return failed ? PS_CAPTURE_ERROR : 0;
}

int ps_capture_open(ps_capture **out)
{
    if (!out) return PS_CAPTURE_ERROR;
    *out = NULL;
    if (atomic_flag_test_and_set(&owned)) return PS_CAPTURE_BUSY;
    ps_capture *s = calloc(1, sizeof *s);
    if (!s) { atomic_flag_clear(&owned); return PS_CAPTURE_ERROR; }
    // The app's report tells from "open start", "open ok" and "open failed" how this attempt ended, also
    // when a step takes the process down (no outcome line then).
    LOG("open start codec=%u resource=%d\n", atomic_load(&video_codec), atomic_load(&resource_id));
    s->resource = atomic_load(&resource_id);
    atomic_store(&session_resource, s->resource);
    const char *step = "system modules";
    atomic_store(&granted, 0);
    atomic_store(&revoked, 0);
    atomic_store(&refused, 0);
    int error = PS_CAPTURE_ERROR;
    load_fn load = NULL;
    const char *kernels[] = {"libkernel_web.sprx", "libkernel_sys.sprx", "libkernel.sprx"};
    for (size_t i = 0; i < sizeof kernels / sizeof kernels[0] && !load; i++) {
        uint32_t h = 0;
        if (!kernel_dynlib_handle(-1, kernels[i], &h) && h)
            load = (load_fn)kernel_dynlib_dlsym(-1, h, "sceKernelLoadStartModule");
    }
    if (!load || !module(load, "libSceIpmi.sprx")) goto fail;
    uint32_t av = module(load, "libSceAvcap2.sprx");
    uint32_t ra = module(load, "libSceResourceArbitrator.sprx");
    if (!av || !ra) goto fail;
    // Firmware gate: the four ResourceArbitrator calls are exported only under NIDs whose names are
    // unknown, so they are resolved by NID and checked against the offsets verified per firmware
    // (ra_resolve.c, docs/firmware-porting.md). Resolved once per process (opens are serialised by
    // `owned`); a refusal is final.
    static ps_ra_functions ra_calls;
    static int ra_state; // 0 not yet resolved, 1 resolved, -1 refused
    if (!ra_state) ra_state = ps_ra_resolve(ra, &ra_calls) ? -1 : 1;
    step = "ResourceArbitrator";
    if (ra_state < 0) { error = PS_CAPTURE_FIRMWARE; goto fail; }
    intptr_t base = kernel_dynlib_mapbase_addr(-1, ra);
    if (!base) goto fail;
    int (*reg)(int, void *, void *) = (void *)(base + ra_calls.offset[PS_RA_REGISTER]);
    s->unregister = (void *)(base + ra_calls.offset[PS_RA_UNREGISTER]);
    s->request = (void *)(base + ra_calls.offset[PS_RA_REQUEST]);
    int (*list)(int32_t *) = (void *)(base + ra_calls.offset[PS_RA_LIST]);
    int32_t active[32] = {0};
    step = "resource list";
    // Right after our own close the arbitrator can list the resource for a moment longer: a reopen failed on that.
    // Stay well below the 2 s a client waits for the stream.
    for (int attempt = 0;; attempt++) {
        if (list(active)) goto fail;
        int listed = 0;
        for (size_t i = 0; i < 32; i++) if (active[i] == s->resource) listed = 1;
        if (!listed) {
            if (attempt) LOG("resource free after %d ms\n", attempt * 20);
            break;
        }
        if (attempt == 40) { error = PS_CAPTURE_BUSY; goto fail; }
        usleep(20000);
    }

    step = "Avcap2 functions";
#define SYM(name) kernel_dynlib_dlsym(-1, av, name)
    int (*init)(void) = (void *)SYM("sceAvcap2Initialize");
    int (*mode)(int *) = (void *)SYM("sceAvcap2GetVideoOutMode");
    int (*open_video)(intptr_t *, const void *, uint8_t) = (void *)SYM("sceAvcap2OpenVideo");
    handle_fn start = s->start = (void *)SYM("sceAvcap2Start");
    s->terminate = (void *)SYM("sceAvcap2Terminate");
    s->stop = (void *)SYM("sceAvcap2Stop");
    s->close = (void *)SYM("sceAvcap2Close");
    s->headers = (void *)SYM("sceAvcap2GetNonVclNalUnitStream");
    s->read = (void *)SYM("sceAvcap2ReadVideo");
    s->read2 = (void *)SYM("sceAvcap2ReadVideo2");
    s->request_keyframe = (void *)SYM("sceAvcap2RequestKeyFrame");
    s->set_bitrate = (void *)SYM("sceAvcap2SetVideoBitrate");
    s->open_audio = (void *)SYM("sceAvcap2OpenAudio");
    s->read_audio = (void *)SYM("sceAvcap2ReadAudio");
#undef SYM
    if (!init || !mode || !open_video || !start || !s->terminate || !s->stop || !s->close || !s->headers || !s->read)
        goto fail;
    step = "register";
    int rc = reg(s->resource, (void *)resource_event, NULL);
    LOG("register rc=%#x\n", rc);
    if (rc) goto fail;
    s->registered = 1;
    s->requested = 1; // STOP is attempted even if the RUNNING request reports failure.
    step = "RUNNING";
    rc = s->request(s->resource, "RUNNING", NULL, getpid());
    LOG("RUNNING rc=%#x\n", rc);
    if (rc < 0) goto fail;
    for (int i = 0; i < 200 && !atomic_load(&granted) && !atomic_load(&revoked) && !atomic_load(&refused); i++) usleep(10000);
    if (!atomic_load(&granted) || atomic_load(&revoked)) goto fail;
    step = "Initialize";
    rc = init();
    LOG("Initialize rc=%#x\n", rc);
    if (rc) goto fail;
    s->initialized = 1;
    step = "video out mode";
    int output = 0;
    // While a title comes up the output has no mode (0) for about a second, and the service refuses a config whose mode isn't
    // the current one. Stay below the 2 s a client waits for the stream.
    for (int attempt = 0; attempt < 50; attempt++) {
        if (mode(&output)) goto fail;
        if (output) {
            if (attempt) LOG("video out mode appeared after %d ms\n", attempt * 20);
            break;
        }
        usleep(20000);
    }
    LOG("video out mode %#x\n", output);
    if (output & 3) { error = PS_CAPTURE_VR; goto fail; }
    uint8_t config[0x2e8];
    mode_snapshot(s->mode);
    configure(config, (uint32_t)output);
    if (atomic_load(&video_codec) == 1) {
        // Memory Mode 1 as Remote Play sets it up (SceRemotePlay 0x6fb83..0x6fc65).
        step = "encoder memory";
        int (*reserve)(const void *, void *, void *) = (void *)kernel_dynlib_resolve(-1, av, "yN0w4tWivqs");
        s->free_memory = (void *)kernel_dynlib_resolve(-1, av, "96UQhNnBlxc");
        if (!reserve || !s->free_memory) goto fail;
        uint8_t request[0x90] = {0};
        u64(request, 0, sizeof request);
        memcpy(request + 8, config + 8, 0x28);
        memcpy(request + 0x30, config + 0x30, 0x58);
        u64(config, 0x198, 0x50);
        u32(config, 0x1a0, 1);
        rc = reserve(request, config + 0x1a8, config + 0x1c8);
        LOG("encoder memory rc=%#x\n", rc);
        if (rc) { atomic_store(&last_open_rc, rc); goto fail; }
        memcpy(s->memory, config + 0x1a8, sizeof s->memory);
        s->has_memory = 1;
    }
    step = "OpenVideo";
    // The service refuses while it doesn't list our resource as active. Seen on firmware 10.40, which may just need a
    // moment; stay below the 2 s a client waits for the stream (Initial Ping Timeout). A larger mode gets the same code
    // when the console's output is too small for it, and needs the time left for the open at 1080p. It also answers this when
    // the output's mode changed since it was read, as while a title comes up, so that is read anew.
    const int repeats = atomic_load(&video_resolution) >= 5 ? 5 : 25;
    for (int attempt = 0;; attempt++) {
        rc = open_video(&s->handle, config, 0);
        if (rc != RESOURCE_NOT_ACTIVE || attempt == repeats || atomic_load(&revoked)) {
            if (attempt) LOG("OpenVideo repeated %d times\n", attempt);
            break;
        }
        usleep(40000);
        const int before = output;
        if (mode(&output) == 0 && output != before) {
            LOG("video out mode now %#x\n", output);
            u32(config, 0x2a0 + record_shift(), (uint32_t)output);
        }
    }
    atomic_store(&last_open_rc, rc);
    LOG("OpenVideo rc=%#x\n", rc);
    if (rc) atomic_store(&refused, 1);
    if (rc == RESOURCE_NOT_ACTIVE) {
        // For the app's report: is our resource listed, and which others are?
        int32_t now[32] = {0};
        const int list_rc = list(now);
        char ids[32 * 12] = "";
        size_t used = 0;
        for (size_t i = 0; i < 32; i++)
            if (now[i]) used += (size_t)snprintf(ids + used, sizeof ids - used, " %d", now[i]);
        LOG("active resources rc=%#x:%s\n", list_rc, ids);
    }
    if (rc) goto fail;
    s->opened = 1;
    step = "Start";
    rc = start(s->handle);
    LOG("Start rc=%#x\n", rc);
    if (rc) { atomic_store(&refused, 1); goto fail; }
    s->started = 1;
    if (!atomic_load(&granted) || atomic_load(&revoked)) { error = PS_CAPTURE_REVOKED; goto fail; }
    *out = s;
    LOG("open ok\n");
    return 0;
fail:
    LOG("open failed step=%s error=%d rc=%#x\n", step, error, atomic_load(&last_open_rc));
    ps_capture_close(s);
    return error;
}

int ps_capture_change_scan_in(ps_capture *s, uint32_t resolution)
{
    if (!s || !s->started) return PS_CAPTURE_ERROR;
    uint32_t h = 0;
    if (kernel_dynlib_handle(-1, "libSceAvcap2.sprx", &h) || !h) return PS_CAPTURE_ERROR;
    int (*change)(intptr_t, uint32_t) = (void *)kernel_dynlib_dlsym(-1, h, "sceAvcap2ChangeScanInResolution");
    return change ? change(s->handle, resolution) : PS_CAPTURE_ERROR;
}

int ps_capture_headers(ps_capture *s, void *destination, size_t capacity)
{
    if (!s || !destination || !s->started) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted) || atomic_load(&revoked)) return PS_CAPTURE_REVOKED;
    uint8_t nal[0x1a8] = {0};
    u64(nal, 0, sizeof nal);
    int rc = s->headers(s->handle, nal);
    if (rc) { LOG("headers rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    size_t used = 0;
    // The console lists HEVC's as SPS, PPS, VPS; decoders and moonlight-common-c want the VPS first.
    static const size_t order[][3] = {{0x10, 0x98, 0x120}, {0x120, 0x10, 0x98}};
    for (size_t i = 0; i < 3; i++) {
        const size_t off = order[s->mode[2] == 1][i];
        uint64_t n;
        memcpy(&n, nal + off, sizeof n);
        if (n > 0x80 || (n && (capacity < used || capacity - used < n + 4))) return PS_CAPTURE_ERROR;
        if (!n) continue;
        memcpy((uint8_t *)destination + used, "\0\0\0\1", 4);
        memcpy((uint8_t *)destination + used + 4, nal + off + 8, (size_t)n);
        used += (size_t)n + 4;
    }
    return (int)used;
}

int ps_capture_read(ps_capture *s, void *destination, size_t capacity, uint8_t descriptor[0x70])
{
    if (!s || !destination || !capacity || !descriptor || !s->started) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted) || atomic_load(&revoked)) return PS_CAPTURE_REVOKED;
    memset(descriptor, 0, 0x70);
    u64(descriptor, 0, 0x70);
    int rc = s->read(s->handle, destination, capacity, descriptor, 1);
    if (rc == AGAIN || rc == 0) return 0;
    if (rc < 0 || (size_t)rc > capacity) { LOG("ReadVideo rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return rc;
}

int ps_capture_read2(ps_capture *s, void *destination, size_t capacity, uint8_t descriptor[0x88])
{
    if (!s || !destination || !capacity || !descriptor || !s->started || !s->read2) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted) || atomic_load(&revoked)) return PS_CAPTURE_REVOKED;
    memset(descriptor, 0, 0x88);
    u64(descriptor, 0, 0x88);
    int rc = s->read2(s->handle, destination, capacity, descriptor, 1);
    if (rc == AGAIN || rc == 0) return 0;
    if (rc < 0 || (size_t)rc > capacity) { LOG("ReadVideo2 rc=%#x\n", rc); return rc < 0 ? rc : PS_CAPTURE_ERROR; }
    return rc;
}

int ps_capture_request_keyframe(ps_capture *s)
{
    if (!s || !s->started || !s->request_keyframe) return PS_CAPTURE_ERROR;
    int rc = s->request_keyframe(s->handle);
    if (rc < 0) { LOG("RequestKeyFrame rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return 0;
}

int ps_capture_set_bitrate(ps_capture *s, uint32_t average, uint32_t peak)
{
    if (!s || !s->started || !s->set_bitrate) return PS_CAPTURE_ERROR;
    uint8_t request[0x10] = {0};
    u64(request, 0, sizeof request);
    u32(request, 8, average);
    u32(request, 0xc, peak);
    int rc = s->set_bitrate(s->handle, request);
    if (rc < 0) { LOG("SetVideoBitrate rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return 0;
}

int ps_capture_audio_open(ps_capture *s, uint32_t encoder, uint32_t bitrate, uint32_t frame_index)
{
    // cfg+0x1C is the ResourceArbitrator id the server checks against the active list, like
    // OpenVideo's cfg+0x28 (docs/re/avcap2-exports.md, R2-Nachtrag).
    return ps_capture_audio_open_ex(s, encoder, bitrate, frame_index, 2, s ? (uint32_t)s->resource : 0, 0);
}

int ps_capture_audio_open_ex(ps_capture *s, uint32_t encoder, uint32_t bitrate, uint32_t frame_index,
                             uint32_t purpose, uint32_t field_1c, uint8_t flag)
{
    if (!s || !s->started || s->audio_opened || !s->open_audio) return PS_CAPTURE_ERROR;
    uint8_t config[0x38] = {0};
    u64(config, 0, sizeof config);
    u64(config, 8, 0x18);
    u32(config, 0x10, 0);
    u32(config, 0x14, purpose);  // 2 Remoteplay: the full game-audio mix
    u32(config, 0x18, encoder);
    u32(config, 0x1c, field_1c);
    u64(config, 0x20, encoder == 2 ? 0x18 : 0x10);
    u32(config, 0x28, bitrate);
    u32(config, 0x30, frame_index);
    int rc = s->open_audio(&s->audio, config, flag);
    LOG("OpenAudio encoder=%u purpose=%u 1c=%#x flag=%u rc=%#x\n", encoder, purpose, field_1c, flag, rc);
    if (rc) return rc < 0 ? rc : PS_CAPTURE_ERROR;
    s->audio_opened = 1;
    rc = s->start(s->audio);
    LOG("audio Start rc=%#x\n", rc);
    if (rc) { ps_capture_audio_close(s); return PS_CAPTURE_ERROR; }
    s->audio_started = 1;
    return 0;
}

int ps_capture_audio_read(ps_capture *s, void *destination, size_t capacity, uint8_t header[0x10])
{
    if (!s || !s->audio_started || !s->read_audio || !destination || !header) return PS_CAPTURE_ERROR;
    if (!atomic_load(&granted) || atomic_load(&revoked)) return PS_CAPTURE_REVOKED;
    memset(header, 0, 0x10);
    u64(header, 0, 0x10);
    int rc = s->read_audio(s->audio, destination, capacity, header, 1);
    if (rc == AGAIN || rc == 0) return 0;
    if (rc < 0 || (size_t)rc > capacity) { LOG("ReadAudio rc=%#x\n", rc); return PS_CAPTURE_ERROR; }
    return rc;
}

int ps_capture_audio_close(ps_capture *s)
{
    if (!s) return 0;
    int failed = 0, rc;
    if (s->audio_started) {
        rc = s->stop(s->audio);
        LOG("audio Stop rc=%#x\n", rc);
        failed |= rc < 0;
    }
    if (s->audio_opened) {
        rc = s->close(s->audio);
        LOG("audio Close rc=%#x\n", rc);
        failed |= rc < 0;
    }
    s->audio_started = s->audio_opened = 0;
    s->audio = 0;
    return failed ? PS_CAPTURE_ERROR : 0;
}
