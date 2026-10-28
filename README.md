# ProsperoSunshine

A native, Sunshine-compatible game-stream host for the PlayStation 5, so that Moonlight clients can
stream the PS5 screen.

> **Status: early work in progress. This is not a working Moonlight host yet.**
> Pairing, RTSP, media transport, audio capture and input are still missing.

## What exists

- **Video capture** (`src/platform/ps5/capture.{h,c}`): H.264 of the composited screen via Avcap2 and
  the ResourceArbitrator, verified on firmware 13.00 (10 s, 1080p, decodes cleanly).
- **Runtime probes** proving C++23, threads, `std::jthread`/stop tokens and sockets on the console.
- **Third-party libraries** cross-built for the PS5 and verified live: OpenSSL 3.5.9, Opus 1.6.1,
  ENet (Moonlight fork) and nanors (Reed-Solomon FEC). Sources and hashes are pinned in `deps.lock`.
- **Frame queue** with IDR resynchronisation and sanitizer-checked unit tests.

The roadmap with work packages and acceptance criteria is in `ROADMAP.md`; goals and rules are in `PLAN.md`, the verified working state in `STATUS.md` (all German).

## Building

Requires Docker. The image contains LLVM 18 and the
[PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.45 (hash-checked).

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

Outputs land in `out/`: the probe ELF payloads, `libprospero_capture.a` and, on the first build,
the static dependencies in `out/deps`. Portable tests:

```sh
docker run --rm --mount type=bind,source="$PWD",target=/repo prosperosunshine-sdk:v0.45 bash scripts/test.sh
```

The payloads are run by sending them to an ELF loader on a console running homebrew.

## License

GPL-3.0-only, like [Sunshine](https://github.com/LizardByte/Sunshine), which this project builds on.
Third-party licence texts are installed to `out/deps/licenses` by `scripts/deps.sh`.
