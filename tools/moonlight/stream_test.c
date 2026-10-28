// SPDX-License-Identifier: GPL-3.0-only
//
// Moonlight-compatible streaming smoke test client built on moonlight-common-c.
//
// moonlight-common-c does not speak HTTP; the /launch request is made by
// pair.py, which prints the RTSP session URL and the rikey/rikeyid that this
// program needs.  This program performs the RTSP handshake, starts the video,
// audio, control and input streams, dumps the H.264 stream to a file, counts
// frames / IDRs / Opus packets, sends a few controller events and requests an
// IDR frame, then stops.
//
// Usage:
//   stream_test --addr <console-ip> --rtsp-url rtsp://<console-ip>:48010 \
//       --rikey <32 hex> --rikeyid <int> --appversion 7.1.431.-1 \
//       --gfe 3.23.0.74 --codec-mode 1 --out out/moonlight-test.h264 \
//       [--duration 20] [--bitrate 6000] [--audio-out file.s16] [--no-encryption]

#include <getopt.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <opus_multistream.h>

#include "Limelight.h"

static FILE *g_out = NULL;
static FILE *g_audio_out = NULL;

static uint64_t g_t0_ms;

static uint64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void tlog(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("[%7.3f] ", (now_ms() - g_t0_ms) / 1000.0);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
}

// ---------------------------------------------------------------- video

static struct {
  uint64_t frames, idrs, bytes, before_first_idr, gaps, missing;
  int last_frame_number;
  int have_last;
  uint64_t first_ms, last_ms;
  uint64_t first_pts, last_pts;
  uint32_t first_rtp, last_rtp;
  uint64_t max_gap_ms;
  int setup_format, setup_w, setup_h, setup_fps;
  int sps_seen, pps_seen;
  uint64_t sec_frames, sec_bytes;
} V;

static int vsetup(int videoFormat, int width, int height, int redrawRate, void *ctx, int drFlags) {
  (void) ctx;
  (void) drFlags;
  V.setup_format = videoFormat;
  V.setup_w = width;
  V.setup_h = height;
  V.setup_fps = redrawRate;
  tlog("video setup: format=0x%x %dx%d @%d\n", videoFormat, width, height, redrawRate);
  return 0;
}
static void vstart(void) { tlog("video start\n"); }
static void vstop(void) { tlog("video stop\n"); }
static void vcleanup(void) { tlog("video cleanup\n"); }

static int vsubmit(PDECODE_UNIT du) {
  uint64_t t = now_ms();
  if (V.frames == 0) {
    V.first_ms = t;
    V.first_pts = du->presentationTimeUs;
    V.first_rtp = du->rtpTimestamp;
  } else if (t - V.last_ms > V.max_gap_ms) {
    V.max_gap_ms = t - V.last_ms;
  }
  V.last_ms = t;
  V.last_pts = du->presentationTimeUs;
  V.last_rtp = du->rtpTimestamp;

  if (V.have_last && du->frameNumber != V.last_frame_number + 1) {
    V.gaps++;
    V.missing += (uint64_t) (du->frameNumber - V.last_frame_number - 1);
  }
  V.last_frame_number = du->frameNumber;
  V.have_last = 1;

  int is_idr = du->frameType == FRAME_TYPE_IDR;
  if (is_idr) {
    V.idrs++;
  } else if (V.idrs == 0) {
    V.before_first_idr++;
  }
  size_t total = 0;
  for (PLENTRY e = du->bufferList; e; e = e->next) {
    if (e->bufferType == BUFFER_TYPE_SPS) V.sps_seen++;
    if (e->bufferType == BUFFER_TYPE_PPS) V.pps_seen++;
    if (g_out) fwrite(e->data, 1, (size_t) e->length, g_out);
    total += (size_t) e->length;
  }
  V.frames++;
  V.bytes += total;
  V.sec_frames++;
  V.sec_bytes += total;
  if (is_idr) {
    tlog("IDR frame #%d (%zu bytes, hostLatency=%.1fms)\n", du->frameNumber, total,
         du->frameHostProcessingLatency / 10.0);
  }
  return DR_OK;
}

// ---------------------------------------------------------------- audio

static struct {
  uint64_t packets, bytes, decoded_samples, decode_errors;
  double sumsq;
  double peak;
  OpusMSDecoder *dec;
  int channels, spf, rate;
  uint64_t sec_packets;
} A;

