// SPDX-License-Identifier: GPL-3.0-only
// P9.3: which resolutions and frame rates does Avcap2 deliver? Each mode opens a fresh channel, keeps
// the stream for three seconds in /data/prosperosunshine/spike_<resolution>_<timing>.h264 and reports
// frames and bytes to spike_modes.txt next to it. ffprobe on the PC reads the real dimensions.
#include "platform/ps5/capture.h"
#include <chrono>
#include <cstdio>
#include <unistd.h>
#include <vector>

int main() {
    using clock = std::chrono::steady_clock;
    const struct { uint32_t resolution, timing, change_to; } modes[] = {{3, 1, 0}, {4, 1, 3}, {4, 0, 3}, {4, 1, 2}};
    std::vector<uint8_t> buffer(4 * 1024 * 1024);
    FILE *report = fopen("/data/prosperosunshine/spike_modes.txt", "w");
    if (!report) return 1;
    for (const auto &mode : modes) {
        ps_capture_set_video_mode(mode.resolution, mode.timing);
        ps_capture *s = nullptr;
        const int open_rc = ps_capture_open(&s);
        if (open_rc) {
            fprintf(report, "res=%u timing=%u open_rc=%d raw=%#x\n", mode.resolution, mode.timing, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
            fflush(report);
            usleep(1000000);
            continue;
        }
        char path[96];
        snprintf(path, sizeof path, "/data/prosperosunshine/spike_%u_%u_%u.h264", mode.resolution, mode.timing, mode.change_to);
        FILE *out = fopen(path, "wb");
        const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
        if (out && header_size > 0) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
        const int change_rc = mode.change_to ? ps_capture_change_scan_in(s, mode.change_to) : 0;
        int frames = 0, error = 0;
        size_t bytes = 0;
        uint8_t desc[0x70];
        const auto start = clock::now();
        while (clock::now() - start < std::chrono::seconds(3)) {
            const int rc = ps_capture_read(s, buffer.data(), buffer.size(), desc);
            if (rc < 0) { error = rc; break; }
            if (rc == 0) { usleep(1000); continue; }
            ++frames;
            bytes += static_cast<size_t>(rc);
            if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
        }
        if (out) fclose(out);
        const int close_rc = ps_capture_close(s);
        fprintf(report, "res=%u timing=%u change_to=%u change_rc=%#x frames=%d bytes=%zu headers=%d error=%#x close_rc=%d\n",
                mode.resolution, mode.timing, mode.change_to, static_cast<unsigned>(change_rc), frames, bytes, header_size,
                static_cast<unsigned>(error), close_rc);
        fflush(report);
        usleep(1000000);
    }
    fclose(report);
    return 0;
}
