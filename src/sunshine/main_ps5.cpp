// SPDX-License-Identifier: GPL-3.0-only
// Host entry point; replaces Sunshine's main.cpp/entry_handler.cpp. Starts the GameStream HTTP(S)
// and RTSP services with the PS5 platform layer and offers a small line-based control socket for
// development (ROADMAP P2.3): pairing PIN, status and shutdown.
#include "control/control_server.hpp"
#include "platform/ps5/mdns_responder.hpp"
#include "src/config.h"
#include "src/globals.h"
#include "src/httpcommon.h"
#include "src/input.h"
#include "src/logging.h"
#include "src/network.h"
#include "src/nvhttp.h"
#include "src/platform/common.h"
#include "src/process.h"
#include "src/rtsp.h"
#include "src/video.h"
#include <filesystem>
#include <fstream>
#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <rs.h>
#include <thread>
extern "C" {
#include <ps5/klog.h>
}

using namespace std::literals;

namespace {
// Sunshine logs to stdout and a file; on the console the kernel log is what developers watch.
struct klog_backend : boost::log::sinks::basic_formatted_sink_backend<char, boost::log::sinks::synchronized_feeding> {
    void consume(const boost::log::record_view &, const std::string &text) {
        klog_printf("ProsperoSunshine: host %s\n", text.c_str());
    }
};

using klog_sink = boost::log::sinks::synchronous_sink<klog_backend>;

boost::shared_ptr<klog_sink> add_klog_sink() {
    namespace expr = boost::log::expressions;
    auto sink = boost::make_shared<klog_sink>();
    sink->set_formatter(expr::stream << '[' << expr::attr<int>("Severity") << "] " << expr::smessage);
    sink->set_filter(expr::attr<int>("Severity") >= 2);  // info and above until the config is read
    boost::log::core::get()->add_sink(sink);
    return sink;
}
}

int main(int argc, char *argv[]) {
    const auto klog = add_klog_sink();
    std::error_code ec;
    std::filesystem::create_directories(platf::appdata(), ec);
    klog_printf("ProsperoSunshine: host appdata=%s mkdir_ec=%d config=%s\n", platf::appdata().c_str(), ec.value(),
                config::sunshine.config_file.c_str());
    // config::parse copies a template apps.json from Sunshine's assets when none exists; there are
    // no assets on the console, and the app list is fixed in process_ps5.cpp anyway.
    if (const auto apps = platf::appdata() / "apps.json"; !std::filesystem::exists(apps, ec))
        std::ofstream(apps) << R"({"env":{},"apps":[{"name":"PS5 Screen"}]})" << '\n';
    // Held controller input stays active until a dead client is detected. Moonlight pings the
    // control stream every 100 ms, so 2 s (upstream default 10 s) still tolerates 20 lost pings while
    // cutting the stuck-input window after a network drop (P4.3). sunshine.conf can override it.
    config::stream.ping_timeout = 2s;
    mail::man = std::make_shared<safe::mail_raw_t>();
    if (const int rc = config::parse(argc, argv)) {
        klog_printf("ProsperoSunshine: host config::parse returned %d\n", rc);
        return rc < 0 ? 1 : 0;
    }
    // From here on the kernel log follows min_log_level from sunshine.conf like the file log.
    klog->set_filter(boost::log::expressions::attr<int>("Severity") >= config::sunshine.min_log_level);
    auto log_guard = logging::init(config::sunshine.min_log_level, config::sunshine.log_file);
    if (!log_guard) klog_printf("ProsperoSunshine: host logging failed to initialize\n");
    BOOST_LOG(info) << "ProsperoSunshine host starting, data in "sv << platf::appdata().string();

    task_pool.start(1);
    auto shutdown_event = mail::man->event<bool>(mail::shutdown);

    auto platform_guard = platf::init();
    auto proc_guard = proc::init();
    reed_solomon_init();
    auto input_guard = input::init();
    if (video::probe_encoders()) {
        BOOST_LOG(fatal) << "Video capture unavailable"sv;
        return 1;
    }
    if (http::init()) {
        BOOST_LOG(fatal) << "HTTP interface failed to initialize"sv;
        return 1;
    }

    std::jthread http_thread{nvhttp::start};
    std::jthread rtsp_thread{rtsp_stream::start};
    prospero::ControlServer control(shutdown_event);
    std::jthread control_thread([&](std::stop_token stop) { control.run(stop); });
    // Moonlight discovery (P9.2); on any socket problem the responder logs and ends, the host runs on.
    std::jthread mdns_thread([name = config::nvhttp.sunshine_name](std::stop_token stop) {
        prospero::mdns::run_responder(stop, name, net::map_port(nvhttp::PORT_HTTP));
    });
    klog_printf("ProsperoSunshine: host ready control_port=%u\n", prospero::ControlServer::port);

    shutdown_event->view();
    BOOST_LOG(info) << "ProsperoSunshine host shutting down"sv;
    control_thread.request_stop();
    mdns_thread.request_stop();
    proc::proc.terminate();
    mdns_thread.join();
    http_thread.join();
    rtsp_thread.join();
    task_pool.stop();
    task_pool.join();
    klog_printf("ProsperoSunshine: host stopped\n");
    return 0;
}
