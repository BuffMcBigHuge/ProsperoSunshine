#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/out"
clang++-18 -std=c++23 -stdlib=libstdc++ -Wall -Wextra -Werror -O1 -g \
    -fsanitize=address,undefined -I "$root/src" "$root/tests/video_test.cpp" -o "$root/out/video_test"
"$root/out/video_test"
