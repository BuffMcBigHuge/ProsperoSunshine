#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build tools/moonlight/stream_test.c as a Windows program in the SDK container, for measurements without
# Docker's network between client and console. Result: out/moonlight/stream_test.exe (see NATIVE=1 in run_smoke.sh).
set -euo pipefail
cd "$(dirname "$0")/../.."
REPO_WIN=$(pwd -W 2>/dev/null || pwd)
IMAGE=${IMAGE:-prosperosunshine-sdk:v0.45}
MSYS_NO_PATHCONV=1 docker run --rm --mount "type=bind,source=$REPO_WIN,target=/repo" "$IMAGE" \
  bash /repo/tools/moonlight/build_windows_in_container.sh
