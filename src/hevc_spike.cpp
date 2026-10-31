// SPDX-License-Identifier: GPL-3.0-only
// P9.7: does Avcap2 deliver HEVC, in which modes and up to which bitrate? Each step opens a fresh channel with
// its bitrate in the config, optionally retargets it at runtime and reads 180 frames. Writes spike_hevc.txt and
// the streams as spike_hevc_<n>.h265 to /data/prosperosunshine/; ffprobe on the PC reads them.
#include "platform/ps5/capture.h"
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <vector>

// resolution 4 = 1080p, 5 = 1440p, 6 = 2160p; timing 1 = 60 fps. change_* retargets at frames 60 and 120 (0 = no).
struct step {
    uint32_t resolution, mbit, change_a, change_b;
};

int main() {
    const step steps[] = {
        {4, 6, 0, 0}, {4, 15, 0, 0}, {4, 30, 0, 0}, {4, 50, 0, 0}, {4, 80, 0, 0}, {4, 100, 0, 0},
        {4, 30, 10, 30}, {4, 20, 40, 60},
        {5, 30, 0, 0}, {5, 50, 0, 0},
        {6, 40, 0, 0}, {6, 80, 0, 0},
    };
    std::vector<uint8_t> buffer(8 * 1024 * 1024);
    FILE *report = fopen("/data/prosperosunshine/spike_hevc.txt", "w");
    if (!report) return 1;
    ps_capture_set_codec(1);
    int index = 0;
    for (const step &current : steps) {
        ps_capture *s = nullptr;
        ps_capture_set_video_mode(current.resolution, 1);
        ps_capture_set_hevc_bitrate(current.mbit * 1000000, current.mbit * 1100000);
        const int open_rc = ps_capture_open(&s);
        if (open_rc) {
            fprintf(report, "res=%u mbit=%u open_rc=%d raw=%#x\n", current.resolution, current.mbit, open_rc,
                    static_cast<unsigned>(ps_capture_last_open_rc()));
            fflush(report);
            sleep(1);
            continue;
        }
        char path[96];
        snprintf(path, sizeof path, "/data/prosperosunshine/spike_hevc_%d.h265", index++);
        FILE *out = fopen(path, "wb");
        const int header_size = ps_capture_headers(s, buffer.data(), buffer.size());
        if (header_size > 0 && out) fwrite(buffer.data(), 1, static_cast<size_t>(header_size), out);
        int frames = 0, error = 0, largest = 0;
        size_t bytes = 0;
        uint8_t desc[0x88];
        for (int polls = 0; frames < 180 && polls < 30000; ++polls) {
            const int rc = ps_capture_read2(s, buffer.data(), buffer.size(), desc);
            if (rc < 0) {
                error = rc;
                break;
            }
            if (rc == 0) {
                usleep(1000);
                continue;
            }
            bytes += static_cast<size_t>(rc);
            if (rc > largest) largest = rc;
            if (out) fwrite(buffer.data(), 1, static_cast<size_t>(rc), out);
            ++frames;
            const uint32_t change = frames == 60 ? current.change_a : frames == 120 ? current.change_b : 0;
            if (change) ps_capture_set_bitrate(s, change * 1000000, change * 1100000);
        }
        if (out) fclose(out);
        const int close_rc = ps_capture_close(s);
        fprintf(report, "res=%u mbit=%u change=%u,%u file=%d frames=%d bytes=%zu largest=%d headers=%d error=%#x close_rc=%d\n",
                current.resolution, current.mbit, current.change_a, current.change_b, index - 1, frames, bytes, largest, header_size,
                static_cast<unsigned>(error), close_rc);
        fflush(report);
        sleep(1);
    }
    fclose(report);
    return 0;
}
