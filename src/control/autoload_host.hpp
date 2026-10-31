// SPDX-License-Identifier: GPL-3.0-only
// The Payload Manager's autoload list, for the app when it may not touch it. The manager saves its files
// for root only (mode 0600), and a title runs as another user; the host, a payload, runs as root. Same edits
// and same slot as the app's own path (app/src/psapp/host.cpp, Host::sync_slot).
#pragma once
#include <string>

namespace prospero::autoload_host {

// "OK list=<0|1> manager=<0|1> slot=<0|1>": the host is in the list, the manager's own autoload is on, the
// slot holds the host ELF of the data folder.
std::string status();
// Puts the host into the list with an up-to-date slot, or takes it out. "OK ..." or "ERR <why>".
std::string set(bool on);

}
