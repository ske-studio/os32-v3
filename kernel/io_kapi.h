#ifndef IO_KAPI_H
#define IO_KAPI_H
#include "types.h"
int kapi_io_policy_set(const void *bytes, unsigned int len);
int kapi_io_policy_get(void *bytes, unsigned int cap, unsigned int *needed);
int kapi_io_policy_reset(void);
int kapi_io_pump(unsigned int budget_us);
/* P2: per-app syscall accounting, including nonlocal park/exit. */
void io_syscall_begin(int owner);
void io_syscall_end(int owner);
extern u32 io_syscall_max_count;
/* Before AS destruction, while the reclaimed slot still holds its identity. */
void io_kapi_owner_exit(int owner);
#endif
