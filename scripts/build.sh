#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
mkdir -p "$root/out"
"$sdk/bin/prospero-clang++" -std=c++23 -Wall -Wextra -Werror -O2 \
    "$root/src/runtime_probe.cpp" -o "$root/out/runtime_probe.elf"
echo "Built $root/out/runtime_probe.elf"
# std::jthread/stop_token sit behind libc++'s experimental gate in SDK v0.45.
"$sdk/bin/prospero-clang++" -std=c++23 -fexperimental-library -Wall -Wextra -Werror -O2 \
    "$root/src/jthread_probe.cpp" -o "$root/out/jthread_probe.elf"
echo "Built $root/out/jthread_probe.elf"
"$sdk/bin/prospero-clang" -std=c11 -Wall -Wextra -Werror -O2 \
    -c "$root/src/platform/ps5/capture.c" -o "$root/out/capture.o"
"$sdk/bin/prospero-ar" rcs "$root/out/libprospero_capture.a" "$root/out/capture.o"
"$sdk/bin/prospero-clang++" -std=c++23 -Wall -Wextra -Werror -O2 \
    "$root/src/capture_probe.cpp" "$root/out/libprospero_capture.a" -o "$root/out/capture_probe.elf"
echo "Built capture_probe.elf and libprospero_capture.a"
deps="$root/out/deps"
[[ -f "$deps/lib/libssl.a" ]] || bash "$root/scripts/deps.sh"
"$sdk/bin/prospero-clang++" -std=c++23 -fexperimental-library -Wall -Wextra -Werror -O2 \
    -isystem "$deps/include" "$root/src/deps_probe.cpp" -L"$deps/lib" \
    -lssl -lcrypto -lopus -lenet -lnanors -lpthread -o "$root/out/deps_probe.elf"
echo "Built $root/out/deps_probe.elf"
