// SPDX-License-Identifier: GPL-3.0-only
// Locates the ResourceArbitrator calls (register, unregister, request, list) in the loaded
// libSceResourceArbitrator.sprx by export NID. Only looks symbols up; never calls into the module.
#ifndef PROSPERO_RA_RESOLVE_H
#define PROSPERO_RA_RESOLVE_H
#include <stdint.h>
#include "ra_nids.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t firmware;                // kernel_get_fw_version()
    int known_firmware;               // firmware is in ps_ra_known_firmware
    intptr_t base;                    // module map base
    uint32_t offset[PS_RA_COUNT];     // module-relative offsets to use (valid when resolve returned 0)
    uint32_t nid[PS_RA_COUNT];        // offset found by NID, 0 if not found
} ps_ra_functions;

// ra_handle: dynlib handle of libSceResourceArbitrator.sprx in this process.
// Every call must resolve by NID; on a known firmware the result must equal the table offset, and the
// table stands in if an NID is not found. Logs one klog line per call. Returns 0, or -1 (refuse).
int ps_ra_resolve(uint32_t ra_handle, ps_ra_functions *out);

#ifdef __cplusplus
}
#endif
#endif
