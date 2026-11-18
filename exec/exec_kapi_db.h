#ifndef OS32_EXEC_KAPI_DB_H
#define OS32_EXEC_KAPI_DB_H
#include "exec_hooks.h"
#if OS32_HOOK_KAPI_DB
#include "kapi_db.h"
static inline void exec_db_cleanup_owned(int owner) { db_cleanup_owned(owner); }
#else
/* The shell has no provider for this interface. */
void exec_db_cleanup_owned(int owner);
#endif
#endif
