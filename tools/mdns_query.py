#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Browse _nvstream._tcp over mDNS (ROADMAP P9.2) and print every record seen.

  python tools/mdns_query.py [seconds]

PS5_HOST (optional) is only used to pick the local interface that reaches the console. Sends a PTR
query once per second and decodes all responses on 224.0.0.251:5353 (PTR, SRV, TXT, A).
"""
import os
import socket
import struct
import sys
import time

GROUP, PORT = "224.0.0.251", 5353
NAMES = {1: "A", 12: "PTR", 16: "TXT", 28: "AAAA", 33: "SRV"}


def local_address():
    peer = os.environ.get("PS5_HOST") or "192.0.2.1"
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.connect((peer, 9))
        return s.getsockname()[0]


def encode(name):
    return b"".join(bytes([len(p)]) + p.encode() for p in name.split(".")) + b"\0"


def read_name(data, pos):
    labels, jumped, end = [], False, pos
    for _ in range(128):
        n = data[pos]
        if n & 0xC0 == 0xC0:
            if not jumped:
                end = pos + 2
            jumped = True
            pos = ((n & 0x3F) << 8) | data[pos + 1]
        elif n == 0:
            return ".".join(labels), (end if jumped else pos + 1)
        else:
            labels.append(data[pos + 1:pos + 1 + n].decode("utf-8", "replace"))
            pos += 1 + n
    raise ValueError("name loop")


def records(data):
    qd, an, ns, ar = struct.unpack(">4H", data[4:12])
    pos = 12
    for _ in range(qd):
        _, pos = read_name(data, pos)
        pos += 4
    for _ in range(an + ns + ar):
        name, pos = read_name(data, pos)
        rtype, _, ttl, length = struct.unpack(">HHIH", data[pos:pos + 10])
        pos += 10
        rd = data[pos:pos + length]
        if rtype == 1 and length == 4:
            value = socket.inet_ntoa(rd)
        elif rtype == 12:
            value = read_name(data, pos)[0]
        elif rtype == 33:
            port = struct.unpack(">H", rd[4:6])[0]
            value = f"port={port} target={read_name(data, pos + 6)[0]}"
        else:
            value = rd.hex()
        yield name, NAMES.get(rtype, rtype), ttl, value
        pos += length


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 8
    iface = local_address()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", PORT))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, socket.inet_aton(GROUP) + socket.inet_aton(iface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(iface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    query = struct.pack(">6H", 0, 0, 1, 0, 0, 0) + encode("_nvstream._tcp.local") + struct.pack(">HH", 12, 1)
    only = os.environ.get("PS5_HOST")  # when set, show just that host's records
    seen, last_query, end = set(), 0.0, time.time() + seconds
    sock.settimeout(0.25)
    while time.time() < end:
        if time.time() - last_query >= 1:
            sock.sendto(query, (GROUP, PORT))
            last_query = time.time()
        try:
            data, (addr, _) = sock.recvfrom(9000)
        except socket.timeout:
            continue
        if len(data) < 12 or not data[2] & 0x80:
            continue  # queries from other hosts
        try:
            for rec in records(data):
                key = (addr,) + rec
                if key not in seen and (not only or addr == only):
                    seen.add(key)
                    print(f"{time.strftime('%H:%M:%S')} from {addr}: {rec[0]} {rec[1]} ttl={rec[2]} {rec[3]}", flush=True)
        except (ValueError, IndexError, struct.error):
            pass


if __name__ == "__main__":
    main()
