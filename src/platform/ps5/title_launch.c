// SPDX-License-Identifier: GPL-3.0-only
// The calls follow launch_app() in shsrv and sys_launch_title() in websrv by John Törnblom (GPL-3.0-or-later,
// https://github.com/ps5-payload-dev). The two libraries have to be linked. Loaded with sceKernelLoadStartModule, a process
// stopped for good, past SIGKILL, until the console restarted: a probe doing that didn't get as far as its first log line, and
// the host stopped right after the title came up. A linked probe got the title's app id back and kept running.
#include "title_launch.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t size, user_id, options;
    uint64_t crash_report;
    uint32_t flags;
} launch_context;

int sceUserServiceInitialize(void *);
int sceUserServiceTerminate(void);
int sceUserServiceGetForegroundUser(uint32_t *);
int sceSystemServiceLaunchApp(const char *, char **, launch_context *);
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t, char *);

int ps_title_running(char title_id[16])
{
    memset(title_id, 0, 16);
    const int app = sceLncUtilGetAppIdOfRunningBigApp();
    return app > 0 && sceLncUtilGetAppTitleId((uint32_t)app, title_id) == 0;
}

int ps_title_launch(const char *title_id)
{
    // Something else in this process may have initialized it already.
    const int already_initialized = (int)0x80960003;
    const int init = sceUserServiceInitialize(NULL);
    if (init && init != already_initialized) return init;
    launch_context context = {.size = sizeof context};
    int rc = sceUserServiceGetForegroundUser(&context.user_id);
    char *args[] = {NULL};
    if (rc == 0) rc = sceSystemServiceLaunchApp(title_id, args, &context);
    if (init == 0) sceUserServiceTerminate();
    return rc;
}
