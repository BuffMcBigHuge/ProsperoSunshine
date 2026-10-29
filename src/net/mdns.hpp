// SPDX-License-Identifier: GPL-3.0-only
// Packet building and parsing for a minimal mDNS/DNS-SD responder (ROADMAP P9.2). It advertises one
// service, _nvstream._tcp, so Moonlight lists the console without an IP. No sockets here.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace prospero::mdns {

constexpr uint16_t type_a = 1, type_ptr = 12, type_txt = 16, type_srv = 33, type_any = 255;

struct Service {
    std::string instance;  // shown to the user, becomes "<instance>._nvstream._tcp.local"
    std::string host;      // DNS host label for the A record, see host_label()
    uint16_t port = 47989;
    std::array<uint8_t, 4> ipv4{};
};

struct Question {
    std::string name;  // dotted, lower-cased, without trailing dot
    uint16_t type = 0;
};

// Host label usable in "<label>.local": letters, digits and '-' only, at most 63 bytes.
std::string host_label(std::string_view name);
// Instance label cut to the 63 bytes one DNS label can hold, never inside a UTF-8 sequence.
std::string instance_label(std::string_view name);

// Questions of a query packet; empty for responses, malformed packets and packets without questions.
// Handles name compression.
std::vector<Question> parse_questions(std::span<const uint8_t> packet);

// Answer to the given questions, or an empty packet when none concerns this service.
std::vector<uint8_t> build_response(const Service &service, std::span<const Question> questions);
// Unsolicited announcement with all records; goodbye sends the same records with TTL 0.
std::vector<uint8_t> build_announcement(const Service &service, bool goodbye);

}
