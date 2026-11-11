/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef INCLUDE_GFX_SYNC_H
#define INCLUDE_GFX_SYNC_H
#include "types.h"
typedef struct GfxSyncTarget {           /* include/gfx_sync.h */
    volatile u8 *va; u32 bytes, pitch; u8 fmt, head;
    int (*sync_read)(void *);            /* method=SYNC_READ */
    int (*wait_port)(void *);            /* method=WAIT_PORT */
    int (*flush)(void *);                /* flush=READBACK (UC の読みの先は backend が知る) */
    int (*read32)(void *, u32 off, u32 *v); /* read=DIRECT */
    void *opaque;
} GfxSyncTarget;
typedef struct GfxSyncCtx GfxSyncCtx;    /* 中身は gfx/sync/ の中だけ。head・面ごとに 1 つ */
int gfxsync_bind(const char *key, const GfxSyncTarget *t, GfxSyncCtx **out); /* 0 / OS32_ERR_FULL (ctx 4 本) / INVAL */
int gfxsync_write_rows(GfxSyncCtx *, u32 x, u32 y, u32 w, u32 h, const u8 *src, u32 pitch);
int gfxsync_read_rows(GfxSyncCtx *, u32 x, u32 y, u32 w, u32 h, u8 *dst, u32 pitch);
int gfxsync_flush(GfxSyncCtx *);
#define GFXSYNC_M_PLAIN 0
#define GFXSYNC_M_GAP 1
#define GFXSYNC_M_SYNC_READ 2
#define GFXSYNC_M_WAIT_PORT 3
int gfxsync_override(GfxSyncCtx *, u32 method, u32 unit, u32 gap);
void gfxsync_override_clear(GfxSyncCtx *);
void gfxsync_unbind(GfxSyncCtx *);
#define GFXSYNC_SHADOW 1
#define GFXSYNC_DIRECT 2
int gfxsync_mode(const GfxSyncCtx *);    /* Y4。bind の後の実効の mode (既定へ落ちた後の値)。NULL は 0 */

void gfxsync_profile(const GfxSyncCtx *, char out[16]);

#endif
