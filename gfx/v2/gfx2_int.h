/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef GFX_V2_GFX2_INT_H
#define GFX_V2_GFX2_INT_H
#include "types.h"
#include "gfx_hal.h"
#include "gfx_hal2.h"
#include "gfx_sync.h"
#include "memmap.h"
#include "pgalloc.h"
struct addrspace;
#define GFX2_MAX_ADAPTERS 4
#define GFX2_MAX_SURFACES 1024
#define GFX2_HEAP_BLOCKS 2048
#define GFX2_CPU_R 1u
#define GFX2_CPU_W 2u
#define GFX2_SF_ALIAS 1
#define GFX2_SF_KEEP 2
#define GFX2_KIND_PC98 1
_Static_assert(GFX2_KIND_PC98 == LEDGER_SF_PC98, "gfx2 kind PC98");
#define GFX2_KIND_PEGC 2
_Static_assert(GFX2_KIND_PEGC == LEDGER_SF_PEGC, "gfx2 kind PEGC");
#define GFX2_KIND_CIRRUS 3
_Static_assert(GFX2_KIND_CIRRUS == LEDGER_SF_CIRRUS, "gfx2 kind CIRRUS");
#define GFX2_KIND_GEFORCE 4
_Static_assert(GFX2_KIND_GEFORCE == LEDGER_SF_GEFORCE, "gfx2 kind GEFORCE");
#define GFX2_KIND_TRIDENT 5
_Static_assert(GFX2_KIND_TRIDENT == LEDGER_SF_TRIDENT, "gfx2 kind TRIDENT");
#define GFX2_KIND_MAX GFX2_KIND_TRIDENT
struct gfx2_rect { i32 x, y, w, h; };
struct gfx2_head { u32 scan_sid, pend_sid, mode_id, conn_mask; };
struct gfx2_heap { u32 base, size, n; struct { u32 off, bytes, used; } blk[GFX2_HEAP_BLOCKS /* 2048 */]; };
struct gfx2_fence_st { u32 issued, reserved, completed, failed_upto, last_sweep; };  /* reserved: 予約済みで commit 前 (0 = 無し) */
#define GFX2_AD_OK 0
#define GFX2_AD_LOST 1
#define GFX2_AD_LOST_HELD 2
struct gfx2_caller { int app_id; struct addrspace *as; u32 owner; };   /* C1 の gfx2_caller_get (§6-5) */
struct gfx2_adapter {
    u32 index, gen, state;               /* C1 (登録)・C3 (喪失で gen+1・state = GFX2_AD_*)・C1 (ACTIVE を外す時に gen+1) */
    u32 flags;                           /* C1 だけ (GFX2_AF_ACTIVE・ONBOARD。ACTIVE は全部の adapter で 1 つ) */
    u32 vram_phys;                       /* C1 (be->heap_range の phys。VRAM の面の PTE の元) */
    GfxBackend *v1;                      /* C1。hook は呼ぶたびに a->v1-> から読む (H4 が書き換える、D2) */
    const struct GfxBackendV2 *be; void *ctx;   /* C1 (be は kernel の中の表、常に非 NULL: v2 の表・C16 の shim・C16 の lost の表のどれか、§11-7) */
    u32 kind;                            /* C1。GFX2_KIND_PC98 1・PEGC 2・CIRRUS 3・GEFORCE 4・TRIDENT 5 (LEDGER_SF_* と同じ値、
                                            display_provider_ledger(v1) の戻り値をそのまま入れる) */
    GFX2_Caps caps;                      /* C10 */
    struct gfx2_fence_st fence;          /* C3 だけが書く (reserved は reserve、issued は commit) */
    struct gfx2_heap heap;               /* C5 だけが書く */
    u32 heads; struct gfx2_head head[2]; /* C12・C13 だけが書く */
    u32 restore_on_leave;                /* C12 */
};   /* 同期層の ctx は持たない。gfx2_sync_get で毎回引く (P1-2) */
struct gfx2_alias_desc {                 /* C14 が v1 の今の値から作り、C4 に渡す */
    u32 adapter;                         /* ACTIVE の adapter (gfx2_active() の index) */
    u16 w, h; u8 fmt, pool, head, role; u32 pitch, bytes, gpu_off;
    u8 *kva;                             /* 借りる。C4 は解放しない */
};
struct gfx2_surf {
    u32 sid, gen, adapter, owner, state; /* C4 だけが書く (state は C7 の lock/unlock も gfx2_surf_state_set 経由で) */
    u16 w, h; u8 fmt, pool, usage, tiling; u32 pitch, bytes, gpu_off;  /* C4 (作成の時だけ) */
    u32 last_write, last_read;           /* C3 だけ (gfx2_fence_note) */
    u16 scan_ref, flags;                 /* C4 (scan_ref_get/put)。flags: GFX2_SF_ALIAS 1 (C4)・GFX2_SF_KEEP 2 (C4・C15) */
    u8 head, role;                       /* C4 (別名だけ。同期層の ctx を引く head) */
    struct gfx2_rect dirty;              /* C4 (gfx2_dirty_add)、C13 の present が読んで 0 に */
    struct addrspace *map_as; u32 map_va, map_npages; /* C7 だけ */
    u8 *staging; u32 staging_pitch;      /* C7 (同期の面の lock の写し) */
    u8 *kva;                             /* C4 (作成の時に be->kva)・C18 (SYSMEM の P2V)。GART の面は NULL (§7-2) */
    u32 heap_off;                        /* C5 (gfx2_heap_alloc の結果、C4 が格納) */
    u32 gart_first, gart_n, gart_moved; /* C6 だけ (gart_moved は隔離へ移せた頁数) */
    u32 sys_pfn, sys_n;                  /* C18 だけ (SYSMEM の連続の頁、§7-2) */
    u32 priv[4];                         /* backend の私物 (GF: 予約の id・世代・node の番号、G9) */
};
struct gfx2_vaspace { u32 as_gen; u32 bits[(MEM_GFX2_MAP_END - MEM_GFX2_MAP_BASE) / 4096 / 32]; }; /* C7、4 本。帯の定数は include/memmap.h (A0、§11-4) */
extern struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS /* 4 */];  /* 実体は C1 */
/* C1 allocates the persistent quarantine owner during gfx2 initialization. */
extern u32 gfx2_quar_owner;
#define GFX2_QUAR_OWNER gfx2_quar_owner
/* C4 alias sentinel: never a ledger-allocated owner. */
#define GFX2_OWNER_SCREEN 0xffffffffUL
extern u32 gfx2_pal[256];                                             /* 実体は C13 (§9-2 のパレットの写し) */
/* GFX2_KIND_* は mm/pgalloc.h:297-301 の LEDGER_SF_* と同じ値。A0 が gfx2_int.h に置き、
   _Static_assert で 5 つとも等しいことを確かめる */
