// SPDX-License-Identifier: GPL-3.0-only
#include "apps/quit_policy.hpp"
#include <cstdio>

using namespace std::chrono_literals;
using prospero::quit_decision;
using prospero::QuitDecision;

static_assert(quit_decision(false, "PPSA03527", "PPSA03527") == QuitDecision::off);
static_assert(quit_decision(true, "", "PPSA03527") == QuitDecision::no_game);  // "PS5 Screen", 409 or failed start
static_assert(quit_decision(true, "PPSA03527", "PPSA03671") == QuitDecision::not_in_front);
static_assert(quit_decision(true, "PPSA03527", "PPSA47989") == QuitDecision::not_in_front);  // behind the settings app
static_assert(quit_decision(true, "PPSA03527", "") == QuitDecision::not_in_front);  // home screen
static_assert(quit_decision(true, "PPSA03527", "PPSA03527") == QuitDecision::close);

static_assert(!prospero::released(1s, true, 0ms));        // the title's folder is still there
static_assert(!prospero::released(2s, false, 1s));        // gone, but within the grace
static_assert(prospero::released(3s, false, 1500ms));
static_assert(prospero::released(11s, true, 0ms));        // ShadowMountPlus gives up waiting after 10 s
static_assert(!prospero::released(10900ms, true, 0ms));

int main() {
    std::puts("PASS: quit policy (setting off, no game, title not in front, release before a restart)");
    return 0;
}
