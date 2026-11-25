#ifndef KAPI_SYSINFO_H
#define KAPI_SYSINFO_H

#include "types.h"

i32 kapi_mem_stat(i32 app_id, void *out, u32 size);
void kapi_sys_build_time_copy(char *buf, u32 size);

#endif
