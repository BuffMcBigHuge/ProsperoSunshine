// SPDX-License-Identifier: GPL-3.0-only
// Sunshine features without a PS5 counterpart: display reconfiguration and the desktop tray.
// The kept upstream sources call these; on the console they are deliberate no-ops.
#include "src/display_device.h"
#include "src/entry_handler.h"
#include "src/system_tray.h"
#include <csignal>

namespace lifetime {
// Upstream's entry_handler.cpp is replaced by main_ps5.cpp; stream.cpp still calls this on a
// fatal protocol state, exactly as upstream does.
void debug_trap() { std::raise(SIGTRAP); }
}

namespace display_device {
void configure_display(const config::video_t &, const rtsp_stream::launch_session_t &) {}
void revert_configuration() {}
}

namespace system_tray {
void update_tray_playing(std::string) {}
void update_tray_pausing(std::string) {}
void update_tray_stopped(std::string) {}
void update_tray_require_pin() {}
}
