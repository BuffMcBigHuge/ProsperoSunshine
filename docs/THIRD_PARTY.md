# Third-party notices for the release archive (ROADMAP P7.2)

This file describes what the release step (P7.4) must bundle with `prospero_sunshine.elf`. It is the specification
for a `THIRD_PARTY` notice in the archive; no build script produces it yet. Licence analysis: `docs/licenses.md`.

## Archive layout

```
prospero_sunshine.elf
SHA256SUMS
CHANGELOG.md
LICENSE                      GPL-3.0 text of ProsperoSunshine (repository file LICENSE)
THIRD_PARTY.md               the notice below
licenses/
  prosperosunshine.txt       copy of LICENSE (the CI workflow already does this)
  sunshine.txt  moonlight-common-c.txt  enet.txt  nanors.txt  simple-web-server.txt
  nlohmann-json.txt  libdisplaydevice.txt  boost.txt  openssl.txt  opus.txt  ffmpeg.txt
                             all produced by scripts/deps.sh into out/deps/licenses/ (already collected by CI)
  ps5-payload-sdk-runtime/   NEW, see below
```

Also state in the release notes the tag that contains the corresponding source (`deps.lock`, `patches/`, scripts).

## Notice text (put in `THIRD_PARTY.md` of the archive)

ProsperoSunshine is free software under GPL-3.0-only. It contains or statically links the following components; their
licence texts are in `licenses/`. Versions and SHA256 of the exact sources: `deps.lock` in the source tag.

| Component | Licence | File in `licenses/` |
| --- | --- | --- |
| Sunshine (LizardByte), patched | GPL-3.0 | `sunshine.txt` |
| moonlight-common-c | GPL-3.0 | `moonlight-common-c.txt` |
| ENet (Moonlight fork) | MIT | `enet.txt` |
| nanors | MIT | `nanors.txt` |
| Simple-Web-Server | MIT | `simple-web-server.txt` |
| nlohmann/json | MIT | `nlohmann-json.txt` |
| libdisplaydevice (common) | MIT | `libdisplaydevice.txt` |
| Boost | BSL-1.0 | `boost.txt` |
| OpenSSL | Apache-2.0 | `openssl.txt` |
| Opus | BSD-3-Clause | `opus.txt` |
| FFmpeg libavcodec/libavutil/libswscale | LGPL-2.1-or-later | `ffmpeg.txt` |
| PS5 Payload SDK runtime (libc, libc++, libc++abi, libunwind, start files) | BSD (FreeBSD libc), Apache-2.0 with LLVM exception | `ps5-payload-sdk-runtime/` |

## SDK runtime: what to add

Finding: neither `scripts/deps/*.sh` nor the CI step collects any text for the Payload SDK runtime, and the SDK zip
(v0.45) itself ships none. The release must add a directory `licenses/ps5-payload-sdk-runtime/` with:

1. The SDK's own licence: `LICENSE` from the upstream `ps5-payload-dev/sdk` repository at the tag matching `SDK_VERSION`
   in `scripts/Dockerfile` (v0.45). Confirm there that it covers the shipped libraries; the zip's `target/lib/*.a` are
   not marked.
2. FreeBSD libc: the BSD copyright/licence notice (FreeBSD `COPYRIGHT` file, covering the UCB and other BSD notices
   in `libc.a`) for the FreeBSD release the SDK libc derives from. Identify that release from the SDK's `target/include`
   (`sys/param.h`, `__FreeBSD_version`) when assembling, and take the text from that release's tree.
3. LLVM libc++, libc++abi, libunwind: `libcxx/LICENSE.TXT`, `libcxxabi/LICENSE.TXT`, `libunwind/LICENSE.TXT` (or the
   single top-level `llvm/LICENSE.TXT`) of the LLVM version the SDK was built from (Apache-2.0 with LLVM exception).
4. If the compiler-rt builtins archive is linked into the ELF: `compiler-rt/LICENSE.TXT`, or the Debian copyright file
   `/usr/share/doc/libclang-rt-18-dev/copyright` from the SDK Docker image (`prosperosunshine-sdk:v0.45`).

Verify with `ar t` / `strings` on the linked archives which of items 2 to 4 actually end up in the ELF before
finalising. Until these files are bundled, the release is incomplete with respect to the BSD notice requirement.

## Checks before publishing

- `licenses/` contains all 11 texts listed above plus `prosperosunshine.txt` and the SDK runtime directory.
- No file contains local paths, addresses or personal data.
- The tag referenced in the release notes contains `deps.lock`, `patches/` and `scripts/`.
