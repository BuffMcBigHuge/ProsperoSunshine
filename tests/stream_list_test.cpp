// SPDX-License-Identifier: GPL-3.0-only
#include "control/stream_list.hpp"
#include <cassert>
#include <iostream>
using namespace prospero;

int main() {
    assert(codec_name(0) == "H.264");
    assert(codec_name(1) == "HEVC");
    assert(codec_name(2) == "AV1");

    StreamFacts first;
    first.cert = "cert-a";
    first.address = "192.168.1.20";
    first.width = 1280;
    first.height = 720;
    first.fps = 30;
    first.kbps = 10000;
    first.video_format = 1;
    first.hdr = true;
    first.seconds = 754;
    first.pads = 1;

    // No capture yet: what the client asked for.
    assert(stream_line(first, std::nullopt, "UUID-A", "Living room PC") ==
           "OK uuid=UUID-A address=192.168.1.20 codec=HEVC hdr=1 mode=1280x720@30 kbps=10000 secs=754 pads=1 "
           "role=starting name=Living room PC");

    // The stream that holds the capture: what is delivered.
    CaptureFacts capture;
    capture.cert = "cert-a";
    capture.joined = {"cert-b"};
    capture.width = 1920;
    capture.height = 1080;
    capture.fps = 60;
    capture.kbps = 20000;
    capture.video_format = 1;
    capture.hdr = false;
    assert(stream_line(first, capture, "UUID-A", "Living room PC") ==
           "OK uuid=UUID-A address=192.168.1.20 codec=HEVC hdr=0 mode=1920x1080@60 kbps=20000 secs=754 pads=1 "
           "role=capture name=Living room PC");

    // A device that joined gets the same picture, whatever it asked for.
    StreamFacts second;
    second.cert = "cert-b";
    second.address = "::ffff:192.168.1.21";
    second.width = 1280;
    second.height = 800;
    second.fps = 90;
    second.kbps = 5000;
    second.seconds = 5;
    second.pads = 2;
    assert(stream_line(second, capture, "UUID-B", "Steam Deck") ==
           "OK uuid=UUID-B address=192.168.1.21 codec=HEVC hdr=0 mode=1920x1080@60 kbps=20000 secs=5 pads=2 "
           "role=joined name=Steam Deck");

    // Neither holding nor joined (it is still setting up, or about to be turned away): its own values.
    StreamFacts third;
    third.cert = "cert-c";
    third.video_format = 0;
    third.hdr = true;  // 10 bit means nothing for H.264
    third.seconds = -3;
    third.pads = -1;
    assert(stream_line(third, capture, "", "") ==
           "OK uuid=- address=- codec=H.264 hdr=0 mode=- kbps=0 secs=0 pads=0 role=starting name=");

    // A session without a certificate never counts as the capture's.
    StreamFacts anonymous;
    CaptureFacts empty_cert;
    assert(stream_line(anonymous, empty_cert, "X", "a").find(" role=starting ") != std::string::npos);

    // A name stays on one line; an IPv6 address stays as it is.
    StreamFacts v6;
    v6.address = "fe80::1";
    assert(stream_line(v6, std::nullopt, "U", "two\nlines\r") ==
           "OK uuid=U address=fe80::1 codec=H.264 hdr=0 mode=- kbps=0 secs=0 pads=0 role=starting name=two lines ");

    std::cout << "PASS: stream list (what each device asked for, what the capture delivers, unknown values, one line)\n";
    return 0;
}
