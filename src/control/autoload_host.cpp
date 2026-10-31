// SPDX-License-Identifier: GPL-3.0-only
#include "control/autoload_host.hpp"
#include "app/psapp/autoload.hpp"
#include "src/platform/common.h"
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace prospero::autoload_host {
namespace fs = std::filesystem;
namespace list = psapp::autoload;

namespace {
constexpr const char *elf_name = "prospero_sunshine.elf";

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream text;
    text << file.rdbuf();
    out = text.str();
    return true;
}

// As the app writes it: through a temporary file, readable for the app afterwards.
bool write_file(const fs::path &path, const std::string &text) {
    fs::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!(file << text)) return false;
    }
    std::error_code ec;
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::group_write |
                                   fs::perms::others_read | fs::perms::others_write, ec);
    fs::rename(temporary, path, ec);
    return !ec;
}

fs::path host_elf() {
    return platf::appdata() / elf_name;
}

bool slot_current() {
    std::string installed, slot;
    return read_file(host_elf(), installed) && read_file(list::kSlotElf, slot) && installed == slot;
}

std::string sync_slot() {
    std::error_code ec;
    if (!fs::exists(host_elf(), ec)) return "no host ELF in the data folder";
    fs::create_directories(list::kSlotDir, ec);
    if (ec) return "cannot create the slot";
    if (!slot_current()) {
        std::string temporary = std::string(list::kSlotElf) + ".tmp";
        fs::copy_file(host_elf(), temporary, fs::copy_options::overwrite_existing, ec);
        if (!ec) fs::rename(temporary, list::kSlotElf, ec);
        if (ec) return "cannot copy the host to the slot";
    }
    char installed_at[40];
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    std::strftime(installed_at, sizeof installed_at, "%Y-%m-%dT%H:%M:%S+0000", &utc);
    return write_file(list::kSlotMeta, list::slot_meta(elf_name, installed_at)) ? "" : "cannot write the slot's meta.json";
}
}

std::string status() {
    std::string entries, config;
    read_file(list::kListPath, entries);
    const bool listed = list::contains(entries, list::kSlotKey);
    const bool manager = read_file(list::kConfigPath, config) && list::manager_autoload_enabled(config);
    return std::string("OK list=") + (listed ? "1" : "0") + " manager=" + (manager ? "1" : "0") +
           " slot=" + (listed && slot_current() ? "1" : "0");
}

std::string set(bool on) {
    std::error_code ec;
    if (!fs::exists("/data/pldmgr", ec)) return "ERR the Payload Manager is not installed";
    std::string entries;
    const bool have = read_file(list::kListPath, entries);
    if (!have && fs::exists(list::kListPath, ec)) return "ERR cannot read autoload.txt";
    if (on) {
        if (const std::string error = sync_slot(); !error.empty()) return "ERR " + error;
    }
    const std::string edited = on ? list::with_entry(entries, list::kSlotKey) : list::without_entry(entries, list::kSlotKey);
    if ((on || have) && edited != entries && !write_file(list::kListPath, edited)) return "ERR cannot write autoload.txt";
    return status();
}

}