static int ainit(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION oc, void *ctx, int arFlags) {
  (void) ctx;
  (void) arFlags;
  int err = 0;
  A.channels = oc->channelCount;
  A.spf = oc->samplesPerFrame;
  A.rate = oc->sampleRate;
  tlog("audio init: config=0x%x rate=%d channels=%d streams=%d coupled=%d samplesPerFrame=%d\n",
       audioConfiguration, oc->sampleRate, oc->channelCount, oc->streams, oc->coupledStreams,
       oc->samplesPerFrame);
  A.dec = opus_multistream_decoder_create(oc->sampleRate, oc->channelCount, oc->streams, oc->coupledStreams,
                                          oc->mapping, &err);
  if (!A.dec) {
    tlog("opus_multistream_decoder_create failed: %s\n", opus_strerror(err));
    return -1;
  }
  return 0;
}
static void astart(void) { tlog("audio start\n"); }
static void astop(void) { tlog("audio stop\n"); }
static void acleanup(void) {
  tlog("audio cleanup\n");
  if (A.dec) opus_multistream_decoder_destroy(A.dec);
  A.dec = NULL;
}
static void adecode(char *data, int len) {
  A.packets++;
  A.sec_packets++;
  A.bytes += (uint64_t) len;
  if (!A.dec) return;
  int16_t pcm[AUDIO_CONFIGURATION_MAX_CHANNEL_COUNT * 960];
  int n = opus_multistream_decode(A.dec, (const unsigned char *) data, len, pcm, 960, 0);
  if (n < 0) {
    A.decode_errors++;
    return;
  }
  size_t cnt = (size_t) n * (size_t) A.channels;
  for (size_t i = 0; i < cnt; i++) {
    double s = pcm[i] / 32768.0;
    A.sumsq += s * s;
    if (fabs(s) > A.peak) A.peak = fabs(s);
  }
  A.decoded_samples += cnt;
  if (g_audio_out) fwrite(pcm, sizeof(int16_t), cnt, g_audio_out);
}

// ---------------------------------------------------------------- connection

static int g_failed_stage = -1;
static int g_failed_code = 0;
static int g_terminated = 0;
static int g_term_code = 0;

static void cl_stage_start(int s) { tlog("stage starting: %s\n", LiGetStageName(s)); }
static void cl_stage_done(int s) { tlog("stage complete: %s\n", LiGetStageName(s)); }
static void cl_stage_failed(int s, int code) {
  g_failed_stage = s;
  g_failed_code = code;
  tlog("STAGE FAILED: %s (stage %d) errorCode=%d (0x%x)\n", LiGetStageName(s), s, code, (unsigned) code);
}
static void cl_started(void) { tlog("connection started\n"); }
static void cl_terminated(int code) {
  g_terminated = 1;
  g_term_code = code;
  tlog("CONNECTION TERMINATED errorCode=%d (0x%x)\n", code, (unsigned) code);
}
static void cl_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  printf("[%7.3f]   lc: ", (now_ms() - g_t0_ms) / 1000.0);
  vprintf(fmt, ap);
  va_end(ap);
  fflush(stdout);
}
static void cl_rumble(unsigned short n, unsigned short lo, unsigned short hi) {
  tlog("rumble pad=%u low=%u high=%u\n", n, lo, hi);
}
static void cl_status(int st) { tlog("connection status: %s\n", st == CONN_STATUS_OKAY ? "OKAY" : "POOR"); }
static void cl_hdr(bool on) { tlog("hdr mode: %d\n", (int) on); }
static void cl_rumble_trig(uint16_t n, uint16_t l, uint16_t r) { tlog("rumble triggers pad=%u l=%u r=%u\n", n, l, r); }
static void cl_motion(uint16_t n, uint8_t type, uint16_t hz) { tlog("motion state pad=%u type=%u hz=%u\n", n, type, hz); }
static void cl_led(uint16_t n, uint8_t r, uint8_t g, uint8_t b) { tlog("led pad=%u rgb=%u,%u,%u\n", n, r, g, b); }
static void cl_adaptive(uint16_t n, uint8_t flags, uint8_t tl, uint8_t tr, uint8_t *l, uint8_t *r) {
  (void) l;
  (void) r;
  tlog("adaptive triggers pad=%u flags=0x%x types=%u/%u\n", n, flags, tl, tr);
}

// ---------------------------------------------------------------- helpers

static int hex2bin(const char *hex, uint8_t *out, size_t n) {
  if (strlen(hex) != n * 2) return -1;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1) return -1;
    out[i] = (uint8_t) v;
  }
  return 0;
}

static void sleep_ms(int ms) { usleep((useconds_t) ms * 1000); }

