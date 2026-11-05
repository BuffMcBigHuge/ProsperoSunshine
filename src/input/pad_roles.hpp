// SPDX-License-Identifier: GPL-3.0-only
// Who is the host among the virtual pads (GitHub issue 15). The host's pad plays as the console's user and has PS and
// Create; every other pad is a guest, a player the console asks about, who can only play. The role belongs to a paired
// device, not to a slot:
// - While no pad is attached, the role is free, and the device whose pad attaches first takes it.
// - One pad holds it at a time: a second controller on the host's device is a guest like any other.
// - When the host's pad goes, the role stays with its device. Nobody inherits it, and that device's next pad takes it
//   back, also after the device reconnects. Once no pad is left, the role is free again.
// - A pad of a device the host doesn't know only takes the role while it is free, as slot 0 did before.
// Portable, tested in tests/pad_roles_test.cpp.
#pragma once
#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace prospero {

template <std::size_t N>
class PadRoles {
public:
    // Pad `nr` attaches for the paired device `owner` (empty when unknown). Returns whether it is the host's pad.
    bool attach(std::size_t nr, const std::string &owner) {
        if (nr >= N) return false;
        detach(nr);
        const bool free = !host_;
        if (free) host_ = owner;
        const bool host = host_pad_ < 0 && (free || (!owner.empty() && *host_ == owner));
        attached_[nr] = true;
        if (host) host_pad_ = static_cast<int>(nr);
        return host;
    }

    void detach(std::size_t nr) {
        if (nr >= N || !attached_[nr]) return;
        attached_[nr] = false;
        if (host_pad_ == static_cast<int>(nr)) host_pad_ = -1;
        for (const bool on : attached_)
            if (on) return;
        host_.reset();
    }

    bool is_host(std::size_t nr) const { return nr < N && host_pad_ == static_cast<int>(nr); }
    // Whether a device holds the role while its pad is gone (for the log).
    bool host_away() const { return host_ && host_pad_ < 0; }

private:
    std::array<bool, N> attached_{};
    std::optional<std::string> host_;  // the device holding the role; empty optional = free
    int host_pad_ = -1;
};

}  // namespace prospero
