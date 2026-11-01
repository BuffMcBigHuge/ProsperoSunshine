#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prints the app's pad_log.txt (app/src/psapp/pad_log.hpp), one line per change. Layout: docs/re/pad-input.md."""
import struct
import sys


def decode(data):
    count = data[0x34]
    fingers = []
    for at in (0x3C, 0x44)[:count]:
        x, y = struct.unpack_from("<HH", data, at)
        fingers.append("id=%d %d,%d" % (data[at + 4], x, y))
    return "buttons=%#x touch=%d [%s] orientation=%s accel=%s gyro=%s" % (
        struct.unpack_from("<I", data, 0)[0], count, "; ".join(fingers),
        "%.3f,%.3f,%.3f,%.3f" % struct.unpack_from("<4f", data, 0x0C),
        "%.3f,%.3f,%.3f" % struct.unpack_from("<3f", data, 0x1C),
        "%.3f,%.3f,%.3f" % struct.unpack_from("<3f", data, 0x28))


def main():
    last = None
    with open(sys.argv[1], encoding="ascii") as f:
        for line in f:
            if line.startswith("#"):
                print(line.strip())
                continue
            ms, rc, raw = line.split()
            text = "rc=%s %s" % (rc, decode(bytes.fromhex(raw)))
            if text != last:
                print(ms, text)
            last = text


if __name__ == "__main__":
    main()
