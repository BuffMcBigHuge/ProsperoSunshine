// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/host.hpp"

#include "app/psapp/autoload.hpp"
#include "app/psapp/io.hpp"
#include "platform/ps5/system.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <pthread.h>

namespace psapp
{

namespace
{

bool is_end(const std::string &line)
{
    return line == "END" || line.rfind("ERR", 0) == 0;
}

bool any_line(const std::string &)
{
    return true;
}

// The value of `name=` in a STATUS line ("OK running=1 app=PS5 Screen pending=0"); app= may hold
// spaces, so a value runs to the next " <word>=".
std::string status_field(const std::string &line, const std::string &name)
{
    const std::size_t start = line.find(" " + name + "=");
    if (start == std::string::npos)
        return {};
    const std::size_t value = start + name.size() + 2;
    std::size_t end = value;
    for (;;)
    {
        const std::size_t space = line.find(' ', end);
        if (space == std::string::npos)
            return line.substr(value);
        const std::size_t eq = line.find('=', space);
        const std::size_t next_space = line.find(' ', space + 1);
        if (eq != std::string::npos && (next_space == std::string::npos || eq < next_space))
            return line.substr(value, space - value);
        end = space + 1;
    }
}

// UTC as ISO 8601. The title's libc has no localtime_r, so the date is worked out here
// (days to civil date, Howard Hinnant's algorithm).
std::string iso_time()
{
    const long long now = static_cast<long long>(std::time(nullptr));
    long long days = now / 86400;
    const long long seconds = now % 86400;
    days += 719468;
    const long long era = days / 146097;
    const long long doe = days - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long day = doy - (153 * mp + 2) / 5 + 1;
    const long long month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    char text[40];
    std::snprintf(text, sizeof text, "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld+0000", year, month, day,
                  seconds / 3600, seconds / 60 % 60, seconds % 60);
    return text;
}

void sleep_ms(int ms)
{
    hui::sys::sleep_us(static_cast<std::uint32_t>(ms) * 1000u);
}

} // namespace

Host::Host() = default;

Host::~Host()
{
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    wake_.notify_all();
}

void Host::start()
{
    if (started_)
        return;
    started_ = true;
    pthread_t thread;
    // Never name threads: pthread_setname_np hangs on this platform (ps5-homebrew-ui).
    if (pthread_create(&thread, nullptr, &Host::thread_main, this) != 0)
        hui::sys::log("[PSAPP] worker thread creation failed");
    else
        pthread_detach(thread);
}

void *Host::thread_main(void *self)
{
    static_cast<Host *>(self)->run();
    return nullptr;
}

Snapshot Host::snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void Host::push(Job job)
{
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(job));
    wake_.notify_all();
}

void Host::submit_pin(const std::string &id, const std::string &pin, const std::string &name)
{
    push({Kind::pin, id, pin, name, {}, false});
}
void Host::start_host()
{
    push({Kind::start, {}, {}, {}, {}, false});
}
void Host::stop_host()
{
    push({Kind::stop, {}, {}, {}, {}, false});
}
void Host::restart_host()
{
    push({Kind::restart, {}, {}, {}, {}, false});
}
void Host::save_config(const Values &values, bool restart)
{
    push({Kind::save, {}, {}, {}, values, restart});
}
void Host::set_autoload(bool on)
{
    push({on ? Kind::autoload_on : Kind::autoload_off, {}, {}, {}, {}, false});
}
void Host::refresh()
{
    push({Kind::refresh, {}, {}, {}, {}, false});
}

void Host::set_busy(const std::string &text)
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.busy = text;
}

void Host::finish(const std::string &message, bool ok)
{
    hui::sys::log("[PSAPP] %s: %s", ok ? "done" : "failed", message.c_str());
    std::lock_guard<std::mutex> lock(mutex_);
    state_.busy.clear();
    state_.message = message;
    state_.message_ok = ok;
    ++state_.message_serial;
}

void Host::run()
{
    hui::sys::log("[PSAPP] worker started");
    prepare_files();
    read_files();
    poll();
    for (;;)
    {
        Job job;
        bool have = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock, std::chrono::seconds(2), [this] { return stop_ || !jobs_.empty(); });
            if (stop_)
                return;
            if (!jobs_.empty())
            {
                job = std::move(jobs_.front());
                jobs_.pop_front();
                have = true;
            }
        }
        if (have)
        {
            switch (job.kind)
            {
            case Kind::pin:
                do_pin(job);
                break;
            case Kind::start:
                do_start();
                break;
            case Kind::stop:
                do_stop();
                break;
            case Kind::restart:
                do_restart();
                break;
            case Kind::save:
                do_save(job);
                break;
            case Kind::autoload_on:
                do_autoload(true);
                break;
            case Kind::autoload_off:
                do_autoload(false);
                break;
            case Kind::refresh:
                read_files();
                finish("Read sunshine.conf and the autoload list again", true);
                break;
            }
        }
        poll();
    }
}

