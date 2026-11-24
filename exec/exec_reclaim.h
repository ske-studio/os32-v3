#ifndef OS32_EXEC_RECLAIM_H
#define OS32_EXEC_RECLAIM_H
#include "exec_hooks.h"
#if OS32_HOOK_RECLAIM
#include "kapi_host.h"
static inline void exec_host_owner_exit(int owner) { host_owner_exit(owner); }
#else
void exec_host_owner_exit(int owner);
#endif
#endif