static void pad_event(int buttons, int lt, int rt, int lx, int ly, int rx, int ry, const char *what) {
  int r = LiSendMultiControllerEvent(0, 0x1, buttons, (unsigned char) lt, (unsigned char) rt, (short) lx,
                                     (short) ly, (short) rx, (short) ry);
  tlog("input: %s -> %d\n", what, r);
}

int main(int argc, char **argv) {
  const char *addr = NULL, *rtsp = NULL, *rikey = NULL, *appver = "7.1.431.-1", *gfe = "3.23.0.74";
  const char *outp = "moonlight-test.h264", *aout = NULL;
  long rikeyid = 0;
  int codec_mode = SCM_H264, duration = 20, bitrate = 6000, no_enc = 0, w = 1920, h = 1080, fps = 60;
  int idr_at = -1;

  static const struct option opts[] = {
    {"addr", 1, 0, 'a'},     {"rtsp-url", 1, 0, 'r'},   {"rikey", 1, 0, 'k'},    {"rikeyid", 1, 0, 'i'},
    {"appversion", 1, 0, 'v'}, {"gfe", 1, 0, 'g'},      {"codec-mode", 1, 0, 'c'}, {"out", 1, 0, 'o'},
    {"audio-out", 1, 0, 'A'}, {"duration", 1, 0, 'd'},  {"bitrate", 1, 0, 'b'},  {"no-encryption", 0, 0, 'n'},
    {"width", 1, 0, 'W'},    {"height", 1, 0, 'H'},     {"fps", 1, 0, 'f'},      {"idr-at", 1, 0, 'I'},
    {0, 0, 0, 0}};
  int c;
  while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
    switch (c) {
      case 'a': addr = optarg; break;
      case 'r': rtsp = optarg; break;
      case 'k': rikey = optarg; break;
      case 'i': rikeyid = strtol(optarg, NULL, 10); break;
      case 'v': appver = optarg; break;
      case 'g': gfe = optarg; break;
      case 'c': codec_mode = (int) strtol(optarg, NULL, 0); break;
      case 'o': outp = optarg; break;
      case 'A': aout = optarg; break;
      case 'd': duration = atoi(optarg); break;
      case 'b': bitrate = atoi(optarg); break;
      case 'n': no_enc = 1; break;
      case 'W': w = atoi(optarg); break;
      case 'H': h = atoi(optarg); break;
      case 'f': fps = atoi(optarg); break;
      case 'I': idr_at = atoi(optarg); break;
      default: return 2;
    }
  }
  if (!addr || !rtsp || !rikey) {
    fprintf(stderr, "usage: %s --addr A --rtsp-url URL --rikey HEX32 --rikeyid N [options]\n", argv[0]);
    return 2;
  }
  if (idr_at < 0) idr_at = duration / 2;
  g_t0_ms = now_ms();

  STREAM_CONFIGURATION sc;
  LiInitializeStreamConfiguration(&sc);
  sc.width = w;
  sc.height = h;
  sc.fps = fps;
  sc.bitrate = bitrate;
  sc.packetSize = 1024;
  sc.streamingRemotely = STREAM_CFG_LOCAL;
  sc.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
  sc.supportedVideoFormats = VIDEO_FORMAT_H264;
  sc.clientRefreshRateX100 = fps * 100;
  sc.colorSpace = COLORSPACE_REC_709;
  sc.colorRange = COLOR_RANGE_LIMITED;
  sc.encryptionFlags = no_enc ? ENCFLG_NONE : ENCFLG_ALL;
  if (hex2bin(rikey, (uint8_t *) sc.remoteInputAesKey, 16)) {
    fprintf(stderr, "rikey must be 32 hex chars\n");
    return 2;
  }
  // The IV is the big-endian rikeyid in the first four bytes (see nvhttp.cpp make_launch_session).
  uint32_t idbe = (uint32_t) rikeyid;
  sc.remoteInputAesIv[0] = (char) (idbe >> 24);
  sc.remoteInputAesIv[1] = (char) (idbe >> 16);
  sc.remoteInputAesIv[2] = (char) (idbe >> 8);
  sc.remoteInputAesIv[3] = (char) idbe;

  SERVER_INFORMATION si;
  LiInitializeServerInformation(&si);
  si.address = addr;
  si.serverInfoAppVersion = appver;
  si.serverInfoGfeVersion = gfe;
  si.rtspSessionUrl = rtsp;
  si.serverCodecModeSupport = codec_mode;

  CONNECTION_LISTENER_CALLBACKS cl;
  LiInitializeConnectionCallbacks(&cl);
  cl.stageStarting = cl_stage_start;
  cl.stageComplete = cl_stage_done;
  cl.stageFailed = cl_stage_failed;
  cl.connectionStarted = cl_started;
  cl.connectionTerminated = cl_terminated;
  cl.logMessage = cl_log;
  cl.rumble = cl_rumble;
  cl.connectionStatusUpdate = cl_status;
  cl.setHdrMode = cl_hdr;
  cl.rumbleTriggers = cl_rumble_trig;
  cl.setMotionEventState = cl_motion;
  cl.setControllerLED = cl_led;
  cl.setAdaptiveTriggers = cl_adaptive;

  DECODER_RENDERER_CALLBACKS dr;
  LiInitializeVideoCallbacks(&dr);
  dr.setup = vsetup;
  dr.start = vstart;
  dr.stop = vstop;
  dr.cleanup = vcleanup;
  dr.submitDecodeUnit = vsubmit;
  dr.capabilities = 0;

  AUDIO_RENDERER_CALLBACKS ar;
  LiInitializeAudioCallbacks(&ar);
  ar.init = ainit;
  ar.start = astart;
  ar.stop = astop;
  ar.cleanup = acleanup;
  ar.decodeAndPlaySample = adecode;
  ar.capabilities = CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;

  g_out = fopen(outp, "wb");
  if (!g_out) {
    perror(outp);
    return 2;
  }
  if (aout) g_audio_out = fopen(aout, "wb");

  tlog("connecting: addr=%s rtsp=%s %dx%d@%d %dkbps enc=0x%x codecMode=0x%x appversion=%s gfe=%s\n", addr, rtsp, w,
       h, fps, bitrate, (unsigned) sc.encryptionFlags, codec_mode, appver, gfe);
  uint64_t t_start = now_ms();
  int rc = LiStartConnection(&si, &sc, &cl, &dr, &ar, NULL, 0, NULL, 0);
  uint64_t t_connected = now_ms();
  tlog("LiStartConnection returned %d after %.2fs\n", rc, (t_connected - t_start) / 1000.0);

  int exit_code = 0;
  if (rc != 0) {
    tlog("RESULT: connection FAILED at stage \"%s\" (stage %d), errorCode=%d\n",
         g_failed_stage >= 0 ? LiGetStageName(g_failed_stage) : "?", g_failed_stage, g_failed_code);
    exit_code = 3;
  } else {
    uint32_t ff = LiGetHostFeatureFlags();
    tlog("host feature flags: 0x%x\n", ff);
    uint64_t run_start = now_ms();
    int sent_arrival = 0, step = 0, sent_idr = 0;
    int last_sec = -1;
    while (!g_terminated) {
      double el = (now_ms() - run_start) / 1000.0;
      if (el >= duration) break;
      int sec = (int) el;
      if (sec != last_sec) {
        last_sec = sec;
        uint32_t rtt = 0, var = 0;
        LiGetEstimatedRttInfo(&rtt, &var);
        tlog("t=%2ds video: %llu frames (+%llu, %llu B/s) idr=%llu | audio: %llu pkts (+%llu) | rtt=%ums\n", sec,
             (unsigned long long) V.frames, (unsigned long long) V.sec_frames, (unsigned long long) V.sec_bytes,
             (unsigned long long) V.idrs, (unsigned long long) A.packets, (unsigned long long) A.sec_packets, rtt);
        V.sec_frames = V.sec_bytes = 0;
        A.sec_packets = 0;
      }
      // scripted input
      if (!sent_arrival && el >= 2.0) {
        int r = LiSendControllerArrivalEvent(0, 0x1, LI_CTYPE_PS, 0x3FFFF | 0, LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE);
        tlog("input: controller arrival (pad 0, PS) -> %d\n", r);
        sent_arrival = 1;
      }
      if (step == 0 && el >= 3.0) { pad_event(0, 0, 0, 0, 0, 0, 0, "neutral state"); step++; }
      if (step == 1 && el >= 4.0) { pad_event(A_FLAG, 0, 0, 0, 0, 0, 0, "A press"); step++; }
      if (step == 2 && el >= 4.3) { pad_event(0, 0, 0, 0, 0, 0, 0, "A release"); step++; }
      if (step == 3 && el >= 5.0) { pad_event(0, 0, 0, 20000, 0, 0, 0, "left stick x=+20000"); step++; }
      if (step == 4 && el >= 5.5) { pad_event(0, 0, 0, -20000, 12000, 0, 0, "left stick x=-20000 y=+12000"); step++; }
      if (step == 5 && el >= 6.0) { pad_event(0, 0, 0, 0, 0, 0, 0, "left stick centre"); step++; }
      if (step == 6 && el >= 6.5) { pad_event(DOWN_FLAG, 0, 200, 0, 0, 0, 0, "dpad down + R2=200"); step++; }
      if (step == 7 && el >= 7.0) { pad_event(0, 0, 0, 0, 0, 0, 0, "release all"); step++; }
      if (!sent_idr && el >= idr_at) {
        tlog("requesting IDR frame (LiRequestIdrFrame); IDRs so far: %llu\n", (unsigned long long) V.idrs);
        LiRequestIdrFrame();
        sent_idr = 1;
      }
      sleep_ms(20);
    }
    uint64_t run_end = now_ms();
    tlog("stopping connection (terminated=%d code=%d)\n", g_terminated, g_term_code);
    LiStopConnection();
    tlog("LiStopConnection returned\n");

    double dur = (V.last_ms - V.first_ms) / 1000.0;
    const RTP_VIDEO_STATS *vs = LiGetRTPVideoStats();
    const RTP_AUDIO_STATS *as = LiGetRTPAudioStats();
    printf("\n==== SUMMARY ====\n");
    printf("connect_time_s        %.2f\n", (t_connected - t_start) / 1000.0);
    printf("run_time_s            %.2f\n", (run_end - run_start) / 1000.0);
    printf("video_format          0x%x %dx%d @%d\n", V.setup_format, V.setup_w, V.setup_h, V.setup_fps);
    printf("video_frames          %llu\n", (unsigned long long) V.frames);
    printf("video_idr_frames      %llu\n", (unsigned long long) V.idrs);
    printf("video_frames_pre_idr  %llu\n", (unsigned long long) V.before_first_idr);
    printf("video_bytes           %llu\n", (unsigned long long) V.bytes);
    printf("video_span_s          %.2f (first to last frame, wall clock)\n", dur);
    printf("video_fps_avg         %.2f\n", dur > 0 ? (V.frames - 1) / dur : 0.0);
    printf("video_bitrate_kbps    %.0f\n", dur > 0 ? V.bytes * 8.0 / dur / 1000.0 : 0.0);
    printf("video_frame_gaps      %llu (missing frame numbers: %llu)\n", (unsigned long long) V.gaps,
           (unsigned long long) V.missing);
    printf("video_max_gap_ms      %llu\n", (unsigned long long) V.max_gap_ms);
    printf("video_pts_span_s      %.3f (presentationTimeUs)\n", (V.last_pts - V.first_pts) / 1e6);
    printf("video_rtp_span_s      %.3f (90 kHz)\n", (uint32_t) (V.last_rtp - V.first_rtp) / 90000.0);
    printf("video_sps_pps         %d/%d\n", V.sps_seen, V.pps_seen);
    printf("rtp_video             packets=%u fec=%u recovered=%u fec_failed=%u oos=%u invalid=%u\n",
           vs->packetCountVideo, vs->packetCountFec, vs->packetCountFecRecovered, vs->packetCountFecFailed,
           vs->packetCountOOS, vs->packetCountInvalid);
    printf("audio_packets         %llu (%llu bytes, avg %.1f B)\n", (unsigned long long) A.packets,
           (unsigned long long) A.bytes, A.packets ? (double) A.bytes / (double) A.packets : 0.0);
    printf("audio_decode_errors   %llu\n", (unsigned long long) A.decode_errors);
    if (A.decoded_samples) {
      printf("audio_rms             %.5f (%.1f dBFS), peak %.5f\n", sqrt(A.sumsq / (double) A.decoded_samples),
             20.0 * log10(sqrt(A.sumsq / (double) A.decoded_samples) + 1e-12), A.peak);
    }
    printf("rtp_audio             packets=%u fec=%u recovered=%u fec_failed=%u oos=%u invalid=%u\n",
           as->packetCountAudio, as->packetCountFec, as->packetCountFecRecovered, as->packetCountFecFailed,
           as->packetCountOOS, as->packetCountInvalid);
    printf("terminated_by_host    %d (code %d)\n", g_terminated, g_term_code);
    printf("output_file           %s\n", outp);
    if (V.frames == 0) exit_code = 4;
    else if (g_terminated && g_term_code != 0) exit_code = 5;
  }
  if (g_out) fclose(g_out);
  if (g_audio_out) fclose(g_audio_out);
  return exit_code;
}
