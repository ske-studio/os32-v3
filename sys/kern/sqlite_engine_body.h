#ifndef SQLITE_ENGINE_BODY_H
#define SQLITE_ENGINE_BODY_H
#include "types.h"

typedef struct SqliteModuleApi SqliteModuleApi;
typedef int sqlite_body_init_fn(void *pool, SqliteModuleApi *out);
enum { SQLITE_BODY_OK, SQLITE_BODY_NO_POOL, SQLITE_BODY_NO_STACK, SQLITE_BODY_NO_GUARD, SQLITE_BODY_INIT };
int sqlite_engine_body_start(u32 owner, sqlite_body_init_fn *init, SqliteModuleApi *out, int *init_rc);
enum { SQLITE_STOP_DONE, SQLITE_STOP_BUSY, SQLITE_STOP_SHUTDOWN, SQLITE_STOP_REMAP };
int sqlite_engine_body_stop(int (*shutdown)(void));
#endif
