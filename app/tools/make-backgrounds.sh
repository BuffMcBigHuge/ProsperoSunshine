#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Makes the home screen backgrounds pic0.dds and pic1.dds (3840x2160, one level, DX10 BC7_UNORM, the
# profile ps5-homebrew-ui's tools/validate-assets.sh and the console accept) from one picture.
# Adapted from PS5_Vulkan's tools/prepare-assets.sh and tools/setup-asset-dependencies.sh
# (BlackBearReloaded, Mihawk-99, GPL-3.0-or-later): FFmpeg scales with Lanczos, the pinned
# richgel999/bc7enc_rdo encodes, and the DDS header is set to one mip level and straight alpha.
#
#   app/tools/make-backgrounds.sh <picture> <output dir> <cache dir>
#
# The encoder is built once in <cache dir>/bc7enc_rdo; a picture already converted is taken from
# <cache dir>/backgrounds/<sha256 of the picture>/.
set -euo pipefail

source=$1
output=$2
cache=$3
revision=b9438627eef73a1157e84201b6fa6eb2ffd6d9f0
encoder_dir="$cache/bc7enc_rdo"
encoder="$encoder_dir/build/bc7enc"

hash=$(sha256sum "$source" | cut -d' ' -f1)
done_dir="$cache/backgrounds/$hash"
if [ ! -f "$done_dir/pic0.dds" ]; then
    if [ ! -x "$encoder" ] || [ "$(cat "$encoder_dir/.built" 2>/dev/null)" != "$revision" ]; then
        [ -d "$encoder_dir/.git" ] || git clone -q https://github.com/richgel999/bc7enc_rdo "$encoder_dir"
        git -C "$encoder_dir" fetch -q origin "$revision" 2>/dev/null || true
        git -C "$encoder_dir" checkout -q --detach "$revision"
        # The project predates CMake 3.5 and leans on <cstdint> arriving transitively.
        cmake -S "$encoder_dir" -B "$encoder_dir/build" -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_CXX_FLAGS="-include cstdint" \
            -DCMAKE_EXE_LINKER_FLAGS="-no-pie" -DSUPPORT_BC7E=OFF > /dev/null
        cmake --build "$encoder_dir/build" --parallel > /dev/null
        echo "$revision" > "$encoder_dir/.built"
    fi
    work=$(mktemp -d)
    ffmpeg -hide_banner -loglevel error -y -i "$source" -vf scale=3840:2160:flags=lanczos -frames:v 1 \
        -pix_fmt rgba "$work/scaled.png"
    "$encoder" -q -g "$work/scaled.png" "$work/pic.dds" > /dev/null
    python3 - "$work/pic.dds" <<'PY'
import struct, sys
path = sys.argv[1]
data = bytearray(open(path, "rb").read())
width, height = 3840, 2160
if data[:4] != b"DDS " or data[84:88] != b"DX10":
    raise SystemExit("make-backgrounds: the encoder did not write a DX10 DDS")
if struct.unpack_from("<II", data, 12) != (height, width) or struct.unpack_from("<I", data, 128)[0] != 98:
    raise SystemExit("make-backgrounds: not a 3840x2160 BC7 image")
if len(data) != 148 + (width // 4) * (height // 4) * 16:
    raise SystemExit("make-backgrounds: unexpected size")
# One mip level and straight alpha, as texconv writes them (bc7enc_rdo leaves both unset).
struct.pack_into("<I", data, 8, struct.unpack_from("<I", data, 8)[0] | 0x20000)
struct.pack_into("<I", data, 24, 1)
struct.pack_into("<I", data, 28, 1)
struct.pack_into("<I", data, 144, 1)
open(path, "wb").write(data)
PY
    mkdir -p "$done_dir"
    cp "$work/pic.dds" "$done_dir/pic0.dds"
    cp "$work/pic.dds" "$done_dir/pic1.dds"
    rm -rf "$work"
fi
cp "$done_dir/pic0.dds" "$done_dir/pic1.dds" "$output/"
echo "make-backgrounds: pic0.dds and pic1.dds from $(basename "$source") ($hash)"
