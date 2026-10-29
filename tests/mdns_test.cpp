// SPDX-License-Identifier: GPL-3.0-only
#include "net/mdns.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
using namespace prospero::mdns;

namespace {

void put_name(std::vector<uint8_t> &p, std::string_view dotted) {
    while (!dotted.empty()) {
        const auto dot = dotted.find('.');
        const auto label = dotted.substr(0, dot);
        p.push_back(static_cast<uint8_t>(label.size()));
        p.insert(p.end(), label.begin(), label.end());
        if (dot == std::string_view::npos) break;
        dotted.remove_prefix(dot + 1);
    }
    p.push_back(0);
}

std::vector<uint8_t> header(uint16_t flags, uint16_t questions) {
    return {0, 0, static_cast<uint8_t>(flags >> 8), static_cast<uint8_t>(flags), static_cast<uint8_t>(questions >> 8),
            static_cast<uint8_t>(questions), 0, 0, 0, 0, 0, 0};
}

std::vector<uint8_t> query(std::string_view name, uint16_t type) {
    auto p = header(0, 1);
    put_name(p, name);
    p.insert(p.end(), {static_cast<uint8_t>(type >> 8), static_cast<uint8_t>(type), 0, 1});
    return p;
}

Service test_service() {
    Service s;
    s.instance = "PS5 Test";
    s.host = host_label(s.instance);
    s.ipv4 = {192, 168, 1, 50};
    return s;
}

uint16_t be16(const std::vector<uint8_t> &p, size_t at) { return static_cast<uint16_t>((p[at] << 8) | p[at + 1]); }

bool contains(const std::vector<uint8_t> &p, const std::vector<uint8_t> &needle) {
    return std::search(p.begin(), p.end(), needle.begin(), needle.end()) != p.end();
}

}

int main() {
    assert(host_label("PS5 Test") == "PS5-Test");
    assert(host_label("--") == "ps5");
    assert(host_label(std::string(100, 'a')).size() == 63);
    assert(instance_label(std::string(70, 'x')).size() == 63);
    {  // never cut inside a multi-byte character: 62 ASCII bytes + "ä" (2 bytes) + more
        const auto cut = instance_label(std::string(62, 'x') + "\xc3\xa4" + "yy");
        assert(cut.size() == 62);
    }

    // PTR query: PTR answer, SRV/TXT/A as additional records.
    const auto service = test_service();
    auto questions = parse_questions(query("_NVStream._tcp.local", type_ptr));
    assert(questions.size() == 1 && questions[0].name == "_nvstream._tcp.local" && questions[0].type == type_ptr);
    auto response = build_response(service, questions);
    assert(response.size() > 12);
    assert(response[2] == 0x84 && be16(response, 6) == 1 && be16(response, 10) == 3);
    std::vector<uint8_t> ptr_target{8};
    ptr_target.insert(ptr_target.end(), {'P', 'S', '5', ' ', 'T', 'e', 's', 't'});
    put_name(ptr_target, "_nvstream._tcp.local");
    assert(contains(response, ptr_target));
    assert(contains(response, {192, 168, 1, 50}));
    assert(contains(response, {0xbb, 0x75}));  // port 47989 inside the SRV record
    assert(contains(response, {0x80, 0x01}));  // cache-flush class on SRV/TXT/A

    // SRV query for the instance: SRV answer plus A additional.
    questions = parse_questions(query("ps5 test._nvstream._tcp.local", type_srv));
    response = build_response(service, questions);
    assert(be16(response, 6) == 1 && be16(response, 10) == 1);

    // A query for the host name.
    questions = parse_questions(query("ps5-test.local", type_a));
    response = build_response(service, questions);
    assert(be16(response, 6) == 1 && be16(response, 10) == 0 && contains(response, {192, 168, 1, 50}));

    // Service enumeration points at _nvstream._tcp.
    response = build_response(service, parse_questions(query("_services._dns-sd._udp.local", type_ptr)));
    assert(be16(response, 6) == 1);

    // Name compression: second question points back at the first name's ".local" suffix.
    {
        auto p = header(0, 2);
        put_name(p, "_nvstream._tcp.local");
        p.insert(p.end(), {0, 12, 0, 1});
        p.push_back(6);
        for (char c : std::string_view("ps5-fo")) p.push_back(static_cast<uint8_t>(c));
        p.insert(p.end(), {0xc0, 12 + 1 + 9 + 1 + 4});  // pointer to "local" in the first name
        p.insert(p.end(), {0, 1, 0, 1});
        const auto qs = parse_questions(p);
        assert(qs.size() == 2);
        assert(qs[0].name == "_nvstream._tcp.local" && qs[1].name == "ps5-fo.local" && qs[1].type == type_a);
    }

    // Unrelated queries, responses, garbage and pointer loops get no answer.
    assert(build_response(service, parse_questions(query("_airplay._tcp.local", type_ptr))).empty());
    assert(build_response(service, parse_questions(query("_nvstream._tcp.local", type_txt))).empty());
    assert(build_response(service, parse_questions(query("other.local", type_a))).empty());
    auto as_response = query("_nvstream._tcp.local", type_ptr);
    as_response[2] = 0x84;
    assert(parse_questions(as_response).empty());
    assert(parse_questions({}).empty());
    auto truncated = query("_nvstream._tcp.local", type_ptr);
    truncated.resize(truncated.size() - 3);
    assert(parse_questions(truncated).empty());
    auto loop = header(0, 1);
    loop.insert(loop.end(), {0xc0, 12, 0, 12, 0, 1});
    assert(parse_questions(loop).empty());

    // Announcement carries all four records; goodbye has TTL 0 everywhere.
    const auto announce = build_announcement(service, false);
    assert(be16(announce, 6) == 4 && be16(announce, 10) == 0);
    const auto goodbye = build_announcement(service, true);
    assert(goodbye.size() == announce.size());
    assert(contains(announce, {0, 0, 0x11, 0x94}));   // TTL 4500 on PTR/TXT
    assert(!contains(goodbye, {0, 0, 0x11, 0x94}));
    assert(!contains(goodbye, {0, 0, 0, 0x78}));       // TTL 120 gone as well
    std::cout << "mdns_test ok\n";
}
