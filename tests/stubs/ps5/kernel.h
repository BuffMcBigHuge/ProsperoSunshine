// SPDX-License-Identifier: GPL-3.0-only
// Stands in for the Payload SDK's header, so that capture.c compiles on the PC (tests/fake_avcap.h defines these).
#pragma once
#include <stdint.h>
#include <sys/types.h>

uint32_t kernel_get_fw_version(void);
int kernel_dynlib_handle(pid_t pid, const char *basename, uint32_t *handle);
intptr_t kernel_dynlib_dlsym(pid_t pid, uint32_t handle, const char *symbol);
intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t handle, const char *nid);
intptr_t kernel_dynlib_mapbase_addr(pid_t pid, uint32_t handle);
