// SPDX-License-Identifier: GPL-3.0-only
// The pure parts of the diagnostic report: what the host's breadcrumb file says (host_diag.log,
// src/platform/ps5/diag.h), the notice the home page shows for it, and the scrubbing that makes the
// report fit to post in a public issue.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psapp::diag
{

// Firmware as kernel_get_fw_version() and kern.sdk_version give it: 0x13000040 is 13.00.
std::string firmware_text(std::uint32_t firmware);
// The only firmware everything was measured on.
bool firmware_verified(std::uint32_t firmware);
// Whether an Experimental switch (its sunshine.conf key) counts on this firmware. Where the function is verified it is
// there anyway and the host ignores the switch: src/platform/ps5/experimental.h has the same rule. An unknown firmware
// (0) keeps the switch: the host still decides.
bool experimental_needed(std::uint32_t firmware, const std::string &key);

enum class Capture
{
    unknown,     // no attempt in the file
    opening,     // "open start" without an outcome: still going, or the process went down there
    ok,
    failed,
};

// One "experimental result=" line of the file: one try to open the capture while a switch counts.
struct Attempt
{
    bool ok = false;
    std::string trial;   // the functions it had to show: hevc, hdr, large, comma separated; "none"
    std::string codec;   // hevc, h264
    std::string bits;    // 8, 10
    std::string mode;    // 1920x1080
    std::string fps;
    std::string variant; // standard, minimal, full
    std::string step;    // where it failed: OpenVideo, Start, first frame, ... ; "none"
    std::string rc;      // the raw code as hex text; "0" when the step has none
};

struct HostLog
{
    std::uint32_t firmware = 0;    // from the header; 0 when there is none
    std::string version;
    bool refused = false;          // the host could not find the ResourceArbitrator calls
    Capture capture = Capture::unknown;
    std::string failed_step;       // "open failed step=": where it stopped
    int failed_error = 0;          // PS_CAPTURE_*
    std::string failed_rc;         // the last raw Avcap2 code, as hex text; "0" when none
    std::string last_step;         // the last capture line of an attempt without an outcome
    bool stopped = false;          // the last line says the host ended on its own
    std::string switches;          // the switches this host uses; empty when none is set
    std::string switches_set;      // what sunshine.conf had when it started, also where they don't count
    std::vector<Attempt> attempts; // this host's, the last kMaxAttempts
    std::vector<std::string> disabled; // functions off until the host restarts: hevc, hdr, large
    std::string pending_trial;     // an attempt without an outcome had these on trial
};

constexpr std::size_t kMaxAttempts = 24;

HostLog parse_host_log(const std::string &text);

// The report's part about the switches: which are set, how each attempt went, what is off now. `conf_switches` is what
// sunshine.conf holds now, which the host only reads when it starts. Only known fields, so nothing about anyone.
std::string experimental_report(const HostLog &log, const std::vector<std::string> &conf_switches);

// One line for the home page about this firmware and the last capture attempt; empty when there is
// nothing to say. `bad` says it is a failure, not a caution.
struct Notice
{
    std::string text;
    bool bad = false;
};
Notice notice(std::uint32_t firmware, const HostLog &log, bool host_running);

// The log with what could identify someone taken out: IPv4 addresses, `names` (paired devices and the
// console's name) and long hex runs (client ids, keys). Other text stays.
std::string scrub(const std::string &text, const std::vector<std::string> &names);
// The last `count` lines, each cut to `width` bytes.
std::string tail(const std::string &text, std::size_t count, std::size_t width = 200);
// A name for the errno values a file or socket call gives here.
std::string errno_name(int error);

} // namespace psapp::diag
