#ifndef POWERMAC_HOSTFS_TRACE_H
#define POWERMAC_HOSTFS_TRACE_H

#include "link_trace_contract.h"

void pm_hfs_trace_finish(i32 handle, const char *phase, const char *reason,
                         int rc, i32 detail, u32 received, u32 length);

#endif
