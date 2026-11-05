// SPDX-License-Identifier: GPL-3.0-only
// The host role belongs to a paired device, not to a gamepad slot (src/input/pad_roles.hpp, GitHub issue 15).
#include "input/pad_roles.hpp"

#include <cassert>
#include <iostream>

using prospero::PadRoles;

int main() {
    {
        // The first device's first pad is the host; its second controller and another device's pad are guests.
        PadRoles<4> roles;
        assert(roles.attach(0, "A"));
        assert(!roles.attach(1, "A"));
        assert(!roles.attach(2, "B"));
        assert(roles.is_host(0) && !roles.is_host(1) && !roles.is_host(2));
    }
    {
        // The host leaves: nobody inherits the role, not the guest that gets slot 0 next. The host's device takes it back.
        PadRoles<4> roles;
        assert(roles.attach(0, "A"));
        assert(!roles.attach(1, "B"));
        roles.detach(0);
        assert(roles.host_away());
        assert(!roles.attach(0, "C"));
        assert(!roles.is_host(0) && !roles.is_host(1));
        assert(roles.attach(2, "A"));
        assert(roles.is_host(2) && !roles.host_away());
    }
    {
        // Once no pad is left, the role is free again: the next device takes it.
        PadRoles<4> roles;
        assert(roles.attach(0, "A"));
        assert(!roles.attach(1, "B"));
        roles.detach(0);
        roles.detach(1);
        assert(!roles.host_away());
        assert(roles.attach(1, "B"));
    }
    {
        // The host's controller drops and comes back while a guest plays: it is the host again.
        PadRoles<4> roles;
        assert(roles.attach(0, "A"));
        assert(!roles.attach(1, "B"));
        roles.detach(0);
        assert(roles.attach(0, "A"));
    }
    {
        // A device the host doesn't know only takes the role while it is free, and never takes it back.
        PadRoles<4> roles;
        assert(roles.attach(0, ""));
        assert(!roles.attach(1, ""));
        roles.detach(0);
        assert(!roles.attach(0, ""));
        PadRoles<4> known;
        assert(known.attach(0, "A"));
        assert(!known.attach(1, ""));
        known.detach(0);
        assert(!known.attach(0, ""));
        assert(known.host_away());
    }
    {
        // Attaching a slot again replaces it, and slots out of range are ignored.
        PadRoles<2> roles;
        assert(roles.attach(0, "A"));
        assert(roles.attach(0, "A"));
        assert(!roles.attach(1, "B"));
        assert(!roles.attach(2, "A"));
        assert(!roles.is_host(2));
        roles.detach(5);
        assert(roles.is_host(0));
    }
    std::cout << "PASS: pad roles (host by device, no inheritance, back after a reconnect, free once no pad is left)\n";
}
