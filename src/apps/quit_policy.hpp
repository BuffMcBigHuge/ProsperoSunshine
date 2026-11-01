// SPDX-License-Identifier: GPL-3.0-only
// When Moonlight's Terminate App closes the session's game (close_game_on_quit in sunshine.conf), and when a start of that game
// may follow. Portable, so that the tests can run it.
#pragma once
#include <chrono>
#include <string_view>

namespace prospero {

enum class QuitDecision { off, no_game, not_in_front, close };

// session_game: the game the last session started or found in front, empty for "PS5 Screen", a refused start or a failed
// one. It stays listed as running after the stream while it is in front, so a /cancel at any later time is meant for it.
constexpr QuitDecision quit_decision(bool enabled, std::string_view session_game, std::string_view in_front) {
    if (!enabled) return QuitDecision::off;
    if (session_game.empty()) return QuitDecision::no_game;
    if (in_front != session_game) return QuitDecision::not_in_front;
    return QuitDecision::close;
}

// ShadowMountPlus finalizes a title's exit once no /mnt/sandbox/<title>_* entry is left, after a second's grace, or after
// 10 s. The same title started before that runs, but ShadowMountPlus doesn't track it (measured on 10.20 with 1.7beta11), so
// a start of the game just closed waits until then, counted from when the close returned.
inline constexpr std::chrono::milliseconds release_margin{1500}, release_cap{11000};

constexpr bool released(std::chrono::milliseconds since_close, bool entry_left, std::chrono::milliseconds since_entries_gone) {
    return since_close >= release_cap || (!entry_left && since_entries_gone >= release_margin);
}

}  // namespace prospero
