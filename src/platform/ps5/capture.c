// SPDX-License-Identifier: GPL-3.0-only
// Derived from ps5-mcp/payload/avcap_probe/avcap_probe.c, FW 13.00 research.
#include "capture.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#define RESOURCE 180
#define AGAIN ((int)0x81950002)
#define LOG(...) klog_printf("ProsperoSunshine: capture " __VA_ARGS__)
typedef int (*load_fn)(const char *, size_t, const void *, uint32_t, void *, int *);
typedef int (*handle_fn)(intptr_t);

struct ps_capture {
    intptr_t handle;
    int registered, requested, initialized, opened, started;
    int (*unregister)(int);
    int (*request)(int, const char *, void *, int);
    int (*terminate)(void);
    handle_fn stop, close;
    int (*headers)(intptr_t, void *);
    int (*read)(intptr_t, void *, size_t, void *, uint32_t);
    // Optional calls (docs/re/avcap2-exports.md), resolved once at open: kernel_dynlib_dlsym is not
    // safe to call from several threads at once, and the video, audio and control threads all use these.
    handle_fn request_keyframe, start;
    int (*set_bitrate)(intptr_t, void *);
    int (*open_audio)(intptr_t *, const void *, uint8_t);
    int (*read_audio)(intptr_t, void *, size_t, void *, uint32_t);
    // Audio channel (docs/re/avcap2-exports.md, R2); shares the video session's resource grant.
    intptr_t audio;
    int audio_opened, audio_started;
};

// Callback ABI currently has no verified instance context, so resource ownership
// is explicitly exclusive. The callback touches only process-lifetime atomics.
static atomic_flag owned = ATOMIC_FLAG_INIT;
static atomic_int granted;
static atomic_int revoked;
static int resource_event(int id, int event, const uint8_t *data)
{
    if (id == RESOURCE && event == 1 && data) atomic_store(&granted, data[0] == 1);
    if (id == RESOURCE && event == 2) {
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

static void configure(uint8_t *p, uint32_t mode)
{
    static const struct { size_t at; uint64_t size; } headers[] = {
        {0, 0x2e0}, {8, 0x28}, {0x30, 0x58}, {0x38, 0x18},
        {0x50, 0x10}, {0x88, 0x30}, {0xb8, 0xe0}, {0xc0, 0x20},
        {0xe0, 0x20}, {0x100, 0x20}, {0x120, 0x18}, {0x1e8, 0xf8},
        {0x1f0, 0x28}, {0x218, 0x88}, {0x2a8, 0x10}
    };
    memset(p, 0, 0x2e0);
    for (size_t i = 0; i < sizeof headers / sizeof headers[0]; i++) u64(p, headers[i].at, headers[i].size);
    u32(p, 0x10, 0);
    u32(p, 0x14, 2);
    u32(p, 0x18, 4);
    u32(p, 0x1c, 1);
    u32(p, 0x24, 0);
    u32(p, 0x28, RESOURCE);
    u32(p, 0x94, 30);
    u32(p, 0xc8, 6000000);
    u32(p, 0x2a0, mode);
    u32(p, 0x2a4, 2); // composited screen, including home menu
    u32(p, 0x2b0, 1);
    p[0x2b4] = 1;
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
    CLEANUP(s->initialized, s->terminate(), "Terminate");
    CLEANUP(s->requested, s->request(RESOURCE, "STOP", NULL, getpid()), "STOP");
    int unregistered = 1;
    if (s->registered) {
        rc = s->unregister(RESOURCE);
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
    if ((kernel_get_fw_version() & 0xffff0000u) != 0x13000000u) return PS_CAPTURE_FIRMWARE;
    if (atomic_flag_test_and_set(&owned)) return PS_CAPTURE_BUSY;
    ps_capture *s = calloc(1, sizeof *s);
    if (!s) { atomic_flag_clear(&owned); return PS_CAPTURE_ERROR; }
    atomic_store(&granted, 0);
    atomic_store(&revoked, 0);
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
    intptr_t base = kernel_dynlib_mapbase_addr(-1, ra);
    if (!base) goto fail;
    int (*reg)(int, void *, void *) = (void *)(base + 0xdb0);
    s->unregister = (void *)(base + 0xee0);
    s->request = (void *)(base + 0x10b0);
    int (*list)(int32_t *) = (void *)(base + 0x11f0);
    int32_t active[32] = {0};
    if (list(active)) goto fail;
    for (size_t i = 0; i < 32; i++) if (active[i] == RESOURCE) { error = PS_CAPTURE_BUSY; goto fail; }

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
    s->request_keyframe = (void *)SYM("sceAvcap2RequestKeyFrame");
    s->set_bitrate = (void *)SYM("sceAvcap2SetVideoBitrate");
    s->open_audio = (void *)SYM("sceAvcap2OpenAudio");
    s->read_audio = (void *)SYM("sceAvcap2ReadAudio");
#undef SYM
    if (!init || !mode || !open_video || !start || !s->terminate || !s->stop || !s->close || !s->headers || !s->read)
        goto fail;
    int rc = reg(RESOURCE, (void *)resource_event, NULL);
    LOG("register rc=%#x\n", rc);
    if (rc) goto fail;
    s->registered = 1;
    s->requested = 1; // STOP is attempted even if the RUNNING request reports failure.
    rc = s->request(RESOURCE, "RUNNING", NULL, getpid());
    LOG("RUNNING rc=%#x\n", rc);
    if (rc < 0) goto fail;
    for (int i = 0; i < 200 && !atomic_load(&granted) && !atomic_load(&revoked); i++) usleep(10000);
    if (!atomic_load(&granted) || atomic_load(&revoked)) goto fail;
    rc = init();
    LOG("Initialize rc=%#x\n", rc);
    if (rc) goto fail;
    s->initialized = 1;
    int output = 0;
    if (mode(&output)) goto fail;
    if (output & 3) { error = PS_CAPTURE_HDR; goto fail; }
    uint8_t config[0x2e0];
    configure(config, (uint32_t)output);
    rc = open_video(&s->handle, config, 0);
    LOG("OpenVideo rc=%#x\n", rc);
    if (rc) goto fail;
    s->opened = 1;
    rc = start(s->handle);
    LOG("Start rc=%#x\n", rc);
    if (rc) goto fail;
    s->started = 1;
    if (!atomic_load(&granted) || atomic_load(&revoked)) { error = PS_CAPTURE_REVOKED; goto fail; }
    *out = s;
    return 0;
fail:
    ps_capture_close(s);
    return error;
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
    for (size_t off = 0x10; off <= 0x120; off += 0x88) {
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
    return ps_capture_audio_open_ex(s, encoder, bitrate, frame_index, 2, RESOURCE, 0);
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
