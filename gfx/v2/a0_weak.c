/* A0 placeholders; replaced by strong definitions in the owning stages. */
#include "gfx2_sw.h"
#include "gfx2_exec.h"
__attribute__((weak)) int gfx2_adapter_count(void)
{
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_adapter_info(u32 a, GFX2_AdapterInfo *o)
{
    (void)a;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_adapter_gen(u32 a, u32 *gen)
{
    (void)a;
    (void)gen;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_caps(u32 a, GFX2_Caps *o, u32 size)
{
    (void)a;
    (void)o;
    (void)size;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_check_op(u32 a, const GFX2_Op *shape, u32 sfmt, u32 dfmt, u32 spool, u32 dpool, u32 *how)
{
    (void)a;
    (void)shape;
    (void)sfmt;
    (void)dfmt;
    (void)spool;
    (void)dpool;
    (void)how;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surface_create(u32 a, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 *sid)
{
    (void)a;
    (void)w;
    (void)h;
    (void)fmt;
    (void)pool;
    (void)usage;
    (void)sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surface_free(u32 sid)
{
    (void)sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surface_desc(u32 sid, GFX2_SurfaceDesc *o)
{
    (void)sid;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_screen_surface(u32 a, u32 head, u32 role, u32 *sid)
{
    (void)a;
    (void)head;
    (void)role;
    (void)sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_lock(u32 sid, u32 flags, void **va, u32 *pitch)
{
    (void)sid;
    (void)flags;
    (void)va;
    (void)pitch;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_unlock(u32 sid)
{
    (void)sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_submit(u32 a, const GFX2_Op *ops, u32 n, u32 gen, u32 *done, u32 *fence)
{
    (void)a;
    (void)ops;
    (void)n;
    (void)gen;
    (void)done;
    (void)fence;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_fence_wait(u32 a, u32 fence, u32 deadline_ms)
{
    (void)a;
    (void)fence;
    (void)deadline_ms;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_fence_query(u32 a, u32 *completed, u32 *failed_upto)
{
    (void)a;
    (void)completed;
    (void)failed_upto;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_head_info(u32 a, u32 head, GFX2_HeadInfo *o)
{
    (void)a;
    (void)head;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_connector_info(u32 a, u32 c, GFX2_ConnectorInfo *o)
{
    (void)a;
    (void)c;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_connector_edid(u32 a, u32 c, u32 block, u8 *out128)
{
    (void)a;
    (void)c;
    (void)block;
    (void)out128;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_mode_count(u32 a, u32 head, u32 fmt)
{
    (void)a;
    (void)head;
    (void)fmt;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_mode_get(u32 a, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *o)
{
    (void)a;
    (void)head;
    (void)fmt;
    (void)i;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_mode_set(u32 a, u32 head, u32 conn_mask, u32 mode_id, u32 sid)
{
    (void)a;
    (void)head;
    (void)conn_mask;
    (void)mode_id;
    (void)sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_flip(u32 a, u32 head, u32 sid, u32 flags, u32 deadline_ms)
{
    (void)a;
    (void)head;
    (void)sid;
    (void)flags;
    (void)deadline_ms;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_vblank_wait(u32 a, u32 head, u32 after, u32 deadline_ms, u32 *seen)
{
    (void)a;
    (void)head;
    (void)after;
    (void)deadline_ms;
    (void)seen;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_palette(u32 a, u32 first, u32 count, const u32 *xrgb)
{
    (void)a;
    (void)first;
    (void)count;
    (void)xrgb;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_gamma(u32 a, u32 head, const u16 *rgb768)
{
    (void)a;
    (void)head;
    (void)rgb768;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_cursor_image(u32 a, u32 head, u32 sid)
{
    (void)a;
    (void)head;
    (void)sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_cursor_move(u32 a, u32 head, i32 x, i32 y)
{
    (void)a;
    (void)head;
    (void)x;
    (void)y;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_cursor_show(u32 a, u32 head, u32 show)
{
    (void)a;
    (void)head;
    (void)show;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_dpms(u32 a, u32 head, u32 state)
{
    (void)a;
    (void)head;
    (void)state;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_present(u32 a, u32 head, i16 x, i16 y, u16 w, u16 h)
{
    (void)a;
    (void)head;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) struct gfx2_adapter * gfx2_adapter_get(u32 a)
{
    (void)a;
    return 0;
}
__attribute__((weak)) int gfx2_adapter_register(GfxBackend *v1, u32 kind, u32 flags)
{
    (void)v1;
    (void)kind;
    (void)flags;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_adapter_activate(struct gfx2_adapter * a0)
{
    (void)a0;
}
__attribute__((weak)) struct gfx2_adapter * gfx2_active(void)
{
    return 0;
}
__attribute__((weak)) void gfx2_adapter_unregister(GfxBackend *v1)
{
    (void)v1;
}
__attribute__((weak)) int gfx2_caller_get(struct gfx2_caller * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_caps_from_v1(struct gfx2_adapter * a0, GFX2_Caps * a1)
{
    (void)a0;
    (void)a1;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_caps_refresh(struct gfx2_adapter *a)
{
    (void)a;
}
__attribute__((weak)) int gfx2_v1_enum_head(const GfxBackend *v1, u32 head, GFX2_HeadInfo *o)
{
    (void)v1;
    (void)head;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_v1_enum_conn(const GfxBackend *v1, u32 conn, GFX2_ConnectorInfo *o)
{
    (void)v1;
    (void)conn;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_v1_enum_mode(const GfxBackend *v1, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *o)
{
    (void)v1;
    (void)head;
    (void)fmt;
    (void)i;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sync_publish(u32 kind, u32 head, GfxSyncCtx *ctx)
{
    (void)kind;
    (void)head;
    (void)ctx;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sync_get(const struct gfx2_adapter * a0, u32 head, GfxSyncCtx **ctx, u32 *mode)
{
    (void)a0;
    (void)head;
    (void)ctx;
    (void)mode;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) struct gfx2_surf * gfx2_surf_get(u32 sid, u32 caller_owner, int *rc)
{
    (void)sid;
    (void)caller_owner;
    (void)rc;
    return 0;
}
__attribute__((weak)) int gfx2_surf_new(struct gfx2_adapter * a0, u16 w, u16 h, u8 fmt, u8 pool, u8 usage, u32 owner, struct gfx2_surf **out)
{
    (void)a0;
    (void)w;
    (void)h;
    (void)fmt;
    (void)pool;
    (void)usage;
    (void)owner;
    (void)out;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surf_alias(struct gfx2_adapter * a0, const struct gfx2_alias_desc * a1, struct gfx2_surf **out)
{
    (void)a0;
    (void)a1;
    (void)out;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surf_alias_update(struct gfx2_surf * a0, const struct gfx2_alias_desc * a1)
{
    (void)a0;
    (void)a1;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surf_destroy(struct gfx2_surf * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surf_state_set(struct gfx2_surf * a0, u32 st)
{
    (void)a0;
    (void)st;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_scan_ref_swap(struct gfx2_adapter * a0, u32 head, struct gfx2_surf * a2)
{
    (void)a0;
    (void)head;
    (void)a2;
}
__attribute__((weak)) void gfx2_dirty_add(struct gfx2_surf * a0, const struct gfx2_rect * a1)
{
    (void)a0;
    (void)a1;
}
__attribute__((weak)) void gfx2_closing_sweep(struct gfx2_adapter * a0)
{
    (void)a0;
}
__attribute__((weak)) u32 gfx2_fence_reserve(struct gfx2_adapter * a0)
{
    (void)a0;
    return 0;
}
__attribute__((weak)) int gfx2_fence_commit(struct gfx2_adapter * a0, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src)
{
    (void)a0;
    (void)f;
    (void)dst;
    (void)src;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_fence_commit_fail(struct gfx2_adapter * a0, u32 f, struct gfx2_surf *dst, struct gfx2_surf *src, int rc)
{
    (void)a0;
    (void)f;
    (void)dst;
    (void)src;
    (void)rc;
}
__attribute__((weak)) void gfx2_fence_abandon(struct gfx2_adapter * a0, u32 f)
{
    (void)a0;
    (void)f;
}
__attribute__((weak)) int gfx2_fence_done(struct gfx2_adapter * a0, u32 f)
{
    (void)a0;
    (void)f;
    return 0;
}
__attribute__((weak)) int gfx2_fence_wait_k(struct gfx2_adapter * a0, u32 f, u32 dl_us)
{
    (void)a0;
    (void)f;
    (void)dl_us;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_cpu_begin(struct gfx2_surf * a0, u32 access, u32 dl_us)
{
    (void)a0;
    (void)access;
    (void)dl_us;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_cpu_end(struct gfx2_surf * a0, u32 access, const struct gfx2_rect * a2)
{
    (void)a0;
    (void)access;
    (void)a2;
}
__attribute__((weak)) int gfx2_surf_quiesce(struct gfx2_surf * a0, u32 dl_us)
{
    (void)a0;
    (void)dl_us;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_adapter_lost(struct gfx2_adapter * a0, int held)
{
    (void)a0;
    (void)held;
}
__attribute__((weak)) void gfx2_adapter_recovered(struct gfx2_adapter * a0)
{
    (void)a0;
}
__attribute__((weak)) int gfx2_heap_init(struct gfx2_heap * a0, u32 base, u32 size)
{
    (void)a0;
    (void)base;
    (void)size;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_heap_alloc(struct gfx2_heap * a0, u32 bytes, u32 align, u32 *off)
{
    (void)a0;
    (void)bytes;
    (void)align;
    (void)off;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_heap_free(struct gfx2_heap * a0, u32 off)
{
    (void)a0;
    (void)off;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_gart_alloc(struct gfx2_adapter * a0, struct gfx2_surf * a1, u32 owner)
{
    (void)a0;
    (void)a1;
    (void)owner;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_gart_free(struct gfx2_adapter * a0, struct gfx2_surf * a1)
{
    (void)a0;
    (void)a1;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_gart_pfns(const struct gfx2_surf * a0, const u32 **pfn, u32 *n)
{
    (void)a0;
    (void)pfn;
    (void)n;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_gart_quarantine(struct gfx2_surf * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_map(struct gfx2_surf * a0, struct addrspace * a1, u32 *va)
{
    (void)a0;
    (void)a1;
    (void)va;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_unmap(struct gfx2_surf * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_surf_page(const struct gfx2_surf * a0, u32 k, u32 *pa, u32 *attr)
{
    (void)a0;
    (void)k;
    (void)pa;
    (void)attr;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_cpu_visible(const struct gfx2_surf * a0)
{
    (void)a0;
    return 0;
}
__attribute__((weak)) int gfx2_map_audit(const struct addrspace * a0)
{
    (void)a0;
    return 0;
}
__attribute__((weak)) int gfx2_sysmem_alloc(struct gfx2_surf * a0, u32 owner)
{
    (void)a0;
    (void)owner;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sysmem_free(struct gfx2_surf * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_head_detach(struct gfx2_adapter * a0, u32 head)
{
    (void)a0;
    (void)head;
}
__attribute__((weak)) void gfx2_map_detach(struct gfx2_surf * a0)
{
    (void)a0;
}
__attribute__((weak)) void gfx2_staging_drop(struct gfx2_surf * a0)
{
    (void)a0;
}
__attribute__((weak)) void gfx2_vaspace_drop(struct addrspace * a0)
{
    (void)a0;
}
__attribute__((weak)) int gfx2_head_restore_v1(struct gfx2_adapter * a0, u32 head)
{
    (void)a0;
    (void)head;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_pal_set(u32 first, u32 count, const u32 *xrgb)
{
    (void)first;
    (void)count;
    (void)xrgb;
}
__attribute__((weak)) void gfx2_pal_set_v1(int first, int count, const u8 *rgb)
{
    (void)first;
    (void)count;
    (void)rgb;
}
__attribute__((weak)) void gfx2_pal_reset16(void)
{
}
__attribute__((weak)) int gfx2_surf_quarantine(struct gfx2_surf * a0)
{
    (void)a0;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gfx2_quarantine_drain(struct gfx2_adapter * a0)
{
    (void)a0;
}
__attribute__((weak)) int v1_bridge_begin(u32 role, u32 access)
{
    (void)role;
    (void)access;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void v1_bridge_end(u32 role, u32 access, u32 fence)
{
    (void)role;
    (void)access;
    (void)fence;
}
__attribute__((weak)) void v1_bridge_rebind(void)
{
}
__attribute__((weak)) void gfx2_scan_ref_get(struct gfx2_surf * a0)
{
    (void)a0;
}
__attribute__((weak)) void gfx2_scan_ref_put(struct gfx2_surf * a0)
{
    (void)a0;
}
__attribute__((weak)) int gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o)
{
    (void)fmt;
    (void)o;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) u32 gfx2_px_read(const u8 *p, u32 fmt)
{
    (void)p;
    (void)fmt;
    return 0;
}
__attribute__((weak)) u32 gfx2_to_argb(u32 fmt, u32 v, const u32 *pal256)
{
    (void)fmt;
    (void)v;
    (void)pal256;
    return 0;
}
__attribute__((weak)) int gfx2_from_argb(u32 fmt, u32 argb, u32 *v)
{
    (void)fmt;
    (void)argb;
    (void)v;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) u32 gfx2_yuv_argb(u8 y, u8 u, u8 v)
{
    (void)y;
    (void)u;
    (void)v;
    return 0;
}
__attribute__((weak)) int gfx2_sw_fill(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_copy(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_rop(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_rop4(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_mono(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_hostdata(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_readback(const struct gfx2_exec * a0, u8 *kbuf)
{
    (void)a0;
    (void)kbuf;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_stretch(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_ckey(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_alpha(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_sw_line(const struct gfx2_exec * a0, struct gfx2_rect *touched)
{
    (void)a0;
    (void)touched;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gfx2_owner_release(struct addrspace *as)
{
    (void)as;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int pgalloc_pin(u32 pfn, u32 n, u32 owner)
{
    (void)pfn;
    (void)n;
    (void)owner;
    return 0;
}
__attribute__((weak)) int pgalloc_unpin(u32 pfn, u32 n, u32 owner)
{
    (void)pfn;
    (void)n;
    (void)owner;
    return 0;
}
__attribute__((weak)) int pgalloc_pinned(u32 pfn)
{
    (void)pfn;
    return 0;
}
__attribute__((weak)) int ledger_transfer_pinned(u32 pfn, int n, u32 from, u32 to)
{
    (void)pfn;
    (void)n;
    (void)from;
    (void)to;
    return 0;
}
__attribute__((weak)) int paging_gfx2_map(struct addrspace *as, u32 va, u32 pa, u32 npages, u32 pte_flags)
{
    (void)as;
    (void)va;
    (void)pa;
    (void)npages;
    (void)pte_flags;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) struct gfx2_adapter gfx2_adapters[GFX2_MAX_ADAPTERS];
__attribute__((weak)) struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
__attribute__((weak)) u32 gfx2_pal[256];
__attribute__((weak)) const GfxBackendV2 gfx2_v1_shim, gfx2_lost_be;
__attribute__((weak)) const gfx2_sw_fn gfx2_sw_table[GFX2_OP_MAX + 1];

__attribute__((weak)) void gfx2_px_write(u8 *p, u32 fmt, u32 v)
{
    (void)p;
    (void)fmt;
    (void)v;
}
__attribute__((weak)) u32 gfx2_div255(u32 t)
{
    (void)t;
    return 0;
}
