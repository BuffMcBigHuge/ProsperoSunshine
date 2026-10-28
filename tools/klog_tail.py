# SPDX-License-Identifier: GPL-3.0-only
"""Print the console's kernel log (klogsrv, TCP 3232) with PC-side timestamps.

Usage: python tools/klog_tail.py <console-ip> <seconds> [substring ...]
Only lines containing one of the substrings are printed (all lines without filters).
"""
import socket
import sys
import time


def main() -> int:
    host, seconds, filters = sys.argv[1], float(sys.argv[2]), sys.argv[3:]
    sock = socket.create_connection((host, 3232), timeout=5)
    sock.settimeout(0.5)
    end = time.monotonic() + seconds
    pending = b""
    while time.monotonic() < end:
        try:
            chunk = sock.recv(65536)
        except socket.timeout:
            continue
        if not chunk:
            break
        pending += chunk
        *lines, pending = pending.split(b"\n")
        stamp = time.strftime("%H:%M:%S") + f".{int(time.time() * 1000) % 1000:03d}"
        for raw in lines:
            line = raw.decode(errors="replace").rstrip()
            if not filters or any(f in line for f in filters):
                print(stamp, line, flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
