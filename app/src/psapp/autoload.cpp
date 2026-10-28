// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/autoload.hpp"

#include <vector>

namespace psapp::autoload
{

namespace
{

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> lines(std::string_view text)
{
    std::vector<std::string_view> out;
    while (!text.empty())
    {
        const std::size_t end = text.find('\n');
        out.push_back(text.substr(0, end));
        if (end == std::string_view::npos)
            break;
        text.remove_prefix(end + 1);
    }
    return out;
}

} // namespace

bool contains(std::string_view list, std::string_view key)
{
    for (std::string_view line : lines(list))
    {
        if (trim(line) == key)
            return true;
    }
    return false;
}

std::string with_entry(std::string_view list, std::string_view key)
{
    std::string out(list);
    if (contains(list, key))
        return out;
    if (!out.empty() && out.back() != '\n')
        out += '\n';
    out += key;
    out += '\n';
    return out;
}

std::string without_entry(std::string_view list, std::string_view key)
{
    std::string out;
    for (std::string_view line : lines(list))
    {
        if (trim(line) == key)
            continue;
        out += line;
        out += '\n';
    }
    return out;
}

bool manager_autoload_enabled(std::string_view config)
{
    bool enabled = false;
    for (std::string_view line : lines(config))
    {
        line = trim(line);
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || trim(line.substr(0, eq)) != "AUTOLOAD_ENABLED")
            continue;
        enabled = trim(line.substr(eq + 1)) == "1";
    }
    return enabled;
}

std::string slot_meta(std::string_view filename, std::string_view installed_at)
{
    std::string out = "{\n  \"schema\": 2,\n  \"kind\": \"local\",\n  \"filename\": \"";
    out += filename;
    out += "\",\n  \"installed_at\": \"";
    out += installed_at;
    out += "\",\n  \"origin\": \"web_upload\",\n  \"origin_detail\": \"ProsperoSunshine app\"\n}\n";
    return out;
}

} // namespace psapp::autoload
