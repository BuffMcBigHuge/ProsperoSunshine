// SPDX-License-Identifier: GPL-3.0-only
// Replaces Sunshine's process.cpp. The host offers exactly one application, the live PS5 screen;
// "launching" it starts nothing, it only marks a session as running so nvhttp/stream can proceed.
#include "src/logging.h"
#include "src/process.h"
#include <fstream>

using namespace std::literals;

// assets/ps5_screen.png, embedded by CMakeLists.txt.
extern const unsigned char prospero_cover_png[];
extern const std::size_t prospero_cover_png_size;

namespace proc {
proc_t proc;

namespace {
constexpr int screen_app_id = 1;

std::filesystem::path cover_path() { return platf::appdata() / "assets" / "ps5_screen.png"; }

// Clients fetch box art for every app; an empty answer makes some (Moonlight Web) drop the list.
void install_cover() {
    std::error_code ec;
    const auto path = cover_path();
    if (std::filesystem::file_size(path, ec) == prospero_cover_png_size) return;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary)
        .write(reinterpret_cast<const char *>(prospero_cover_png), static_cast<std::streamsize>(prospero_cover_png_size));
}

std::vector<ctx_t> screen_app() {
    ctx_t app{};
    app.name = "PS5 Screen";
    app.id = std::to_string(screen_app_id);
    app.elevated = false;
    app.auto_detach = true;
    app.wait_all = false;
    app.exit_timeout = 5s;
    std::vector<ctx_t> apps;
    apps.push_back(std::move(app));
    return apps;
}

class proc_deinit_t final : public platf::deinit_t {
public:
    ~proc_deinit_t() override { proc.terminate(); }
};
}

std::unique_ptr<platf::deinit_t> init() {
    install_cover();
    proc = proc_t({}, screen_app());
    return std::make_unique<proc_deinit_t>();
}

void refresh(const std::string &) {}

int proc_t::execute(int app_id, std::shared_ptr<rtsp_stream::launch_session_t>) {
    terminate();
    const auto app = std::find_if(_apps.begin(), _apps.end(), [&](const ctx_t &a) { return a.id == std::to_string(app_id); });
    if (app == _apps.end()) {
        BOOST_LOG(error) << "Couldn't find app with ID ["sv << app_id << ']';
        return 404;
    }
    _app_id = app_id;
    _app = *app;
    _app_launch_time = std::chrono::steady_clock::now();
    placebo = true;
    BOOST_LOG(info) << "Session app ["sv << _app.name << "] marked running"sv;
    return 0;
}

int proc_t::running() { return placebo ? _app_id : 0; }

void proc_t::terminate() {
    if (placebo) BOOST_LOG(info) << "Session app ["sv << _app.name << "] stopped"sv;
    placebo = false;
    _app_id = -1;
}

const std::vector<ctx_t> &proc_t::get_apps() const { return _apps; }
std::vector<ctx_t> &proc_t::get_apps() { return _apps; }

std::string proc_t::get_app_image(int) { return cover_path().string(); }

std::string proc_t::get_last_run_app_name() { return _app.name; }

void proc_t::update_apps_and_env(proc_t &&other) { _apps = std::move(other._apps); }

proc_t::~proc_t() = default;
}
