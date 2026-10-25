#ifndef KSELFTEST_PROBE_H
#define KSELFTEST_PROBE_H

#include "types.h"

#define KSELFTEST_PROBE_VERSION 1u

/* Private boot selftest ABI. Same calling conditions as inj_push; no new lock. */
u32 kdi_selftest_inj_push(const u8 *p, u32 len, int dest);

#endif
