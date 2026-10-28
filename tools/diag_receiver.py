# SPDX-License-Identifier: GPL-3.0-only
"""Receive the raw H.264 diagnostic stream from capture_soak.elf (TCP 9998) and save it.

Usage: python tools/diag_receiver.py <console-ip> <output.h264> [--retry-seconds N]
Pipe to a player instead with an output of "-" (e.g. ... - | ffplay -f h264 -).
"""
import argparse
import socket
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("output")
    parser.add_argument("--port", type=int, default=9998)
    parser.add_argument("--retry-seconds", type=float, default=120)
    args = parser.parse_args()

    deadline = time.monotonic() + args.retry_seconds
    while True:
        try:
            sock = socket.create_connection((args.host, args.port), timeout=5)
            break
        except OSError:
            if time.monotonic() > deadline:
                print("no connection", file=sys.stderr)
                return 1
            time.sleep(1)
    # The payload's start/stop cycles run before it accepts, so the first bytes can take a while.
    sock.settimeout(120)
    out = sys.stdout.buffer if args.output == "-" else open(args.output, "wb")
    total = 0
    start = time.monotonic()
    try:
        while chunk := sock.recv(1 << 16):
            out.write(chunk)
            total += len(chunk)
    except socket.timeout:
        pass
    finally:
        out.flush()
        if out is not sys.stdout.buffer:
            out.close()
    elapsed = time.monotonic() - start
    print(f"received {total} bytes in {elapsed:.1f} s", file=sys.stderr)
    return 0 if total else 1


if __name__ == "__main__":
    sys.exit(main())
