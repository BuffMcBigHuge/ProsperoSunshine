// SPDX-License-Identifier: GPL-3.0-only
// PC tests for the app's pure parts: sunshine.conf edits and the Payload Manager's autoload list.
#include "app/psapp/autoload.hpp"
#include "app/psapp/conf.hpp"
#include "app/psapp/diag.hpp"

#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

using namespace psapp;

static void conf_tests()
{
    // An empty file: every key has its default.
    Config empty = Config::parse("");
    CHECK(!empty.get("port"));
    empty.set("port", "48000");
    CHECK(empty.text() == "port = 48000\n");

    // Comments, unknown keys and spacing stay; a known key is replaced in place.
    const std::string text = "# my settings\r\nsunshine_name=Living room # tv\nfoo = bar\n\nport = 47989\n";
    Config c = Config::parse(text);
    CHECK(c.get("sunshine_name") == std::optional<std::string>("Living room"));
    CHECK(c.get("foo") == std::optional<std::string>("bar"));
    c.set("sunshine_name", "PS5");
    c.set("min_log_level", "debug");
    c.set("port", std::nullopt);
    CHECK(c.text() == "# my settings\nsunshine_name = PS5\nfoo = bar\n\nmin_log_level = debug\n");

    // Sunshine keeps a key's first line; set() keeps that place and drops the later duplicates.
    Config d = Config::parse("ping_timeout = 1000\nx = 1\nping_timeout = 3000\n");
    CHECK(d.get("ping_timeout") == std::optional<std::string>("1000"));
    d.set("ping_timeout", "5000");
    CHECK(d.text() == "ping_timeout = 5000\nx = 1\n");

    // A line without a value is no setting.
    CHECK(!Config::parse("port =\n").get("port"));

    const OptionSpec *port = find_option("port");
    CHECK(port != nullptr);
    CHECK(!validate(*port, "47989"));
    CHECK(validate(*port, "1024"));
    CHECK(validate(*port, "65515"));
    CHECK(validate(*port, "12a"));
    const OptionSpec *name = find_option("sunshine_name");
    CHECK(!validate(*name, "My PS5"));
    CHECK(validate(*name, "a#b"));
    CHECK(validate(*name, " lead"));
    CHECK(validate(*name, ""));
    const OptionSpec *level = find_option("min_log_level");
    CHECK(!validate(*level, "warning"));
    CHECK(validate(*level, "loud"));
    CHECK(!validate(*find_option("lan_encryption_mode"), "2"));
    CHECK(validate(*find_option("wan_encryption_mode"), "3"));
    CHECK(!validate(*find_option("ping_timeout"), "2000"));
    CHECK(validate(*find_option("ping_timeout"), "100"));
}

static void autoload_tests()
{
    // The list on the test console (2026-10-29), without a trailing newline after the last entry.
    const std::string list = "repo/kstuff-lite@e8cce5d1\n!1000\nrepo/elfldr@e8cce5d1\n!1000\nrepo/nanodns@e8cce5d1";
    CHECK(!autoload::contains(list, autoload::kSlotKey));
    const std::string on = autoload::with_entry(list, autoload::kSlotKey);
    CHECK(on == list + "\nlocal/prospero-sunshine\n");
    CHECK(autoload::contains(on, autoload::kSlotKey));
    CHECK(autoload::with_entry(on, autoload::kSlotKey) == on);
    const std::string off = autoload::without_entry(on, autoload::kSlotKey);
    CHECK(off == list + "\n");
    CHECK(!autoload::contains(off, autoload::kSlotKey));
    CHECK(autoload::with_entry("", autoload::kSlotKey) == "local/prospero-sunshine\n");
    // Every line that names the slot goes, including one with stray spaces.
    CHECK(autoload::without_entry(" local/prospero-sunshine \n!500\nlocal/prospero-sunshine\n", autoload::kSlotKey) ==
          "!500\n");
    // Another slot whose name starts the same is left alone.
    CHECK(autoload::contains("local/prospero-sunshine-2\n", autoload::kSlotKey) == false);

    CHECK(autoload::manager_autoload_enabled("AUTOLOAD_ENABLED=1\nAUTOLOAD_DELAY=5\n"));
    CHECK(!autoload::manager_autoload_enabled("AUTOLOAD_ENABLED=0\n"));
    CHECK(!autoload::manager_autoload_enabled("AUTOLOAD_DELAY=5\n"));

    const std::string meta = autoload::slot_meta("prospero_sunshine.elf", "2026-10-29T00:00:00+0000");
    CHECK(meta.find("\"kind\": \"local\"") != std::string::npos);
    CHECK(meta.find("\"filename\": \"prospero_sunshine.elf\"") != std::string::npos);
}

