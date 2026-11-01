# Licences and provenance (ROADMAP P7.2)

Checked: 29 October 2026. ProsperoSunshine is licensed GPL-3.0-only (`LICENSE`), like Sunshine, which it builds on.
Every third-party source is pinned by SHA256 in `deps.lock`. `scripts/deps.sh` and `scripts/deps/*.sh` install one
licence text per component into `out/deps/licenses/`; the CI workflow copies those plus `LICENSE` into the artifact.

## Findings (read first)

1. **PS5 Payload SDK runtime: texts kept in this repository.** The host links the SDK's `libc.a` (FreeBSD-derived),
   `libc++.a`, `libc++abi.a`, `libunwind.a`, `libpthread.a` and the `crt*.o` start files statically, and the SDK zip
   (v0.45) contains no licence file. The texts are therefore stored in `licenses/ps5-payload-sdk-runtime/` and bundled
   by CI and `app/build.sh`; sources and limits are in `docs/THIRD_PARTY.md`, "SDK runtime".
2. **OpenSSL 3.5.9 is Apache-2.0 only.** That is compatible with GPL-3.0-only but not with GPL-2.0-only. Nothing in the
   project is GPL-2.0-only, but never relicense or combine with GPL-2.0-only code without checking this.
3. **FFmpeg is LGPL-2.1-or-later and linked statically.** Allowed inside a GPL-3.0 work (LGPL-2.1 section 3 permits
   conversion to GPL). It is built without `--enable-gpl` and with no codec, only `libavcodec`/`libavutil`/`libswscale`
   with the CBS bitstream filters. A release must make the complete corresponding source available (repository tag with
   `deps.lock`, `patches/` and build scripts), which satisfies LGPL-2.1 and GPL-3.0 section 6.
4. No incompatible licence was found among the pinned dependencies. Sunshine's own `third-party/` submodules
   (ViGEmClient, nvapi, glad, tray, wayland protocols and so on) are not downloaded, compiled or linked; only the
   sources listed in `CMakeLists.txt` (`SUNSHINE_KEPT`, `RtspParser.c`) are used.

## Components

| Component | Version / pin | Licence | Licence text comes from | Notes |
| --- | --- | --- | --- | --- |
| Sunshine | `a679b059` | GPL-3.0 | `LICENSE` of the pinned tree, installed by `scripts/deps/sunshine.sh` as `sunshine.txt` | Only `nvhttp, httpcommon, rtsp, stream, crypto, network, audio, input, config, logging, cbs, file_handler, globals, stat_trackers` are compiled; 5 patches (below). |
| moonlight-common-c | `04053843` | GPL-3.0 | `LICENSE.txt`, `moonlight-common-c.txt` | Only `src/RtspParser.c` compiled plus headers. |
| ENet (Moonlight fork) | `aca87840` | MIT | `LICENSE`, `enet.txt` | Static library. |
| nanors | `b1e3c22c` | MIT | `LICENSE`, `nanors.txt` | Static library (Reed-Solomon FEC). |
| Simple-Web-Server (LizardByte fork) | `546895a9` | MIT | `LICENSE`, `simple-web-server.txt` | Headers only. |
| nlohmann/json | 3.11.3 | MIT | `LICENSE.MIT`, `nlohmann-json.txt` | Headers only. |
| libdisplaydevice (`common` only) | `a2afc043` | MIT | `LICENSE`, `libdisplaydevice.txt` | Static library; types and logger only. |
| Boost | 1.92.0 | BSL-1.0 | `LICENSE_1_0.txt`, `boost.txt` | Static libraries plus headers; 1 patch. |
| OpenSSL | 3.5.9 | Apache-2.0 | `LICENSE.txt`, `openssl.txt` | libcrypto and libssl, static. No `NOTICE` file in this release. |
| Opus | 1.6.1 | BSD-3-Clause | `COPYING`, `opus.txt` | Static library. |
| FFmpeg (avcodec, avutil, swscale) | 9.0.2 | LGPL-2.1-or-later | `COPYING.LGPLv2.1`, `ffmpeg.txt` | Static, no `--enable-gpl`, no codecs; 1 patch. Some FFmpeg files carry MIT/BSD-style licences, covered by the LGPL statement in FFmpeg's `LICENSE.md`. |
| PS5 Payload SDK | v0.45 (zip SHA256 in `scripts/Dockerfile`) | GPL-3.0 (upstream `LICENSE` at v0.45); runtime: FreeBSD libc (BSD-2/3-Clause, various), LLVM libc++/libc++abi/libunwind (Apache-2.0 with LLVM exception) | `licenses/ps5-payload-sdk-runtime/` (finding 1) | Statically linked runtime; the SDK tools (clang wrappers, linker) are not part of the output. |
| LLVM 18 toolchain (Debian `clang-18`, `lld-18`, `libclang-rt-18-dev`) | Debian trixie packages | Apache-2.0 with LLVM exception | Not shipped | Build tools only. If the compiler-rt builtins archive is linked into the ELF, its licence (same terms) is covered by the same LLVM text. |
| ProsperoSunshine's own code | this repository | GPL-3.0-only | `LICENSE` | Copied to `licenses/prosperosunshine.txt` by CI. |

