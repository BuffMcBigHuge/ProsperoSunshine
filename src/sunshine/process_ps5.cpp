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
constexpr int launch_mount_busy = static_cast<int>(0x80940033);

bool settings_app_closed() {
    std::ofstream(settings_app_quit).flush();
    char running[16];
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(100ms);
        if (ps_title_running(running) <= 0 || settings_app != running) return true;
    }
    return false;
}

// A title other than id that is still open, from the folder the system keeps for each running title. A game opened before the
// settings app stays open behind it, but isn't the running big app once the app is gone: starting another title could close
// it. A title that is just ending gets a moment to go. Empty when /mnt/sandbox can't be read.
std::string other_open_title(const std::string &id) {
    for (int i = 0;; i++) {
        std::string open;
        std::error_code ec;
        for (const auto &entry : std::filesystem::directory_iterator("/mnt/sandbox", ec)) {
            // <title id>_000: 9 characters, an underscore and an instance number.
            const auto name = entry.path().filename().string();
            if (name.size() != 13 || name[9] != '_') continue;
            const auto title = name.substr(0, 9);
            if (!title.starts_with("NPXS") && title != settings_app && title != id) open = title;
        }
        if (open.empty() || i == 8) return open;
        std::this_thread::sleep_for(250ms);
    }
}

// False when another title is open and stays so: the stream would show that one, not the title the client asked for, or
// starting the new one could close it.
bool start_title(const std::string &id) {
    char running[16];
    if (ps_title_running(running) > 0) {
        if (id == running) {
            BOOST_LOG(info) << "Title "sv << id << " is already in front"sv;
            return true;
        }
        if (settings_app != running || !settings_app_closed()) {
            BOOST_LOG(warning) << "Not starting "sv << id << ": "sv << running << " is in front and closing it could lose progress"sv;
            return false;
        }
    }
    if (const auto open = other_open_title(id); !open.empty()) {
        BOOST_LOG(warning) << "Not starting "sv << id << ": "sv << open << " is still open and starting another title could close it"sv;
        return false;
    }
    // Without a title the stream still shows the console.
    int rc = ps_title_launch(id.c_str());
    // ShadowMountPlus releases a title about a second after it ends, and refuses starts until then ("launch mount
    // unavailable ... Device busy" in its log). Seen right after the settings app closed itself, on firmware 10.20. Retried
    // only while nothing else is open: once ShadowMountPlus lets a start through, the system could close that title.
    for (auto until = std::chrono::steady_clock::now() + 3s;
         rc == launch_mount_busy && std::chrono::steady_clock::now() < until && other_open_title(id).empty();) {
        std::this_thread::sleep_for(500ms);
        rc = ps_title_launch(id.c_str());
    }
    if (rc < 0) BOOST_LOG(warning) << "Starting "sv << id << " failed: "sv << util::hex(rc).to_string_view();
    else BOOST_LOG(info) << "Started "sv << id;
    return true;
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
    if (!_app.cmd.empty() && !start_title(_app.cmd)) {
        // Refuse the launch, so the client doesn't name this title over a stream of the one in front. "PS5 Screen" still
        // streams whatever is in front.
        terminate();
        return 409;
    }
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
