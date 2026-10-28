#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# OpenSSL: libcrypto and libssl only. async needs ucontext, which the payload libc does not promise.
set -euo pipefail
b="$work/openssl-build"
rm -rf "$b" && mkdir -p "$b" && cd "$b"
CC="$sdk/bin/prospero-clang" AR="$sdk/bin/prospero-ar" RANLIB="$sdk/bin/prospero-ranlib" \
    "$src_openssl/Configure" BSD-x86_64 \
    no-shared no-module no-dso no-engine no-async no-tests no-apps no-docs no-ui-console \
    --prefix="$prefix" --libdir=lib --openssldir=/user/homebrew/etc/ssl -O2 -fPIC >/dev/null
make -j"$jobs" build_libs >/dev/null
make install_dev >/dev/null 2>&1
cp "$src_openssl/LICENSE.txt" "$prefix/licenses/openssl.txt"