Not third-party code: the app icon and background in `assets/` and the cover image `assets/ps5_screen.png` are
original project assets. No Sunshine images or web UI are built or shipped.

## Patches

Patches are applied on top of the pinned sources by the `scripts/deps/*.sh` scripts. Sunshine patches are by this
project and fall under Sunshine's GPL-3.0; the Boost patch is by this project under BSL-1.0; the FFmpeg patch is
third-party (MIT).

| Patch | Origin | Purpose |
| --- | --- | --- |
| `patches/sunshine/0001-input-drop-libvirtualhid.patch` | ProsperoSunshine | Removes the libvirtualhid dependency from `input.cpp` (no keyboard/mouse in v1.0; gamepads go through the PS5 platform layer). |
| `patches/sunshine/0002-config-no-nvenc-vaapi-headers.patch` | ProsperoSunshine | Defines the few NVENC/VAAPI enum values `config.cpp` needs on `__SCE__` instead of including both vendor SDK headers. |
| `patches/sunshine/0003-httpcommon-no-curl.patch` | ProsperoSunshine | Builds `httpcommon` without libcurl (only used by the unbuilt web UI); `url_escape` keeps the same encoding. |
| `patches/sunshine/0004-httpcommon-url-host-without-curl.patch` | ProsperoSunshine | Parses the URL host directly instead of via libcurl, follows 0003. |
| `patches/sunshine/0005-last-launch-client.patch` | ProsperoSunshine | Remembers the client of the last launch/resume so the control socket `STATUS` can report it. |
| `patches/sunshine/0006-end-app-with-last-client.patch` | ProsperoSunshine | Ends the session app with the last client instead of holding it for a later resume. |
| `patches/sunshine/0007-launch-timeout.patch` | ProsperoSunshine | Separates the launch timeout from the short `ping_timeout` and ends the session app when no client comes. |
| `patches/sunshine/0008-feedback-latency.patch` | ProsperoSunshine | Shortens the wait in the control loop so rumble and light bar changes reach the client quickly. |
| `patches/sunshine/0009-rtsp-restart.patch` | ProsperoSunshine | Lets the RTSP server start again after rest mode took its socket. |
| `patches/sunshine/0010-close-game-on-quit.patch` | ProsperoSunshine | Adds `close_game_on_quit`: `/cancel` then also closes the session's game. |
| `patches/boost/0001-process-v2-no-close-range-on-prospero.patch` | ProsperoSunshine | The PS5 libc has no `close_range()`; makes that path opt-in so the portable fallback is used (Boost.Process v2 is built but unused). |
| `patches/ffmpeg/0001-remove-register.patch` | LizardByte/build-deps `d0ac5ef4` (MIT), unchanged | Removes the `register` storage class in `get_bits.h`, ill-formed in the C++23 code that includes it. |

## Obligations for a release

- Keep the complete source available: repository tag including `deps.lock`, `patches/` and the build scripts (GPL-3.0
  section 6, LGPL-2.1 section 6).
- Bundle licence texts and a notice file as specified in `docs/THIRD_PARTY.md`.
- Offer the release ELF with its SHA256; the GPL-3.0 text and the written source offer are covered by the tag and the
  bundled `LICENSE`.

## Not included

- No Sony binaries or data. Decrypted system modules used for analysis live only in the ignored `.cache/re`;
  `docs/re/` describes offsets and behaviour without code excerpts, and `tools/re/` only reads files locally.
- No images or assets from Sunshine (the web UI is not built).