// The data folder, and the host ELF the app carries copied to it when it is missing or another
// build. Start, restart and autoload all use that one copy.
void Host::prepare_files()
{
    if (!io::make_dirs(kDataDir))
        hui::sys::log("[PSAPP] cannot create %s", kDataDir);
    const bool bundled = io::exists(kBundledElf);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.bundled_elf = bundled;
    }
    if (bundled && !io::same_content(kBundledElf, kHostElf))
    {
        const std::string error = io::copy_file(kBundledElf, kHostElf);
        if (error.empty())
            hui::sys::log("[PSAPP] installed the bundled host to %s", kHostElf);
        else
            finish("Could not install the host: " + error, false);
    }
    // An autoload slot made by an earlier version of the app follows the installed host.
    std::string list;
    if (io::read_text(autoload::kListPath, &list) && autoload::contains(list, autoload::kSlotKey) &&
        io::exists(kHostElf) && !io::same_content(kHostElf, autoload::kSlotElf))
    {
        const std::string error = sync_slot();
        hui::sys::log("[PSAPP] autoload slot updated: %s", error.empty() ? "ok" : error.c_str());
    }
}

void Host::read_files()
{
    Values values;
    std::string error;
    std::string text;
    if (io::read_text(kConfPath, &text))
    {
        const Config config = Config::parse(text);
        for (const OptionSpec &spec : option_specs())
            values[spec.key] = config.get(spec.key);
    }
    else
    {
        for (const OptionSpec &spec : option_specs())
            values[spec.key] = std::nullopt;
        if (io::exists(kConfPath))
            error = "sunshine.conf could not be read";
    }
    std::string list, manager;
    const bool have_list = io::read_text(autoload::kListPath, &list);
    const bool have_manager = io::read_text(autoload::kConfigPath, &manager);
    std::lock_guard<std::mutex> lock(mutex_);
    state_.files_read = true;
    state_.config = values;
    state_.config_error = error;
    state_.autoload = have_list && autoload::contains(list, autoload::kSlotKey);
    state_.manager_autoload = have_manager && autoload::manager_autoload_enabled(manager);
    state_.manager_present = io::exists("/data/pldmgr");
    state_.host_elf_size = io::file_size(kHostElf);
}

void Host::poll()
{
    const io::Exchange reply = io::exchange(kControlPort, "STATUS\nPAIRINGS\n", is_end, 3000);
    bool running = false, streaming = false;
    std::string app, error;
    std::vector<Pairing> pairings;
    if (reply.connect_refused)
    {
        // Nobody listens: the host is not running.
    }
    else if (!reply.error.empty() && reply.lines.empty())
    {
        running = true; // something holds the port but did not answer (a PIN in progress)
        error = reply.error;
    }
    else
    {
        running = true;
        for (std::size_t i = 0; i < reply.lines.size(); ++i)
        {
            const std::string &line = reply.lines[i];
            if (i == 0)
            {
                if (line.rfind("OK ", 0) != 0)
                {
                    error = line;
                    break;
                }
                streaming = status_field(line, "running") == "1";
                app = status_field(line, "app");
                continue;
            }
            if (line == "END" || line.rfind("OK ", 0) != 0)
                continue;
            // "OK <id> <name> <address>": the name may hold spaces, the id and address do not.
            const std::string rest = line.substr(3);
            const std::size_t first = rest.find(' ');
            const std::size_t last = rest.rfind(' ');
            Pairing p;
            p.id = rest.substr(0, first);
            if (first != std::string::npos && last > first)
            {
                p.name = rest.substr(first + 1, last - first - 1);
                p.address = rest.substr(last + 1);
            }
            else if (first != std::string::npos)
            {
                p.name = rest.substr(first + 1);
            }
            pairings.push_back(p);
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_.polled = true;
    state_.running = running;
    state_.streaming = streaming;
    state_.app = app;
    state_.pairings = pairings;
    state_.poll_error = error;
}

bool Host::wait_control(bool open, int timeout_ms)
{
    for (int waited = 0; waited <= timeout_ms; waited += 250)
    {
        if (io::port_open(kControlPort, 250) == open)
            return true;
        sleep_ms(250);
    }
    return false;
}

std::string Host::launch()
{
    if (!io::exists(kHostElf))
        return std::string("no host at ") + kHostElf;
    hui::sys::log("[PSAPP] sending %s to the ELF loader", kHostElf);
    const std::string error = io::send_file(kLoaderPort, kHostElf);
    if (!error.empty())
        return error;
    if (!wait_control(true, 30000))
        return "the host did not come up within 30 s (see sunshine.log)";
    return {};
}

std::string Host::quit_and_wait()
{
    const io::Exchange reply = io::exchange(kControlPort, "QUIT\n", any_line, 3000);
    if (reply.connect_refused)
        return {}; // not running
    if (!reply.error.empty())
        return "QUIT: " + reply.error;
    if (!wait_control(false, 20000))
        return "the host did not stop within 20 s";
    // The control socket closes first; give the rest of the host a moment to let go of its ports
    // and the capture service.
    sleep_ms(2000);
    return {};
}

std::string Host::sync_slot()
{
    if (!io::make_dirs(autoload::kSlotDir))
        return std::string("cannot create ") + autoload::kSlotDir;
    std::string error;
    if (!io::same_content(kHostElf, autoload::kSlotElf))
        error = io::copy_file(kHostElf, autoload::kSlotElf);
    if (error.empty())
        error = io::write_text(autoload::kSlotMeta, autoload::slot_meta("prospero_sunshine.elf", iso_time()));
    return error;
}

void Host::do_pin(const Job &job)
{
    set_busy("Pairing: waiting for Moonlight...");
    std::string name = job.c;
    for (char &c : name)
    {
        if (c == ' ' || c == '\t')
            c = '_';
    }
    const std::string request = "PIN " + job.a + " " + job.b + (name.empty() ? "" : " " + name) + "\n";
    // The host answers once Moonlight finished its side of the handshake.
    const io::Exchange reply = io::exchange(kControlPort, request, any_line, 60000);
    if (!reply.error.empty())
    {
        finish("Pairing failed: " + reply.error, false);
        return;
    }
    const std::string answer = reply.lines.empty() ? std::string() : reply.lines.front();
    if (answer == "OK paired")
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++state_.paired_serial;
        }
        finish("Paired with " + (job.c.empty() ? job.a : job.c), true);
    }
    else
    {
        finish("Pairing failed (" + answer + "). Check the PIN Moonlight shows.", false);
    }
}

