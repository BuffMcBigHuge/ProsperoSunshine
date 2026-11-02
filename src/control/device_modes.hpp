// SPDX-License-Identifier: GPL-3.0-only
// The mode each paired device last asked for, for the app's device list. A file of its own, one
// "<uuid> <mode>" a line: Sunshine rewrites its pairing file without a temporary one, and a launch
// should not put the pairings at risk for a line of information.
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

namespace prospero {

// "1280x720@60", or "" for a request without a usable mode.
inline std::string format_mode(int width, int height, int fps) {
    if (width <= 0 || height <= 0 || fps <= 0) return {};
    return std::to_string(width) + 'x' + std::to_string(height) + '@' + std::to_string(fps);
}

class DeviceModes {
public:
    explicit DeviceModes(std::filesystem::path file) : file_(std::move(file)) {
        std::ifstream in(file_);
        for (std::string line; std::getline(in, line);) {
            std::istringstream fields(line);
            std::string uuid, mode, extra;
            if (fields >> uuid >> mode && !(fields >> extra)) modes_[uuid] = mode;
        }
    }

    void remember(const std::string &uuid, const std::string &mode) {
        if (uuid.empty() || mode.empty()) return;
        std::scoped_lock lock(mutex_);
        const auto it = modes_.find(uuid);
        if (it != modes_.end() && it->second == mode) return;
        modes_[uuid] = mode;
        save();
    }

    void forget(const std::string &uuid) {
        std::scoped_lock lock(mutex_);
        if (modes_.erase(uuid) > 0) save();
    }

    std::string mode(const std::string &uuid) const {
        std::scoped_lock lock(mutex_);
        const auto it = modes_.find(uuid);
        return it == modes_.end() ? std::string() : it->second;
    }

private:
    void save() const {
        auto temporary = file_;
        temporary += ".new";
        {
            std::ofstream out(temporary, std::ios::trunc);
            for (const auto &[uuid, mode] : modes_) out << uuid << ' ' << mode << '\n';
            if (!out.flush()) return;
        }
        std::error_code ec;
        std::filesystem::rename(temporary, file_, ec);
    }

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::map<std::string, std::string> modes_;
};

}
