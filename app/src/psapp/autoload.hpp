// SPDX-License-Identifier: GPL-3.0-only
// The Payload Manager's autoload list (itsPLK/ps5-payload-manager v0.6.3, src/autoload.c and
// src/slot.c). /data/pldmgr/autoload.txt holds one entry a line, run in order at boot after
// AUTOLOAD_DELAY (pldmgr_config.txt, when AUTOLOAD_ENABLED=1): "!<ms>" waits, anything else names a
// payload. A payload is a slot, "local/<name>" or "repo/<id>@<hash>", that is a folder under
// /data/pldmgr/payloads holding the ELF and a meta.json; the manager sends the ELF to the ELF loader
// on 127.0.0.1:9021. Absolute paths are accepted only under /data/pldmgr and USB drives, so the
// host gets a local slot of its own. A file with a line over 511 bytes, a ',' or a '\r' is refused
// as a whole by the manager, so the edits below never add one.
#pragma once

#include <string>
#include <string_view>

namespace psapp::autoload
{

constexpr const char *kListPath = "/data/pldmgr/autoload.txt";
constexpr const char *kConfigPath = "/data/pldmgr/pldmgr_config.txt";
constexpr const char *kSlotKey = "local/prospero-sunshine";
constexpr const char *kSlotDir = "/data/pldmgr/payloads/local/prospero-sunshine";
constexpr const char *kSlotElf = "/data/pldmgr/payloads/local/prospero-sunshine/prospero_sunshine.elf";
constexpr const char *kSlotMeta = "/data/pldmgr/payloads/local/prospero-sunshine/meta.json";

// Pure text edits of autoload.txt; `key` is a whole line.
bool contains(std::string_view list, std::string_view key);
// The list with `key` as its last entry (unchanged when it is there already).
std::string with_entry(std::string_view list, std::string_view key);
// The list without any line that is `key`; every other line stays as it was.
std::string without_entry(std::string_view list, std::string_view key);

// The manager's own switch for all of autoload (AUTOLOAD_ENABLED in pldmgr_config.txt); missing
// counts as off, as in the manager's config.c.
bool manager_autoload_enabled(std::string_view config);

// The slot's meta.json (schema 2, a local slot) for an ELF file name and an ISO-8601 time.
std::string slot_meta(std::string_view filename, std::string_view installed_at);

} // namespace psapp::autoload
