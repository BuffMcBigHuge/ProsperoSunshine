// SPDX-License-Identifier: GPL-3.0-only
// Derived from ps5-mcp/payload/RemoteEye/Program.cs. libScePad's virtual-pad calls each reduce to one
// ioctl on /dev/hid; scePadInit itself hangs inside a payload, so the requests are built directly:
//   add     0xC018482A  { u32 op = 3; u64 desc*; u64 int* handleOut }
//           desc (0xA8 bytes): u8 class @0, u32 userId @0x1C, u8 subtype @0x20
//   insert  0x8018482C  { u32 handle; u64 state*; u8 0 }
//           state (0xA8 bytes): u32 buttons @0xC, u8 lx ly rx ry @0x10, u8 l2 r2 @0x14, u8 1 @0x17
//   delete  0x80104850  { u8 op = 5; u32 handle @8 }
//   output  0x80684830  { u32 handle; u64 pointers to 12 out-fields } (libScePad 0xdd70, Remote Play's only reader)
#include "pad.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BLOCK 0xA8
#define IOCTL_ADD 0xC018482AUL
#define IOCTL_INSERT 0x8018482CUL
#define IOCTL_DELETE 0x80104850UL
#define IOCTL_OUTPUT 0x80684830UL
// Public device type 1 (standard controller) maps to class 3, subtype 1.
#define TYPE_CLASS 3
#define TYPE_SUBTYPE 1

struct ps_pad {
    int fd;
    int handle;
};

uint32_t ps_pad_default_user(void)
{
    DIR *dir = opendir("/user/home");
    if (!dir) return 0;
    uint32_t user = 0;
    for (struct dirent *e; !user && (e = readdir(dir));) {
        if (e->d_name[0] == '.') continue;
        char *end = NULL;
        unsigned long value = strtoul(e->d_name, &end, 16);
        if (end && !*end && value) user = (uint32_t)value;
    }
    closedir(dir);
    return user;
}

int ps_pad_open(uint32_t user_id, ps_pad **out)
{
    if (!out) return -EINVAL;
    *out = NULL;
    // RemoteEye opens read-only; the ioctls do not need write access.
    int fd = open("/dev/hid", O_RDONLY);
    if (fd < 0) return -errno;
    uint8_t desc[BLOCK] = {0};
    desc[0] = TYPE_CLASS;
    memcpy(desc + 0x1C, &user_id, 4);
    desc[0x20] = TYPE_SUBTYPE;
    int handle = -1;
    struct { uint32_t op; uint32_t pad; uint64_t desc; uint64_t handle; } request = {
        3, 0, (uint64_t)(uintptr_t)desc, (uint64_t)(uintptr_t)&handle
    };
    if (ioctl(fd, IOCTL_ADD, &request) != 0 || handle < 0) {
        int error = errno ? -errno : -EIO;
        close(fd);
        return error;
    }
    ps_pad *pad = malloc(sizeof *pad);
    if (!pad) {
        struct { uint8_t op; uint8_t pad[7]; uint32_t handle; uint32_t pad2; } del = {5, {0}, (uint32_t)handle, 0};
        ioctl(fd, IOCTL_DELETE, &del);
        close(fd);
        return -ENOMEM;
    }
    pad->fd = fd;
    pad->handle = handle;
    *out = pad;
    return 0;
}

int ps_pad_send(ps_pad *pad, const ps_pad_state *state)
{
    if (!pad || !state) return -EINVAL;
    uint8_t block[BLOCK] = {0};
    memcpy(block + 0xC, &state->buttons, 4);
    const uint8_t analog[6] = {state->lx, state->ly, state->rx, state->ry, state->l2, state->r2};
    memcpy(block + 0x10, analog, sizeof analog);
    block[0x17] = 1;
    struct { uint32_t handle; uint32_t pad; uint64_t state; uint8_t zero; uint8_t pad2[7]; } request = {
        (uint32_t)pad->handle, 0, (uint64_t)(uintptr_t)block, 0, {0}
    };
    return ioctl(pad->fd, IOCTL_INSERT, &request) == 0 ? 0 : (errno ? -errno : -EIO);
}

int ps_pad_read_output(ps_pad *pad, ps_pad_output *output)
{
    if (!pad || !output) return -EINVAL;
    // The kernel writes each field through its own pointer, so the fields need not be contiguous.
    uint8_t flags[4] = {0}, motors[4] = {0}, color[4] = {0}, triggers[24] = {0};
    uint32_t flag_a = 0, flag_b = 0, values[6] = {0};
    struct { uint32_t handle; uint32_t pad; uint64_t fields[12]; } request = {
        (uint32_t)pad->handle, 0,
        {(uint64_t)(uintptr_t)&flag_a, (uint64_t)(uintptr_t)flags, (uint64_t)(uintptr_t)motors,
         (uint64_t)(uintptr_t)color, (uint64_t)(uintptr_t)triggers, (uint64_t)(uintptr_t)&values[0],
         (uint64_t)(uintptr_t)&values[1], (uint64_t)(uintptr_t)&values[2], (uint64_t)(uintptr_t)&values[3],
         (uint64_t)(uintptr_t)&values[4], (uint64_t)(uintptr_t)&values[5], (uint64_t)(uintptr_t)&flag_b}
    };
    if (ioctl(pad->fd, IOCTL_OUTPUT, &request) != 0) return errno ? -errno : -EIO;
    output->flag_a = flag_a;
    memcpy(output->flags, flags, sizeof output->flags);
    memcpy(output->motors, motors, sizeof output->motors);
    memcpy(output->color, color, sizeof output->color);
    memcpy(output->triggers, triggers, sizeof output->triggers);
    memcpy(output->values, values, sizeof output->values);
    output->flag_b = flag_b;
    return 0;
}

int ps_pad_neutral(ps_pad *pad)
{
    const ps_pad_state neutral = {0, 128, 128, 128, 128, 0, 0};
    return ps_pad_send(pad, &neutral);
}

int ps_pad_close(ps_pad *pad)
{
    if (!pad) return 0;
    int rc = ps_pad_neutral(pad);
    struct { uint8_t op; uint8_t pad[7]; uint32_t handle; uint32_t pad2; } del = {5, {0}, (uint32_t)pad->handle, 0};
    if (ioctl(pad->fd, IOCTL_DELETE, &del) != 0 && !rc) rc = errno ? -errno : -EIO;
    close(pad->fd);
    free(pad);
    return rc;
}
