// SPDX-License-Identifier: GPL-3.0-only
// Replaces Sunshine's process.cpp. The host offers the live PS5 screen and the console's installed games. "Launching" the
// screen starts nothing, it only marks a session as running so nvhttp/stream can proceed. A game starts unless another title
// is in front. Nothing here ever closes a game, because that would lose unsaved progress: ending a session leaves it running.
#include "apps/titles.hpp"
#include "platform/ps5/title_launch.h"
#include "src/logging.h"
#include "src/process.h"
#include <fstream>
#include <thread>

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

ctx_t make_app(std::string name, int id) {
    ctx_t app{};
    app.name = std::move(name);
    app.id = std::to_string(id);
    app.elevated = false;
    app.auto_detach = true;
    app.wait_all = false;
    app.exit_timeout = 5s;
    return app;
}

// A game keeps its title ID in cmd.
std::vector<ctx_t> all_apps() {
    std::vector<ctx_t> apps;
    // Moonlight sorts by name and shows no order of the host's. The space, which it doesn't trim or show, puts this one first.
    apps.push_back(make_app(" PS5 Screen", screen_app_id));
    for (const auto &title : prospero::list_titles("/user")) {
        auto app = make_app(title.name, title.app_id);
        app.cmd = title.id;
        app.image_path = title.cover.string();
        apps.push_back(std::move(app));
    }
    return apps;
}

// The settings app ends itself when this file appears. It has to go first: a title started while it is alive once blocked
// every later start until a restart (STATUS.md).
constexpr auto settings_app = "PPSA47989"sv;
constexpr auto settings_app_quit = "/data/prosperosunshine/app.quit";

bool settings_app_closed() {
    std::ofstream(settings_app_quit).flush();
    char running[16];
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(100ms);
        if (ps_title_running(running) <= 0 || settings_app != running) return true;
    }
    return false;
}

void start_title(const std::string &id) {
    char running[16];
    if (ps_title_running(running) > 0) {
        if (id == running) {
            BOOST_LOG(info) << "Title "sv << id << " is already in front"sv;
            return;
        }
        if (settings_app != running || !settings_app_closed()) {
            BOOST_LOG(warning) << "Not starting "sv << id << ": "sv << running << " is in front and closing it could lose progress"sv;
            return;
        }
    }
    // Without a title the stream still shows the console.
    const int rc = ps_title_launch(id.c_str());
    if (rc < 0) BOOST_LOG(warning) << "Starting "sv << id << " failed: "sv << util::hex(rc).to_string_view();
    else BOOST_LOG(info) << "Started "sv << id;
}

class proc_deinit_t final : public platf::deinit_t {
public:
    ~proc_deinit_t() override { proc.terminate(); }
};
}

std::unique_ptr<platf::deinit_t> init() {
    install_cover();
    proc = proc_t({}, all_apps());
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
    if (!_app.cmd.empty()) start_title(_app.cmd);
    return 0;
}

int proc_t::running() { return placebo ? _app_id : 0; }

void proc_t::terminate() {
    if (placebo) BOOST_LOG(info) << "Session app ["sv << _app.name << "] stopped"sv;
    placebo = false;
    _app_id = -1;
}

const std::vector<ctx_t> &proc_t::get_apps() const { return _apps; }
// nvhttp asks through this one for the client's list, so a game installed meanwhile shows up.
std::vector<ctx_t> &proc_t::get_apps() {
    _apps = all_apps();
    return _apps;
}

std::string proc_t::get_app_image(int app_id) {
    const auto app = std::find_if(_apps.begin(), _apps.end(), [&](const ctx_t &a) { return a.id == std::to_string(app_id); });
    if (app != _apps.end() && !app->image_path.empty() && std::ifstream(app->image_path).good()) return app->image_path;
    return cover_path().string();
}

std::string proc_t::get_last_run_app_name() { return _app.name.starts_with(' ') ? _app.name.substr(1) : _app.name; }

void proc_t::update_apps_and_env(proc_t &&other) { _apps = std::move(other._apps); }

proc_t::~proc_t() = default;
}
