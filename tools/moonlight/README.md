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

Manual steps: `pair.py launch` prints JSON (`sessionUrl0`, `rikey`, `rikeyid`, `appversion`, `gfe`, `codec_mode`) that
`stream_test` takes as arguments (`stream_test --help` lists them); `pair.py cancel` ends the host session.
`stream_test` scripted input: controller arrival, A press/release, stick moves, D-pad and R2, `LiRequestIdrFrame()` at
half time.
