// SPDX-License-Identifier: GPL-3.0-only
// libSceResourceArbitrator.sprx: the four calls ProsperoSunshine needs. They are exported, but only
// their NIDs are known, not their names, so kernel_dynlib_dlsym cannot find them; they are looked up
// by NID instead (ra_resolve.c). Portable part: NIDs, the offsets verified per firmware and the rule
// that combines both (tested in tests/ra_choose_test.cpp). Background: docs/firmware-porting.md.
#ifndef PROSPERO_RA_NIDS_H
#define PROSPERO_RA_NIDS_H
#include <stddef.h>
#include <stdint.h>

enum ps_ra_function { PS_RA_REGISTER, PS_RA_UNREGISTER, PS_RA_REQUEST, PS_RA_LIST, PS_RA_COUNT };

// register(int id, int (*callback)(int id, int event, const uint8_t *data), void *user)
// unregister(int id)
// request(int id, const char *state, void *data, int pid)
// list(int32_t active[32])
static const char *const ps_ra_names[PS_RA_COUNT] = {"register", "unregister", "request", "list"};
static const char *const ps_ra_nids[PS_RA_COUNT] = {"kJvjeet+f9I", "LqmYZGB-UEQ", "CYjfezkWcaE", "fBX+DBOwNis"};

// Module-relative offsets verified live, keyed by kernel_get_fw_version() & 0xffff0000.
struct ps_ra_known {
    uint32_t firmware;
    uint32_t offset[PS_RA_COUNT];
};
static const struct ps_ra_known ps_ra_known_firmware[] = {
    {0x13000000u, {0xdb0, 0xee0, 0x10b0, 0x11f0}},
};

static inline const struct ps_ra_known *ps_ra_find_known(uint32_t firmware)
{
    for (size_t i = 0; i < sizeof ps_ra_known_firmware / sizeof ps_ra_known_firmware[0]; i++)
        if (ps_ra_known_firmware[i].firmware == (firmware & 0xffff0000u)) return &ps_ra_known_firmware[i];
    return NULL;
}

enum ps_ra_verdict {
    PS_RA_BY_NID,       // NID resolved (and matches the table on a known firmware)
    PS_RA_BY_TABLE,     // known firmware, NID not found: table offset as fallback
    PS_RA_MISSING,      // unknown firmware and NID not found
    PS_RA_CONFLICT      // known firmware and the NID points elsewhere
};

// Combines the NID result (module-relative offset, 0 = not found) with the table entry (NULL for an
// unknown firmware). *offset receives the offset to use, 0 when the verdict refuses.
static inline enum ps_ra_verdict ps_ra_choose(const struct ps_ra_known *known, int function, uint32_t by_nid,
                                              uint32_t *offset)
{
    uint32_t by_table = known ? known->offset[function] : 0;
    enum ps_ra_verdict verdict;
    if (by_nid && by_table && by_nid != by_table) verdict = PS_RA_CONFLICT;
    else if (by_nid) verdict = PS_RA_BY_NID;
    else if (by_table) verdict = PS_RA_BY_TABLE;
    else verdict = PS_RA_MISSING;
    *offset = verdict == PS_RA_BY_NID ? by_nid : verdict == PS_RA_BY_TABLE ? by_table : 0;
    return verdict;
}
#endif
