#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# PS5 build inside the SDK image: third-party libraries (scripts/deps.sh, cached by stamps), then
# the CMake project, which places the host, probes and libraries in out/. Extra arguments go to
# `cmake --build`, e.g. `--target prospero_sunshine`.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
build=${BUILD_DIR:-/tmp/prospero-build}
# The commit the host reports. A worktree's .git is a file naming a folder outside the container, so git finds nothing there:
# pass PROSPERO_COMMIT in (scripts/build.ps1 and CI do). The mounted repository often has another owner, hence safe.directory.
PROSPERO_COMMIT=${PROSPERO_COMMIT:-$(git -c safe.directory='*' -C "$root" rev-parse --short=8 HEAD 2>/dev/null || true)}
export PROSPERO_COMMIT=${PROSPERO_COMMIT:-unknown}
echo "build: commit $PROSPERO_COMMIT"
[[ "$PROSPERO_COMMIT" != unknown ]] || echo "build: WARNING: no commit found; docker run -e PROSPERO_COMMIT=<hash> names it" >&2
bash "$root/scripts/deps.sh" >/dev/null
"$sdk/bin/prospero-cmake" -S "$root" -B "$build" -G Ninja -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$build" "$@"
ls -la "$root/out"/*.elf