extern struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES /* 1024 */];     /* 実体は C4 */

extern const GfxBackendV2 gfx2_v1_shim, gfx2_lost_be;
struct gfx2_adapter *gfx2_adapter_get(u32 a);
int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags);
void gfx2_adapter_activate(struct gfx2_adapter *);
struct gfx2_adapter *gfx2_active(void);
void gfx2_adapter_unregister(GfxBackend *v1);
int gfx2_caller_get(struct gfx2_caller *);
int gfx2_caps_from_v1(struct gfx2_adapter *, GFX2_Caps *);
void gfx2_caps_refresh(struct gfx2_adapter *a);
int gfx2_v1_enum_head(const GfxBackend *v1, u32 head, GFX2_HeadInfo *o);
int gfx2_v1_enum_conn(const GfxBackend *v1, u32 conn, GFX2_ConnectorInfo *o);
int gfx2_v1_enum_mode(const GfxBackend *v1, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *o);
int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx);
int gfx2_sync_get(const struct gfx2_adapter *, u32 head, GfxSyncCtx **ctx, u32 *mode);
struct gfx2_surf *gfx2_surf_get(u32 sid, u32 caller_owner, int *rc);
int gfx2_surf_new(struct gfx2_adapter *, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 owner, struct gfx2_surf **out);
int gfx2_surf_alias(struct gfx2_adapter *, const struct gfx2_alias_desc *, struct gfx2_surf **out);
int gfx2_surf_alias_update(struct gfx2_surf *, const struct gfx2_alias_desc * );
int gfx2_surf_destroy(struct gfx2_surf *);
int gfx2_surf_state_set(struct gfx2_surf *, u32 st);
void gfx2_scan_ref_swap(struct gfx2_adapter *, u32 head, struct gfx2_surf *);
void gfx2_dirty_add(struct gfx2_surf *, const struct gfx2_rect *);
void gfx2_closing_sweep(struct gfx2_adapter *);
u32 gfx2_fence_reserve(struct gfx2_adapter *);
int gfx2_fence_commit(struct gfx2_adapter *, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src);
void gfx2_fence_commit_fail(struct gfx2_adapter *, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src, int rc);
void gfx2_fence_abandon(struct gfx2_adapter *, u32 f);
int gfx2_fence_done(struct gfx2_adapter *, u32 f);
int gfx2_fence_wait_k(struct gfx2_adapter *, u32 f, u32 dl_us);
int gfx2_cpu_begin(struct gfx2_surf *, u32 access, u32 dl_us);
void gfx2_cpu_end(struct gfx2_surf *, u32 access, const struct gfx2_rect *);
int gfx2_surf_quiesce(struct gfx2_surf *, u32 dl_us);
void gfx2_adapter_lost(struct gfx2_adapter *, int held);
void gfx2_adapter_recovered(struct gfx2_adapter *);
int gfx2_heap_init(struct gfx2_heap *, u32 base, u32 size);
int gfx2_heap_alloc(struct gfx2_heap *, u32 bytes, u32 align, u32 *off);
int gfx2_heap_free(struct gfx2_heap *, u32 off);
int gfx2_gart_alloc(struct gfx2_adapter *, struct gfx2_surf *, u32 owner);
int gfx2_gart_free(struct gfx2_adapter *, struct gfx2_surf *);
int gfx2_gart_pfns(const struct gfx2_surf *, const u32 **pfn, u32 *n);
int gfx2_gart_quarantine(struct gfx2_surf *);
int gfx2_map(struct gfx2_surf *, struct addrspace *, u32 *va);
int gfx2_unmap(struct gfx2_surf *);
int gfx2_surf_page(const struct gfx2_surf *, u32 k, u32 *pa, u32 *attr);
int gfx2_cpu_visible(const struct gfx2_surf *);
int gfx2_map_audit(const struct addrspace *);
int gfx2_sysmem_alloc(struct gfx2_surf *, u32 owner);
int gfx2_sysmem_free(struct gfx2_surf *);
void gfx2_head_detach(struct gfx2_adapter *, u32 head);
void gfx2_map_detach(struct gfx2_surf *);
void gfx2_staging_drop(struct gfx2_surf *);
void gfx2_vaspace_drop(struct addrspace *);
int gfx2_head_restore_v1(struct gfx2_adapter *, u32 head);
void gfx2_pal_set(u32 first, u32 count, const u32 *xrgb);
void gfx2_pal_set_v1(int first, int count, const u8 *rgb);
void gfx2_pal_reset16(void);
int gfx2_surf_quarantine(struct gfx2_surf *);
void gfx2_quarantine_drain(struct gfx2_adapter *);
int v1_bridge_begin(u32 role, u32 access);
void v1_bridge_end(u32 role, u32 access, u32 fence);
void v1_bridge_rebind(void);
void gfx2_scan_ref_get(struct gfx2_surf *);
void gfx2_scan_ref_put(struct gfx2_surf *);

#endif
