<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Moonlight test client

Automated, Moonlight-compatible client for checking a ProsperoSunshine host on a console from the PC.

- `pair.py` - GameStream pairing and HTTPS client (Python 3, `cryptography`). Follows moonlight-qt's `nvpairingmanager.cpp`.
- `stream_test.c` - streaming smoke test on top of moonlight-common-c (RTSP, video, audio, control, input).
- `build.sh` - builds moonlight-common-c (pinned source in `.cache/deps/src`) and `stream_test` in the SDK container.
- `run_smoke.sh` - one command: `/launch`, run `stream_test`, verify with ffmpeg, `/cancel`.

The host must be running (ELF loaded) and the dev control socket (TCP 47995) reachable. Pass the console address as the first argument or set `PS5_HOST`.
The client identity (key, certificate, unique id, pinned server certificate) is kept in `out/moonlight-client/`.

## Pair

```sh
py -3 tools/moonlight/pair.py pair            # random PIN, approved through the control socket
py -3 tools/moonlight/pair.py info            # /serverinfo + /applist over HTTPS, expects PairStatus=1
py -3 tools/moonlight/pair.py negative        # a fresh, unpaired certificate must be rejected
```

`pair` starts `phrase=getservercert` in a thread (it blocks until a PIN is approved), sends `PIN <pin> <name>` to the
control socket, then completes the challenge/response steps and `pairchallenge` over HTTPS, verifying the host's
signature and PIN hash. Use `--manual-pin` to approve the PIN yourself, `--host-pin`/`--no-verify` for the wrong-PIN
test, `--host` for another address, `--dir` for another identity directory.

## Smoke test

```sh
bash tools/moonlight/build.sh                         # once; needs Docker image prosperosunshine-sdk:v0.45
bash tools/moonlight/run_smoke.sh <console-ip> 20       # host, seconds
bash tools/moonlight/run_smoke.sh <console-ip> 20 --corever   # also use encrypted RTSP (rtspenc://), like Moonlight
```

Outputs: `out/moonlight-test.h264` (received stream), `out/moonlight-test.s16` (decoded audio, 48 kHz stereo s16le),
`out/moonlight/stream_test.log`. The script prints frame/IDR/audio counts, then `ffprobe -count_frames` and
`ffmpeg -xerror` results for the file. Exit code is non-zero if the connection failed, no frame arrived, or the host
terminated the stream with an error. A failed connection reports the failing stage and error code.

## Audio/video synchronisation (ROADMAP P5.2)

`av_sync.py` measures how far apart picture and sound arrive at the client for one real event, and the long-term drift.
The PS5 home menu moves its focus highlight and plays a navigation click when D-pad left/right is pressed; that is the
simultaneous picture+sound event. The home screen must be in front (the script does not launch titles, and it passes
`--no-script` because the scripted startup input presses A, which would start the focused tile).

```sh
py -3 tools/moonlight/av_sync.py --host <console-ip> --duration 60 --ticks 30     # capture (run_smoke.sh) + analysis
py -3 tools/moonlight/av_sync.py --analyze-only                                    # re-analyse the last capture
py -3 tools/moonlight/av_sync.py --kind ps ...                                     # PS button (control centre) instead of D-pad
```

`stream_test` gained `--ui-ticks N [--tick-interval-ms 1500] [--tick-kind dpad|ps] [--tick-start S] [--no-script]` and
`--timing-log <csv>`. Without them nothing changes. The log has one line per received video frame
(`V,frame,idr,arrival_us,presentationTimeUs,bytes`), audio packet (`A,index,arrival_us,rms,peak`) and press
(`P,tick,action,sent_us`) on a single CLOCK_MONOTONIC microsecond clock (arrival = when the moonlight-common-c callback
ran, i.e. after reassembly/FEC, so network jitter affects both streams alike).

`av_sync.py` decodes the h264/s16 files in the SDK container with ffmpeg filters only (no numpy): per decoded frame the mean
absolute luma difference to the previous frame (`tblend=difference`, `signalstats`, 480x270, region `--roi`), and the
level of the audio after a 1.5 kHz high-pass per 5 ms packet. A visual event is the first frame after a press whose
difference exceeds median + 8 MAD of the run; an audio onset is the first packet whose high-passed power is 12 dB above
the mean of the previous 200 ms. It prints per press: press to picture (input-to-photon at the client), press to sound,
and sound minus picture; median/min/max/sd and the trend over the run; and, without any events, the slope of both
streams' arrival times against their own timeline (video `presentationTimeUs`, audio 5 ms per packet), which is the
clock drift of the host streams against the client clock. Events with no detectable change are listed and excluded.

The picture detector skips keyframes (once a second) and the frame after them, since a keyframe changes the whole
picture a little. Disconnect other virtual pads first (`ps5_detach` for RemoteEye): the console's own UI takes input
from every pad, but the app was seen to read only the one connected first. To measure inside the app, limit `--roi` to
the region that changes.

Caveats: times are client arrival times, not presentation times. Video is quantised to the 16.7 ms frame interval; the
host sends audio in FEC blocks of 4 x 5 ms packets, so audio arrives in bursts every ~20 ms (the script also prints a
burst-aligned estimate that subtracts 5 ms per packet still to come in the burst). The press time is when
`LiSendMultiControllerEvent` returned on the client; the real input path adds network and host latency.

Manual steps: `pair.py launch` prints JSON (`sessionUrl0`, `rikey`, `rikeyid`, `appversion`, `gfe`, `codec_mode`) that
`stream_test` takes as arguments (`stream_test --help` lists them); `pair.py cancel` ends the host session.
`stream_test` scripted input: controller arrival, A press/release, stick moves, D-pad and R2, `LiRequestIdrFrame()` at
half time.
