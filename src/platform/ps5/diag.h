// SPDX-License-Identifier: GPL-3.0-only
// The host's breadcrumbs for the app's diagnostic report: each line goes to the kernel log and, once
// ps_diag_open ran, to a small file. Every line is on disk before the next step, so the file still
// tells how far the host got when the step after it brings the process down.
#ifndef PROSPERO_DIAG_H
#define PROSPERO_DIAG_H
#ifdef __cplusplus
extern "C" {
#endif

// Starts the file at `path` with `header` as its first line. Without this call (the probes) lines go
// to the kernel log only.
void ps_diag_open(const char *path, const char *header);
void ps_diag_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
#endif
