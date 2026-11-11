#ifndef OS32_EXEC_KAPI_DB_H
#define OS32_EXEC_KAPI_DB_H
#include "kapi_db.h"
static inline void exec_db_cleanup_owned(int owner) { db_cleanup_owned(owner); }
#endif
