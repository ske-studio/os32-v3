/* Persistent shared storage, used by Y4 when the sync backends are wired. */
#include "sync_int.h"
GfxSyncCtx gfxsync_ctx_pool[4]
    __attribute__((section(".bss.gfxsync_ctx_pool"), used));
