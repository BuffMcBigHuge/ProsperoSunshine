// SPDX-License-Identifier: GPL-3.0-only
#include "control/device_modes.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace prospero;

int main() {
    assert(format_mode(1280, 720, 60) == "1280x720@60");
    assert(format_mode(3840, 2160, 144) == "3840x2160@144");
    assert(format_mode(0, 0, 0).empty());
    assert(format_mode(1280, 720, 0).empty());

    const auto dir = std::filesystem::temp_directory_path() / ("device_modes_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "device_modes.txt";
    {
        DeviceModes modes(file);  // no file yet
        assert(modes.mode("a").empty());
        modes.remember("a", "1920x1080@60");
        modes.remember("b", "1280x720@30");
        modes.remember("a", "3840x2160@60");
        modes.remember("", "1920x1080@60");
        modes.remember("c", "");
        assert(modes.mode("a") == "3840x2160@60");
        assert(modes.mode("c").empty());
    }
    {
        DeviceModes modes(file);  // survives a restart
        assert(modes.mode("a") == "3840x2160@60");
        assert(modes.mode("b") == "1280x720@30");
        assert(modes.mode("").empty());
        modes.forget("a");
        modes.forget("never there");
        assert(modes.mode("a").empty());
    }
    {
        DeviceModes modes(file);
        assert(modes.mode("a").empty());
        assert(modes.mode("b") == "1280x720@30");
    }
    assert(!std::filesystem::exists(dir / "device_modes.txt.new"));
    std::ofstream(file) << "lonely\nb 1280x720@30\ntoo many fields\n\n";
    {
        DeviceModes modes(file);  // a damaged file costs only its bad lines
        assert(modes.mode("b") == "1280x720@30");
        assert(modes.mode("lonely").empty());
        assert(modes.mode("too").empty());
    }
    std::filesystem::remove_all(dir);
    std::cout << "PASS: device modes (kept per device, across restarts, gone when removed)\n";
}