static void diag_tests()
{
    CHECK(diag::firmware_text(0x13000040) == "13.00");
    CHECK(diag::firmware_text(0x10400000) == "10.40");
    CHECK(diag::firmware_verified(0x13000040));
    CHECK(!diag::firmware_verified(0x13600000));

    // A good run.
    const std::string good = "ProsperoSunshine: host version=0.1.3 firmware=0x13000040 known=1\n"
                             "ProsperoSunshine: host ready control_port=47995\n"
                             "ProsperoSunshine: capture open start codec=1\n"
                             "ProsperoSunshine: capture OpenVideo rc=0\n"
                             "ProsperoSunshine: capture open ok\n";
    diag::HostLog log = diag::parse_host_log(good);
    CHECK(log.version == "0.1.3");
    CHECK(log.firmware == 0x13000040);
    CHECK(log.capture == diag::Capture::ok);
    CHECK(log.last_step.empty());
    CHECK(!log.stopped);
    CHECK(diag::notice(log.firmware, log, true).text.empty());
    CHECK(diag::parse_host_log(good + "ProsperoSunshine: host stopped\n").stopped);

    // No file yet: only the firmware can speak.
    CHECK(diag::parse_host_log("").capture == diag::Capture::unknown);
    CHECK(diag::notice(0x13000040, {}, false).text.empty());
    diag::Notice note = diag::notice(0x10400000, {}, false);
    CHECK(note.text.find("Firmware 10.40 is untested") == 0);
    CHECK(!note.bad);

    // A failed step, then a later attempt that works: the latest outcome counts.
    const std::string failed = "ProsperoSunshine: host version=0.1.3 firmware=0x10400000 known=0\n"
                               "ProsperoSunshine: capture open start codec=0\n"
                               "ProsperoSunshine: capture OpenVideo rc=0x80c78000\n"
                               "ProsperoSunshine: capture open failed step=OpenVideo error=-1 rc=0x80c78000\n";
    log = diag::parse_host_log(failed);
    CHECK(log.capture == diag::Capture::failed);
    CHECK(log.failed_step == "OpenVideo");
    CHECK(log.failed_error == -1);
    CHECK(log.failed_rc == "0x80c78000");
    note = diag::notice(log.firmware, log, true);
    CHECK(note.bad);
    CHECK(note.text == "Capture failed at OpenVideo on Firmware 10.40. See Diagnostics.");
    log = diag::parse_host_log(failed + "ProsperoSunshine: capture open start codec=0\nProsperoSunshine: capture open ok\n");
    CHECK(log.capture == diag::Capture::ok);
    CHECK(!diag::notice(log.firmware, log, true).bad);

    // Causes the user can act on, whatever the firmware.
    log = diag::parse_host_log("capture open start codec=0\ncapture open failed step=video error=-4 rc=0\n");
    CHECK(diag::notice(0x13000040, log, true).text.find("PS VR") != std::string::npos);
    log = diag::parse_host_log("capture open start codec=0\ncapture open failed step=resource list error=-2 rc=0\n");
    CHECK(log.failed_step == "resource list");
    CHECK(diag::notice(0x13000040, log, true).text.find("Something else") == 0);

    // The refusal of an unknown firmware.
    log = diag::parse_host_log("ProsperoSunshine: ra refusing firmware 0x9000000: ResourceArbitrator calls not resolved\n");
    CHECK(log.refused);
    note = diag::notice(0x09000000, log, true);
    CHECK(note.bad);
    CHECK(note.text.find("Firmware 9.00 is not supported yet") == 0);

    // The process went down inside an attempt: no outcome line, and no host left.
    log = diag::parse_host_log("capture open start codec=0\ncapture register rc=0\ncapture RUNNING rc=0\n");
    CHECK(log.capture == diag::Capture::opening);
    CHECK(log.last_step == "RUNNING rc=0");
    CHECK(diag::notice(0x13000040, log, true).text.empty()); // still going
    CHECK(diag::notice(0x13000040, log, false).text.find("went down") != std::string::npos);

    // Scrubbing keeps the codes and takes what identifies someone.
    CHECK(diag::scrub("client 192.168.1.20 and 172.16.0.1:47989", {}) == "client <address> and <address>:47989");
    CHECK(diag::scrub("OpenVideo rc=0x80c78000 version 1.2.3", {}) == "OpenVideo rc=0x80c78000 version 1.2.3");
    CHECK(diag::scrub("from Living Room PC", {"Living Room PC"}) == "from <name>");
    CHECK(diag::scrub("name PC stays", {"PC"}) == "name PC stays");
    CHECK(diag::scrub("id 0123456789abcdef0123 end", {}) == "id <id> end");
    CHECK(diag::scrub("uuid 1b4e28ba-2fa1-11d2-883f-0016d3cca427.", {}) == "uuid <id>.");
    CHECK(diag::scrub("firmware=0x13000040 known=1", {}) == "firmware=0x13000040 known=1");

    CHECK(diag::tail("a\nb\nc\n", 2) == "b\nc\n");
    CHECK(diag::tail("a\nb\nc", 5) == "a\nb\nc\n");
    CHECK(diag::tail("", 5).empty());
    CHECK(diag::tail("abcdef\n", 1, 3) == "abc\n");
    CHECK(diag::errno_name(13) == "errno 13 (permission denied)");
    CHECK(diag::errno_name(9999) == "errno 9999");
}

int main()
{
    conf_tests();
    autoload_tests();
    diag_tests();
    std::printf("%s\n", failures == 0 ? "psapp tests passed" : "psapp tests FAILED");
    return failures == 0 ? 0 : 1;
}
