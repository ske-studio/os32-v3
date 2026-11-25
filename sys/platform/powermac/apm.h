#pragma once
#include "types.h"

#define PM_APM_MAX_ENTRIES 128

struct pm_partition { u32 start, sectors; };
int pm_apm_root(int unit, u32 capacity, u32 number, struct pm_partition *root);
int pm_partition_register(int unit, u32 capacity, u32 number);
void pm_partition_enable_writes(void);
