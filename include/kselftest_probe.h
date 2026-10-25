#ifndef KSELFTEST_PROBE_H
#define KSELFTEST_PROBE_H

#include "types.h"
#include "os32_kapi_shared.h"

#define KSELFTEST_PROBE_VERSION 1u

/* Private boot selftest ABI. Same calling conditions as inj_push; no new lock. */
u32 kdi_selftest_inj_push(const u8 *p, u32 len, int dest);

/* Preserve the kernel observation used by boot diagnostics and tools. */
void kdi_selftest_lease_result(u32 result);

struct kselftest_exec_view {
    u32 tramp_page;       /* Initialized page, zero before exec_init. */
    const KernelAPI *api; /* Read-only view of the existing kernel table. */
};
void kdi_selftest_exec_view(struct kselftest_exec_view *out);

#endif
