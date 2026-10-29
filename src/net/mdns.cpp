// SPDX-License-Identifier: GPL-3.0-only
#include "net/mdns.hpp"
#include <algorithm>
#include <cctype>

namespace prospero::mdns {
namespace {

constexpr std::string_view service_type = "_nvstream._tcp.local";
constexpr std::string_view enumeration = "_services._dns-sd._udp.local";
constexpr uint32_t ttl_service = 4500, ttl_host = 120;

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

void put16(std::vector<uint8_t> &out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

void put32(std::vector<uint8_t> &out, uint32_t v) {
    put16(out, static_cast<uint16_t>(v >> 16));
    put16(out, static_cast<uint16_t>(v));
}

// Name as labels. The instance label may contain dots, so it is passed on its own.
void put_labels(std::vector<uint8_t> &out, std::string_view instance, std::string_view rest) {
    auto label = [&](std::string_view l) {
        l = l.substr(0, 63);
        out.push_back(static_cast<uint8_t>(l.size()));
        out.insert(out.end(), l.begin(), l.end());
    };
    if (!instance.empty()) label(instance);
    while (!rest.empty()) {
        const auto dot = rest.find('.');
        label(rest.substr(0, dot));
        if (dot == std::string_view::npos) break;
        rest.remove_prefix(dot + 1);
    }
    out.push_back(0);
}

// One resource record; `body` writes the RDATA.
template <class F>
void put_record(std::vector<uint8_t> &out, std::string_view instance, std::string_view rest, uint16_t type,
                bool cache_flush, uint32_t ttl, F body) {
    put_labels(out, instance, rest);
    put16(out, type);
    put16(out, cache_flush ? 0x8001 : 0x0001);
    put32(out, ttl);
    const auto length_at = out.size();
    put16(out, 0);
    body();
    const auto length = out.size() - length_at - 2;
    out[length_at] = static_cast<uint8_t>(length >> 8);
    out[length_at + 1] = static_cast<uint8_t>(length);
}

struct Records {
    bool ptr = false, srv = false, txt = false, a = false;
};

// Answer and additional sections as DNS-SD recommends: a PTR answer carries SRV, TXT and A along,
// an SRV answer carries A.
struct Plan {
    Records answer, extra;
    bool enumeration = false;
};

void put_records(std::vector<uint8_t> &out, const Service &s, const Records &r, bool goodbye) {
    const auto ttl = [&](uint32_t normal) { return goodbye ? 0u : normal; };
    const auto host = s.host + ".local";
    const auto instance = instance_label(s.instance);
    if (r.ptr)
        put_record(out, {}, service_type, type_ptr, false, ttl(ttl_service),
                   [&] { put_labels(out, instance, service_type); });
    if (r.srv)
        put_record(out, instance, service_type, type_srv, true, ttl(ttl_host), [&] {
            put16(out, 0);  // priority
            put16(out, 0);  // weight
            put16(out, s.port);
            put_labels(out, {}, host);
        });
    if (r.txt)
        put_record(out, instance, service_type, type_txt, true, ttl(ttl_service),
                   [&] { out.push_back(0); });  // one empty string = no attributes
    if (r.a)
        put_record(out, {}, host, type_a, true, ttl(ttl_host),
                   [&] { out.insert(out.end(), s.ipv4.begin(), s.ipv4.end()); });
}

int count(const Records &r) { return r.ptr + r.srv + r.txt + r.a; }

std::vector<uint8_t> assemble(const Service &s, const Plan &plan, bool goodbye) {
    std::vector<uint8_t> out{0, 0, 0x84, 0};  // id 0, response + authoritative
    put16(out, 0);
    put16(out, static_cast<uint16_t>(count(plan.answer) + (plan.enumeration ? 1 : 0)));
    put16(out, 0);
    put16(out, static_cast<uint16_t>(count(plan.extra)));
    if (plan.enumeration)
        put_record(out, {}, enumeration, type_ptr, false, ttl_service, [&] { put_labels(out, {}, service_type); });
    put_records(out, s, plan.answer, goodbye);
    put_records(out, s, plan.extra, goodbye);
    return out;
}

// Reads a (possibly compressed) name at `pos` and advances pos past its in-place part.
bool read_name(std::span<const uint8_t> p, size_t &pos, std::string &name) {
    name.clear();
    size_t cursor = pos;
    bool jumped = false;
    for (int steps = 0; steps < 128; ++steps) {
        if (cursor >= p.size()) return false;
        const uint8_t len = p[cursor];
        if ((len & 0xc0) == 0xc0) {
            if (cursor + 1 >= p.size()) return false;
            const size_t target = (static_cast<size_t>(len & 0x3f) << 8) | p[cursor + 1];
            if (!jumped) pos = cursor + 2;
            jumped = true;
            cursor = target;
        } else if (len & 0xc0) {
            return false;
        } else if (len == 0) {
            if (!jumped) pos = cursor + 1;
            return true;
        } else {
            if (cursor + 1 + len > p.size() || name.size() > 255) return false;
            if (!name.empty()) name += '.';
            name.append(reinterpret_cast<const char *>(&p[cursor + 1]), len);
            cursor += 1 + len;
        }
    }
    return false;  // pointer loop
}

}

std::string host_label(std::string_view name) {
    std::string out;
    for (unsigned char c : name) out += std::isalnum(c) ? static_cast<char>(c) : '-';
    if (out.size() > 63) out.resize(63);
    while (!out.empty() && out.back() == '-') out.pop_back();
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    return out.empty() ? "ps5" : out;
}

std::string instance_label(std::string_view name) {
    if (name.size() <= 63) return std::string(name);
    size_t cut = 63;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xc0) == 0x80) --cut;
    return std::string(name.substr(0, cut));
}

