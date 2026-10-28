// SPDX-License-Identifier: GPL-3.0-only
// PC tests for the app's pure parts: sunshine.conf edits and the Payload Manager's autoload list.
#include "app/psapp/autoload.hpp"
#include "app/psapp/conf.hpp"

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

int main()
{
    conf_tests();
    autoload_tests();
    std::printf("%s\n", failures == 0 ? "psapp tests passed" : "psapp tests FAILED");
    return failures == 0 ? 0 : 1;
}
