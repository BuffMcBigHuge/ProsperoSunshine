#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# PS5 build inside the SDK image: third-party libraries (scripts/deps.sh, cached by stamps), then
# the CMake project, which places the host, probes and libraries in out/. Extra arguments go to
# `cmake --build`, e.g. `--target prospero_sunshine`.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
build=${BUILD_DIR:-/tmp/prospero-build}
bash "$root/scripts/deps.sh" >/dev/null
"$sdk/bin/prospero-cmake" -S "$root" -B "$build" -G Ninja -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$build" "$@"
ls -la "$root/out"/*.elf