std::vector<Question> parse_questions(std::span<const uint8_t> p) {
    std::vector<Question> questions;
    if (p.size() < 12 || (p[2] & 0x80)) return questions;  // too short or a response
    const size_t count = (static_cast<size_t>(p[4]) << 8) | p[5];
    size_t pos = 12;
    for (size_t i = 0; i < count; ++i) {
        Question q;
        if (!read_name(p, pos, q.name) || pos + 4 > p.size()) return {};
        q.name = lower(q.name);
        q.type = static_cast<uint16_t>((p[pos] << 8) | p[pos + 1]);
        pos += 4;  // type + class; the unicast-response bit is ignored, we always answer by multicast
        questions.push_back(std::move(q));
    }
    return questions;
}

std::vector<uint8_t> build_response(const Service &s, std::span<const Question> questions) {
    const auto instance = lower(instance_label(s.instance) + "." + std::string(service_type));
    const auto host = lower(s.host + ".local");
    Plan plan;
    for (const auto &q : questions) {
        const bool any = q.type == type_any;
        if (q.name == enumeration && (q.type == type_ptr || any)) plan.enumeration = true;
        if (q.name == service_type && (q.type == type_ptr || any)) {
            plan.answer.ptr = true;
            plan.extra = {false, true, true, true};
        }
        if (q.name == instance) {
            if (q.type == type_srv || any) {
                plan.answer.srv = true;
                plan.extra.a = true;
            }
            if (q.type == type_txt || any) plan.answer.txt = true;
        }
        if (q.name == host && (q.type == type_a || any)) plan.answer.a = true;
    }
    // What is already an answer is not repeated as additional record.
    plan.extra = {plan.extra.ptr && !plan.answer.ptr, plan.extra.srv && !plan.answer.srv,
                  plan.extra.txt && !plan.answer.txt, plan.extra.a && !plan.answer.a};
    if (!plan.enumeration && count(plan.answer) == 0) return {};
    return assemble(s, plan, false);
}

std::vector<uint8_t> build_announcement(const Service &s, bool goodbye) {
    Plan plan;
    plan.answer = {true, true, true, true};
    return assemble(s, plan, goodbye);
}

}
