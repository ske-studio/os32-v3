#ifndef OS32_EXEC_RECLAIM_H
#define OS32_EXEC_RECLAIM_H
#include "pcm_contract.h"
#include "kapi_host.h"
static inline void exec_pcm_reclaim(int owner) { pcm_reclaim(owner); }
static inline void exec_host_owner_exit(int owner) { host_owner_exit(owner); }
#endif
