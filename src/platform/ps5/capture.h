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
    PS_CAPTURE_VR = -4,
    PS_CAPTURE_REVOKED = -5
};

// Mode for the next ps_capture_open. resolution: 1 = 640x360, 2 = 960x540, 3 = 1280x720,
// 4 = 1920x1080 (default), 5 = 2560x1440, 6 = 3840x2160; timing: 0 = 30 fps, 1 = 60 fps (default).
int ps_capture_set_video_mode(uint32_t resolution, uint32_t timing);
// Codec for the next ps_capture_open: 0 = H.264 (default), 1 = HEVC.
int ps_capture_set_codec(uint32_t codec);
// 10 bit for the next ps_capture_open, HEVC only: Main 10, BT.2020, PQ (docs/re/hdr.md).
void ps_capture_set_ten_bit(uint32_t ten_bit);
// Whether HEVC was verified on this firmware: 13.00 and 10.20.
int ps_capture_hevc_verified(void);
// Whether 1440p and 2160p were verified on this firmware.
int ps_capture_large_modes_verified(void);
// The resolution set for the next open. After the system refused the last open of a mode above 1080p (seen in a title,
// the arbitrator logs "4K NG(4K120)"; other sources report OpenVideo refusing it when the console's output is smaller),
// ps_capture_fall_back_mode sets 1080p and returns 1; otherwise it changes nothing.
uint32_t ps_capture_video_resolution(void);
int ps_capture_fall_back_mode(void);
// Bitrate in the config of the next open, in bit/s. Default 6 Mbit/s and no peak. HEVC needs a peak. Raising
// the bitrate above this at runtime kills the channel (0x80c78000 on the next read).
void ps_capture_set_opening_bitrate(uint32_t average, uint32_t peak);
// Research, for other firmwares: the resource for the next open (ps_capture_set_video_mode sets the default for the
// mode, so call this after it), and one more 32-bit config field to write after all others (field 0 = none).
void ps_capture_set_resource(int id);
void ps_capture_override_config(size_t field, uint32_t value);
// Research: raw result of the last sceAvcap2OpenVideo, and sceAvcap2ChangeScanInResolution at runtime.
int ps_capture_last_open_rc(void);
int ps_capture_change_scan_in(ps_capture *session, uint32_t resolution);
// One session per process. Measured FW 13.00 / AVC / SDR configuration.
// On failure *out is NULL and any acquired resources have had cleanup attempted.
int ps_capture_open(ps_capture **out);
// Whether the session was opened with the mode set now. NULL counts as not current.
int ps_capture_mode_current(const ps_capture *session);
// Annex-B parameter sets, for HEVC in the order VPS, SPS, PPS; returns bytes written or a negative error.
int ps_capture_headers(ps_capture *session, void *destination, size_t capacity);
// Nonblocking: >0 encoded bytes, 0 means try again, <0 error/revocation.
// Descriptor bytes are copied unchanged; their timestamp fields remain unverified.
int ps_capture_read(ps_capture *session, void *destination, size_t capacity, uint8_t descriptor[0x70]);
// Same through sceAvcap2ReadVideo2, the only read that accepts HEVC. Its descriptor is laid out differently.
int ps_capture_read2(ps_capture *session, void *destination, size_t capacity, uint8_t descriptor[0x88]);
// Stop, close, terminate, STOP, unregister. NULL is harmless; consumes the session.
int ps_capture_close(ps_capture *session);

// Ask the encoder for an IDR as soon as possible (sceAvcap2RequestKeyFrame). 0 or error.
int ps_capture_request_keyframe(ps_capture *session);
// Change target and peak bitrate in bit/s at runtime; 0 keeps a value. 0 or error.
int ps_capture_set_bitrate(ps_capture *session, uint32_t average, uint32_t peak);
// Game audio over the same session. encoder: 0 raw PCM, 2 Opus (48 kHz stereo, bitrate in bit/s,
// frame_index 1 = 10 ms, 0 = 20 ms). One audio channel per session; ps_capture_close closes it too.
int ps_capture_audio_open(ps_capture *session, uint32_t encoder, uint32_t bitrate, uint32_t frame_index);
// Research variant: explicit purpose, config word 0x1C and open flag; returns the raw Avcap2 code on failure.
int ps_capture_audio_open_ex(ps_capture *session, uint32_t encoder, uint32_t bitrate, uint32_t frame_index,
                             uint32_t purpose, uint32_t field_1c, uint8_t flag);
// Nonblocking: >0 bytes of one packet, 0 means try again, <0 error. header receives the packet meta.
int ps_capture_audio_read(ps_capture *session, void *destination, size_t capacity, uint8_t header[0x10]);
int ps_capture_audio_close(ps_capture *session);
#ifdef __cplusplus
}
#endif
#endif
