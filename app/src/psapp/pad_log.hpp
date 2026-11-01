// SPDX-License-Identifier: GPL-3.0-only
// Research (docs/re/pad-input.md): while the empty file /data/prosperosunshine/pad_log exists at the app's start, writes what
// the system reports for the controller, ten times a second, to pad_log.txt next to it. Only an app can read this: scePadInit
// hangs in a payload. tools/moonlight/pad_log.py decodes the file.
#pragma once

#include <cstdint>
#include <cstdio>

namespace psapp
{

class PadLog
{
  public:
    PadLog();
    ~PadLog();
    PadLog(const PadLog &) = delete;
    PadLog &operator=(const PadLog &) = delete;

    void tick(std::int64_t now_us);

  private:
    std::FILE *out_ = nullptr;
    int handle_ = -1;
    std::int64_t next_us_ = 0;
};

} // namespace psapp
