#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Portable unit tests on the build host, under AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$root/out"
run_test() {
    local name=$1
    shift
    clang++-18 -std=c++23 -stdlib=libstdc++ -Wall -Wextra -Werror -O1 -g \
        -fsanitize=address,undefined -I "$root/src" "$root/tests/$name.cpp" "$@" -o "$root/out/$name"
    "$root/out/$name"
}
run_test video_test
run_test capture_session_test "$root/src/capture/capture_session.cpp"
run_test gamepad_map_test
run_test listener_watch_test
run_test mdns_test "$root/src/net/mdns.cpp"
run_test ra_choose_test