void Host::do_start()
{
    if (io::port_open(kControlPort))
    {
        finish("The host is running already", true);
        return;
    }
    set_busy("Starting the host...");
    const std::string error = launch();
    finish(error.empty() ? "The host is running" : "Start failed: " + error, error.empty());
}

void Host::do_stop()
{
    set_busy("Stopping the host...");
    const std::string error = quit_and_wait();
    finish(error.empty() ? "The host has stopped" : "Stop failed: " + error, error.empty());
}

void Host::do_restart()
{
    set_busy("Stopping the host...");
    std::string error = quit_and_wait();
    if (error.empty())
    {
        set_busy("Starting the host...");
        error = launch();
    }
    finish(error.empty() ? "The host restarted with the saved settings" : "Restart failed: " + error,
           error.empty());
}

void Host::do_save(const Job &job)
{
    set_busy("Saving...");
    std::string text;
    io::read_text(kConfPath, &text);
    Config config = Config::parse(text);
    for (const auto &[key, value] : job.values)
    {
        const OptionSpec *spec = find_option(key);
        if (spec == nullptr)
            continue;
        if (value)
        {
            if (const auto problem = validate(*spec, *value))
            {
                finish(std::string(spec->label) + " " + *problem, false);
                return;
            }
        }
        config.set(key, value);
    }
    if (!io::make_dirs(kDataDir))
    {
        finish(std::string("Cannot create ") + kDataDir, false);
        return;
    }
    const std::string error = io::write_text(kConfPath, config.text());
    read_files();
    if (!error.empty())
    {
        finish("Saving failed: " + error, false);
        return;
    }
    if (!job.flag)
    {
        finish(io::port_open(kControlPort) ? "Saved. Restart the host to use the new settings." : "Saved",
               true);
        return;
    }
    do_restart();
}

void Host::do_autoload(bool on)
{
    set_busy(on ? "Turning autoload on..." : "Turning autoload off...");
    std::string list;
    const bool have = io::read_text(autoload::kListPath, &list);
    if (!have && io::exists(autoload::kListPath))
    {
        finish("Cannot read the Payload Manager's autoload.txt", false);
        return;
    }
    std::string error;
    if (on)
    {
        if (!io::exists("/data/pldmgr"))
            error = "the Payload Manager is not installed (/data/pldmgr is missing)";
        else if (!io::exists(kHostElf))
            error = std::string("no host at ") + kHostElf;
        else
            error = sync_slot();
        if (error.empty())
            error = io::write_text(autoload::kListPath, autoload::with_entry(list, autoload::kSlotKey));
    }
    else if (have)
    {
        error = io::write_text(autoload::kListPath, autoload::without_entry(list, autoload::kSlotKey));
    }
    read_files();
    if (!error.empty())
        finish("Autoload: " + error, false);
    else
        finish(on ? "The Payload Manager starts the host at boot" : "The host no longer starts at boot", true);
}

} // namespace psapp
