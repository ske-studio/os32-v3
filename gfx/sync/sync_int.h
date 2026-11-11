/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef GFX_SYNC_SYNC_INT_H
#define GFX_SYNC_SYNC_INT_H
#include "gfx_sync.h"
#include "os32_kapi_shared.h"
#define GFXSYNC_M_PLAIN 0
#define GFXSYNC_M_GAP 1
#define GFXSYNC_M_SYNC_READ 2
#define GFXSYNC_M_WAIT_PORT 3
#define GFXSYNC_F_NONE 0
#define GFXSYNC_F_READBACK 1
#define GFXSYNC_F_SERIALIZE 2
#define GFXSYNC_R_SHADOW 1
#define GFXSYNC_R_DIRECT 2
struct gfxsync_cfg { u8 mode, unit, method, flush, read; u32 gap_ns, read_ns; char key[16]; };
struct GfxSyncCtx { GfxSyncTarget t; struct gfxsync_cfg cfg; u32 gap_loops; u8 used; };
int gfxsync_cfg_lookup(const char *key, struct gfxsync_cfg *out); /* Y1。0 = 区画を見つけた、NOTFOUND = 既定を入れた */
int gfxsync_cfg_check(const struct gfxsync_cfg *, const GfxSyncTarget *); /* Y4。0 / INVAL (§10 の表) */
u32 gfxsync_gap_loops(u32 loops_per_tick, u32 ns);                         /* Y2。write_gap.c の算数 */
int gfxsync_row_write(GfxSyncCtx *, u32 off, const u8 *src, u32 bytes);    /* Y3 */
int gfxsync_row_read(GfxSyncCtx *, u32 off, u8 *dst, u32 bytes);           /* Y3 */
extern GfxSyncCtx gfxsync_ctx_pool[4];                                     /* 実体は Y4 (sync_bind.c) */

#endif
