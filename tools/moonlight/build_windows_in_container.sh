#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Runs INSIDE the prosperosunshine-sdk container (see build_windows.sh): cross-compiles OpenSSL, Opus,
# moonlight-common-c and stream_test.c with MinGW, statically, from the sources pinned in deps.lock.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

REPO=${REPO:-/repo}
SRC=$REPO/.cache/deps/src
B=${BUILD_DIR:-/tmp/mlc-win}
PREFIX=$B/prefix
OUT=$REPO/out/moonlight
TRIPLE=x86_64-w64-mingw32

apt-get update -qq
apt-get install -y --no-install-recommends gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix cmake ninja-build make perl >/dev/null
export CC=$TRIPLE-gcc-posix

rm -rf "$B"
mkdir -p "$B" "$PREFIX" "$OUT"
cat >"$B/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $TRIPLE-g++-posix)
set(CMAKE_RC_COMPILER $TRIPLE-windres)
set(CMAKE_FIND_ROOT_PATH /usr/$TRIPLE $PREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

cp -r "$SRC/openssl" "$B/openssl"
(cd "$B/openssl" && CC=$CC ./Configure mingw64 no-shared no-tests no-apps no-docs no-asm --libdir=lib --prefix="$PREFIX" >/dev/null \
  && make -j"$(nproc)" build_libs >/dev/null 2>&1 && make install_dev >/dev/null)

cmake -S "$SRC/opus" -B "$B/opus" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
cmake --build "$B/opus" --target install 2>&1 | tail -1

# moonlight-common-c expects its submodules inside its tree
cp -r "$SRC/moonlight-common-c" "$B/src"
rm -rf "$B/src/enet" "$B/src/nanors"
cp -r "$SRC/enet" "$B/src/enet"
cp -r "$SRC/nanors" "$B/src/nanors"
cmake -S "$B/src" -B "$B/build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$B/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF -DOPENSSL_ROOT_DIR="$PREFIX" -DOPENSSL_USE_STATIC_LIBS=ON >/dev/null
cmake --build "$B/build" 2>&1 | tail -1

ENET_LIB=$(find "$B/build" -name 'libenet.a' | head -1)
MLC_LIB=$(find "$B/build" -name 'libmoonlight-common-c.a' | head -1)
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -static \
  -I"$B/src/src" -I"$PREFIX/include" -I"$PREFIX/include/opus" \
  "$REPO/tools/moonlight/stream_test.c" -o "$OUT/stream_test.exe" \
  "$MLC_LIB" "$ENET_LIB" -L"$PREFIX/lib" -lopus -lssl -lcrypto -lws2_32 -lwinmm -lcrypt32 -lbcrypt -liphlpapi -lpthread -lm
ls -l "$OUT/stream_test.exe"
