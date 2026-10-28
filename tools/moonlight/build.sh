#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build moonlight-common-c + tools/moonlight/stream_test.c in the SDK container.
# Result: out/moonlight/stream_test (a Linux binary, run it in the same container).
set -euo pipefail
cd "$(dirname "$0")/../.."
REPO_WIN=$(pwd -W 2>/dev/null || pwd)
IMAGE=${IMAGE:-prosperosunshine-sdk:v0.45}
MSYS_NO_PATHCONV=1 docker run --rm --mount "type=bind,source=$REPO_WIN,target=/repo" "$IMAGE" \
  bash /repo/tools/moonlight/build_in_container.sh
