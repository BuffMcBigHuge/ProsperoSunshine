// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/diag.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace psapp::diag
{

namespace
{

// The file's capture lines come from src/platform/ps5/capture.c; PS_CAPTURE_* from capture.h.
constexpr int kBusy = -2;
constexpr int kVr = -4;
constexpr const char *kPrefix = "ProsperoSunshine: ";

std::string field(const std::string &line, const std::string &name)
{
    const std::size_t start = line.find(" " + name + "=");
    if (start == std::string::npos)
        return {};
    const std::size_t value = start + name.size() + 2;
    return line.substr(value, line.find(' ', value) - value);
}

bool starts_with(const std::string &text, const char *prefix)
{
    return text.rfind(prefix, 0) == 0;
}

bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Replaces dotted quads in `text`.
std::string without_addresses(const std::string &text)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        if (is_digit(text[i]) && (i == 0 || !(is_digit(text[i - 1]) || text[i - 1] == '.')))
        {
            std::size_t at = i;
            int parts = 0;
            for (;;)
            {
                std::size_t digits = 0;
                while (at < text.size() && is_digit(text[at]) && digits < 4)
                {
                    ++at;
                    ++digits;
                }
                if (digits == 0 || digits > 3)
                    break;
                ++parts;
                if (parts == 4 || at >= text.size() || text[at] != '.')
                    break;
                ++at;
            }
            if (parts == 4 && (at >= text.size() || !is_digit(text[at])))
            {
                out += "<address>";
                i = at;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

std::string without_long_hex(const std::string &text)
{
    constexpr std::size_t kLongest = 16; // a firmware code or an address stays
    std::string out;
    std::size_t i = 0;
    while (i < text.size())
    {
        std::size_t end = i;
        while (end < text.size() && (is_hex(text[end]) || text[end] == '-'))
            ++end;
        if (end - i >= kLongest)
            out += "<id>";
        else
            out.append(text, i, end - i);
        if (end < text.size())
            out += text[end];
        i = end + 1;
    }
    return out;
}

} // namespace

std::string firmware_text(std::uint32_t firmware)
{
    char text[16];
    std::snprintf(text, sizeof text, "%x.%02x", firmware >> 24, (firmware >> 16) & 0xff);
    return text;
}

bool firmware_verified(std::uint32_t firmware)
{
    return (firmware & 0xffff0000u) == 0x13000000u;
}

HostLog parse_host_log(const std::string &text)
{
    HostLog log;
    std::string last_capture;
    std::string last_line;
    std::size_t at = 0;
    while (at < text.size())
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.empty())
            continue;
        if (starts_with(line, kPrefix))
            line.erase(0, std::strlen(kPrefix));
        last_line = line;
        if (starts_with(line, "host version="))
        {
            log.version = field(line, "version");
            log.firmware = static_cast<std::uint32_t>(std::strtoul(field(line, "firmware").c_str(), nullptr, 16));
        }
        else if (starts_with(line, "ra refusing firmware"))
        {
            log.refused = true;
        }
        else if (starts_with(line, "capture open start"))
        {
            log.capture = Capture::opening;
            log.last_step.clear();
        }
        else if (starts_with(line, "capture open ok"))
        {
            log.capture = Capture::ok;
            log.last_step.clear();
        }
        else if (starts_with(line, "capture open failed"))
        {
            log.capture = Capture::failed;
            // The step's name may hold spaces; "error=" comes next.
            const std::size_t step = line.find(" step=");
            const std::size_t after = line.find(" error=");
            if (step != std::string::npos && after != std::string::npos && after > step)
                log.failed_step = line.substr(step + 6, after - step - 6);
            log.failed_error = std::atoi(field(line, "error").c_str());
            log.failed_rc = field(line, "rc");
            log.last_step.clear();
        }
        else if (starts_with(line, "capture ") && log.capture == Capture::opening)
        {
            log.last_step = line.substr(8);
        }
    }
    log.stopped = last_line == "host stopped";
    return log;
}

Notice notice(std::uint32_t firmware, const HostLog &log, bool host_running)
{
    const std::string name = firmware != 0 ? "Firmware " + firmware_text(firmware) : std::string("This firmware");
    if (log.refused)
        return {name + " is not supported yet: the host cannot find the capture calls. See Diagnostics.", true};
    if (log.capture == Capture::failed)
    {
        if (log.failed_error == kVr)
            return {"The stream cannot run while PS VR is in use.", true};
        if (log.failed_error == kBusy)
            return {"Something else is using the console's capture. Close it, or restart the console.", true};
        return {"Capture failed at " + log.failed_step + " on " + name + ". See Diagnostics.", true};
    }
    if (log.capture == Capture::opening && !host_running)
        return {"The host went down while starting capture on " + name + ". See Diagnostics.", true};
    if (firmware != 0 && !firmware_verified(firmware))
        return {name + " is untested: only 13.00 is verified. See Diagnostics if streaming fails.", false};
    return {};
}

std::string scrub(const std::string &text, const std::vector<std::string> &names)
{
    std::string out = text;
    for (const std::string &name : names)
    {
        if (name.size() < 3)
            continue;
        for (std::size_t at = out.find(name); at != std::string::npos; at = out.find(name, at + 6))
            out.replace(at, name.size(), "<name>");
    }
    return without_long_hex(without_addresses(out));
}

std::string tail(const std::string &text, std::size_t count, std::size_t width)
{
    std::size_t start = text.size();
    if (start > 0 && text[start - 1] == '\n')
        --start;
    for (std::size_t lines = 0; start > 0; --start)
    {
        if (text[start - 1] == '\n' && ++lines == count)
            break;
    }
    std::string out;
    for (std::size_t at = start; at < text.size();)
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        out.append(text, at, end - at > width ? width : end - at);
        out += '\n';
        at = end + 1;
    }
    return out;
}

std::string errno_name(int error)
{
    const char *name = nullptr;
    switch (error)
    {
    case EPERM:
        name = "operation not permitted";
        break;
    case ENOENT:
        name = "no such file";
        break;
    case EIO:
        name = "I/O error";
        break;
    case EACCES:
        name = "permission denied";
        break;
    case ENOTDIR:
        name = "not a folder";
        break;
    case EISDIR:
        name = "is a folder";
        break;
    case EROFS:
        name = "read-only file system";
        break;
    case ENOSPC:
        name = "no space left";
        break;
    default:
        break;
    }
    return "errno " + std::to_string(error) + (name != nullptr ? std::string(" (") + name + ")" : std::string());
}

} // namespace psapp::diag
