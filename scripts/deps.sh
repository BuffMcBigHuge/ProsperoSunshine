#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fetch the sources pinned in deps.lock, verify them and build static PS5 libraries into out/deps.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
sdk=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}
dl="$root/.cache/deps/dl"
work=${DEPS_WORK:-/tmp/prospero-deps}
prefix="$root/out/deps"
jobs=$(nproc)
mkdir -p "$dl" "$prefix/include" "$prefix/lib"
rm -rf "$work" && mkdir -p "$work"

declare -A src
while read -r name version sha url; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    file="$dl/$name-$version.tar.gz"
    [[ -f "$file" ]] || curl -fsSL -o "$file" "$url"
    echo "$sha  $file" | sha256sum -c --quiet -
    mkdir -p "$work/$name"
    tar -xzf "$file" -C "$work/$name" --strip-components=1
    src[$name]="$work/$name"
done < "$root/deps.lock"

cc="$sdk/bin/prospero-clang"
ar="$sdk/bin/prospero-ar"
cflags="-O2 -fPIC"

# OpenSSL: libcrypto and libssl only. async needs ucontext, which the payload libc does not promise.
(cd "${src[openssl]}" && \
    CC="$cc" AR="$ar" RANLIB="$sdk/bin/prospero-ranlib" ./Configure BSD-x86_64 \
        no-shared no-module no-dso no-engine no-async no-tests no-apps no-docs no-ui-console \
        --prefix="$prefix" --libdir=lib --openssldir=/user/homebrew/etc/ssl $cflags >/dev/null && \
    make -j"$jobs" build_libs >/dev/null && make install_dev >/dev/null)

# Opus: float build with runtime CPU detection.
"$sdk/bin/prospero-cmake" -S "${src[opus]}" -B "$work/opus-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DOPUS_BUILD_PROGRAMS=OFF -DOPUS_BUILD_TESTING=OFF \
    -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$work/opus-build" >/dev/null
cmake --install "$work/opus-build" >/dev/null

# ENet: moonlight's fork, configured by its own CMake feature checks.
"$sdk/bin/prospero-cmake" -S "${src[enet]}" -B "$work/enet-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_VERBOSE_MAKEFILE=OFF >/dev/null
cmake --build "$work/enet-build" >/dev/null
cmake --install "$work/enet-build" >/dev/null
mv -f "$prefix/lib/static/libenet.a" "$prefix/lib/libenet.a" && rmdir "$prefix/lib/static"

# nanors: the same three sources Sunshine compiles directly. Its SIMD kernels use per-function
# target attributes, but clang's immintrin.h hides AVX2/AVX-512 types on __SCE__ targets unless the
# feature is enabled for the whole file, so the macro is masked for that one include.
# Its dispatch uses __builtin_cpu_supports, whose compiler-rt support the SDK does not link;
# the prelude answers the same questions from CPUID and XCR0 instead.
n="${src[nanors]}"
mkdir -p "$work/nanors-build" "$prefix/include/nanors"
cat > "$work/nanors-build/prelude.h" <<'EOF'
#include <stdint.h>
#include <string.h>
#include <cpuid.h>
#pragma push_macro("__SCE__")
#undef __SCE__
#include <immintrin.h>
#pragma pop_macro("__SCE__")
static inline int prospero_cpu_supports(const char *feature) {
    unsigned a, b, c, d, b7 = 0, c7 = 0, xcr0 = 0;
    if (!__get_cpuid(1, &a, &b, &c, &d)) return 0;
    const unsigned c1 = c;
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d)) { b7 = b; c7 = c; }
    if (c1 & (1u << 27)) { unsigned hi; __asm__("xgetbv" : "=a"(xcr0), "=d"(hi) : "c"(0)); }
    const int ymm = (xcr0 & 0x06) == 0x06, zmm = (xcr0 & 0xE6) == 0xE6;
    if (!strcmp(feature, "ssse3")) return (c1 >> 9) & 1;
    if (!strcmp(feature, "avx2")) return ymm && ((b7 >> 5) & 1);
    if (!strcmp(feature, "avx512f")) return zmm && ((b7 >> 16) & 1);
    if (!strcmp(feature, "gfni")) return (c7 >> 8) & 1;
    return 0;
}
#define __builtin_cpu_init() ((void)0)
#define __builtin_cpu_supports(feature) prospero_cpu_supports(feature)
EOF
for f in rs.c deps/obl/oblas_common.c deps/obl/oblas_lite.c; do
    "$cc" -std=gnu11 $cflags -include "$work/nanors-build/prelude.h" -I"$n" -I"$n/deps/obl" \
        -c "$n/$f" -o "$work/nanors-build/$(basename "$f" .c).o"
done
"$ar" rcs "$prefix/lib/libnanors.a" "$work/nanors-build"/*.o
cp "$n/rs.h" "$n/deps/obl"/*.h "$prefix/include/nanors/"

# Upstream licence texts travel with the binaries.
mkdir -p "$prefix/licenses"
cp "${src[openssl]}/LICENSE.txt" "$prefix/licenses/openssl.txt"
cp "${src[opus]}/COPYING" "$prefix/licenses/opus.txt"
cp "${src[enet]}/LICENSE" "$prefix/licenses/enet.txt"
cp "$n/LICENSE" "$prefix/licenses/nanors.txt"
cp "${src[moonlight-common-c]}/LICENSE.txt" "$prefix/licenses/moonlight-common-c.txt"
ls -la "$prefix/lib"/*.a
