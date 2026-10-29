// SPDX-License-Identifier: GPL-3.0-only
// Everything the app does to the host, on one worker thread so the frame loop never waits:
//   - the host's control socket on 127.0.0.1:47995 (src/control/control_server.hpp): STATUS and
//     PAIRINGS every two seconds, PIN for a pending pairing, QUIT to stop it;
//   - starting the host: its ELF sent to the ELF loader on 127.0.0.1:9021;
//   - /data/prosperosunshine/sunshine.conf (conf.hpp) and the Payload Manager's autoload list
//     (autoload.hpp).
// The screen reads a Snapshot each frame and queues requests; results come back as a message.
#pragma once

#include "app/psapp/conf.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace psapp
{

constexpr const char *kDataDir = "/data/prosperosunshine";
constexpr const char *kConfPath = "/data/prosperosunshine/sunshine.conf";
constexpr const char *kHostElf = "/data/prosperosunshine/prospero_sunshine.elf";
constexpr const char *kStatePath = "/data/prosperosunshine/sunshine_state.json";
constexpr const char *kBundledElf = "/app0/payloads/prospero_sunshine.elf";
constexpr std::uint16_t kControlPort = 47995;
constexpr std::uint16_t kLoaderPort = 9021;

struct Pairing
{
    std::string id;
    std::string name;
    std::string address;
};

using Values = std::map<std::string, std::optional<std::string>>; // key -> value, nullopt: default

struct Snapshot
{
    // The host, from the last poll.
    bool polled = false;      // at least one poll finished
    bool running = false;     // the control socket answered
    bool streaming = false;   // STATUS running=1
    std::string app;          // STATUS app=
    std::string client;       // STATUS client= and address= (newer hosts; empty otherwise)
    std::string client_address;
    std::vector<Pairing> pairings;
    std::string poll_error;   // set when the socket is there but did not answer properly
    // Files.
    bool files_read = false;
    Values config;            // the keys of option_specs()
    std::string config_error; // reading failed (a missing file is fine)
    bool autoload = false;           // local/prospero-sunshine is in autoload.txt
    bool manager_autoload = false;   // AUTOLOAD_ENABLED=1 in pldmgr_config.txt
    bool manager_present = false;    // /data/pldmgr exists
    long long host_elf_size = -1;    // /data/prosperosunshine/prospero_sunshine.elf
    std::vector<std::string> paired; // named_devices in sunshine_state.json
    std::string address;             // the console's IPv4 address, for Moonlight
    bool bundled_elf = false;        // the app carries one
    // The request being worked on ("" when idle), and the outcome of the last one.
    std::string busy;
    std::string message;
    bool message_ok = true;
    std::uint64_t message_serial = 0; // grows with every new message
    std::uint64_t paired_serial = 0;  // grows with every successful PIN
};

class Host
{
  public:
    Host();
    ~Host();

    void start(); // starts the worker thread
    Snapshot snapshot();

    // Requests; each is queued and done in order on the worker.
    void submit_pin(const std::string &id, const std::string &pin, const std::string &name);
    void start_host();
    void stop_host();
    void restart_host();
    void save_config(const Values &values, bool restart);
    void set_autoload(bool on);
    void refresh();

  private:
    enum class Kind
    {
        pin,
        start,
        stop,
        restart,
        save,
        autoload_on,
        autoload_off,
        refresh,
    };
    struct Job
    {
        Kind kind;
        std::string a, b, c;
        Values values;
        bool flag = false;
    };

    static void *thread_main(void *self);
    void run();
    void push(Job job);
    void set_busy(const std::string &text);
    void finish(const std::string &message, bool ok);

    void prepare_files();
    void read_files();
    void poll();
    bool wait_control(bool open, int timeout_ms);
    std::string launch();
    std::string quit_and_wait();
    std::string sync_slot();

    void do_pin(const Job &job);
    void do_start();
    void do_stop();
    void do_restart();
    void do_save(const Job &job);
    void do_autoload(bool on);

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    Snapshot state_;
    bool stop_ = false;
    bool started_ = false;
};

} // namespace psapp
