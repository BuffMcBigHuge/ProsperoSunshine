// SPDX-License-Identifier: GPL-3.0-only
#ifndef PROSPERO_TITLE_LAUNCH_H
#define PROSPERO_TITLE_LAUNCH_H
#ifdef __cplusplus
extern "C" {
#endif

// The title in front into title_id: 1, 0 on the home screen, negative if the system calls are missing.
int ps_title_running(char title_id[16]);
// Starts a title for the user in front. The new app's ID, or a negative error code from the system.
int ps_title_launch(const char *title_id);

#ifdef __cplusplus
}
#endif
#endif
