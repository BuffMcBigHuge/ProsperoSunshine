// SPDX-License-Identifier: GPL-3.0-only
#include "diag.h"
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ps5/klog.h>

// A capture that keeps failing logs a few lines four times a second (the reopen loop in video_ps5.cpp).
#define MAX_BYTES (128 * 1024)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int fd = -1;
static size_t written;
static char first_line[256];

static void append(const char *line, size_t length)
{
    if (fd < 0) return;
    if (written + length > MAX_BYTES) {
        // Starts over with the header: the latest lines matter most.
        if (ftruncate(fd, 0) != 0) return;
        written = 0;
        append(first_line, strlen(first_line));
    }
    if (write(fd, line, length) == (ssize_t)length) written += length;
    fsync(fd);
}

void ps_diag_open(const char *path, const char *header)
{
    pthread_mutex_lock(&lock);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
    written = 0;
    snprintf(first_line, sizeof first_line, "%s\n", header);
    append(first_line, strlen(first_line));
    pthread_mutex_unlock(&lock);
    klog_printf("%s\n", header);
}

void ps_diag_log(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof line, format, args);
    va_end(args);
    if (length <= 0) return;
    klog_printf("%s", line);
    pthread_mutex_lock(&lock);
    append(line, (size_t)length < sizeof line ? (size_t)length : sizeof line - 1);
    pthread_mutex_unlock(&lock);
}
