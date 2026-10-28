// SPDX-License-Identifier: GPL-3.0-only
#ifndef PROSPERO_CAPTURE_H
#define PROSPERO_CAPTURE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ps_capture ps_capture;
enum ps_capture_error {
    PS_CAPTURE_ERROR = -1,
    PS_CAPTURE_BUSY = -2,
    PS_CAPTURE_FIRMWARE = -3,
    PS_CAPTURE_HDR = -4,
    PS_CAPTURE_REVOKED = -5
};

// One session per process. Fixed, measured FW 13.00 / AVC / 1080p / SDR configuration.
// On failure *out is NULL and any acquired resources have had cleanup attempted.
int ps_capture_open(ps_capture **out);
// Annex-B parameter sets; returns bytes written or a negative error.
int ps_capture_headers(ps_capture *session, void *destination, size_t capacity);
// Nonblocking: >0 encoded bytes, 0 means try again, <0 error/revocation.
// Descriptor bytes are copied unchanged; their timestamp fields remain unverified.
int ps_capture_read(ps_capture *session, void *destination, size_t capacity, uint8_t descriptor[0x70]);
// Stop, close, terminate, STOP, unregister. NULL is harmless; consumes the session.
int ps_capture_close(ps_capture *session);
#ifdef __cplusplus
}
#endif
#endif
