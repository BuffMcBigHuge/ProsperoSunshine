// SPDX-License-Identifier: GPL-3.0-only
#include "ra_resolve.h"
#include <string.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>

#define LOG(...) klog_printf("ProsperoSunshine: ra " __VA_ARGS__)
// An NID hit outside this window behind the map base cannot be one of the module's own functions.
#define MODULE_WINDOW 0x100000

int ps_ra_resolve(uint32_t ra_handle, ps_ra_functions *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof *out);
    out->firmware = kernel_get_fw_version();
    const struct ps_ra_known *known = ps_ra_find_known(out->firmware);
    out->known_firmware = known != NULL;
    out->base = kernel_dynlib_mapbase_addr(-1, ra_handle);
    if (!out->base) { LOG("no map base for handle %#x\n", ra_handle); return -1; }
    LOG("firmware=%#x known=%d\n", out->firmware, out->known_firmware);

    static const char *const verdicts[] = {"nid", "table (nid not found)", "missing", "nid and table disagree"};
    int ok = 1;
    for (int f = 0; f < PS_RA_COUNT; f++) {
        intptr_t address = kernel_dynlib_resolve(-1, ra_handle, ps_ra_nids[f]);
        intptr_t relative = address - out->base;
        out->nid[f] = address && relative > 0 && relative < MODULE_WINDOW ? (uint32_t)relative : 0;
        enum ps_ra_verdict verdict = ps_ra_choose(known, f, out->nid[f], &out->offset[f]);
        if (verdict == PS_RA_MISSING || verdict == PS_RA_CONFLICT) ok = 0;
        LOG("%-10s nid=%s -> %#x table=%#x use=%#x (%s)\n", ps_ra_names[f], ps_ra_nids[f], out->nid[f],
            known ? known->offset[f] : 0, out->offset[f], verdicts[verdict]);
    }
    if (!ok) {
        LOG("refusing firmware %#x: ResourceArbitrator calls not resolved\n", out->firmware);
        return -1;
    }
    if (!known)
        LOG("firmware %#x is unverified: calls found by NID, but the Avcap2 configuration and the pad layouts "
            "were measured on 13.00 only (docs/firmware-porting.md)\n", out->firmware);
    return 0;
}
