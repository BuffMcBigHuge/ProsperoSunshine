// SPDX-License-Identifier: GPL-3.0-only
#include "app/psapp/pad_log.hpp"

#include "app/psapp/io.hpp"

extern "C"
{
    int sceUserServiceGetInitialUser(int *user);
    int scePadGetHandle(int user, int type, int index);
    int scePadReadState(int handle, void *data);
}

namespace psapp
{

PadLog::PadLog()
{
    if (!io::exists("/data/prosperosunshine/pad_log"))
        return;
    out_ = std::fopen("/data/prosperosunshine/pad_log.txt", "w");
    if (!out_)
        return;
    int user = -1;
    const int rc = sceUserServiceGetInitialUser(&user);
    handle_ = rc == 0 ? scePadGetHandle(user, 0, 0) : rc;
    std::fprintf(out_, "# handle %d\n", handle_);
    std::fflush(out_);
}

PadLog::~PadLog()
{
    if (out_)
        std::fclose(out_);
}

void PadLog::tick(std::int64_t now_us)
{
    if (!out_ || handle_ < 0 || now_us < next_us_)
        return;
    next_us_ = now_us + 100000;
    // Larger than the 0x78 bytes of the layout known here, in case this SDK's is longer.
    std::uint8_t data[0x100] = {};
    const int rc = scePadReadState(handle_, data);
    std::fprintf(out_, "%lld %d ", static_cast<long long>(now_us / 1000), rc);
    for (int i = 0; i < 0x78; i++)
        std::fprintf(out_, "%02x", data[i]);
    std::fputc('\n', out_);
    std::fflush(out_);
}

} // namespace psapp
