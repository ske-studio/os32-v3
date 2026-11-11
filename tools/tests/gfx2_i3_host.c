/* I3 composes the real I1 lifecycle with C11-C13 and Y1-Y4. Device/VFS edges only. */
#define _start i1_start
#include "gfx2_i1_host.c"
#undef _start
#include "vfs.h"
#include "config.h"
#include "sync_defaults.inc"
static u32 config_pos, mode_calls, flip_ready, fullscreen = 1;
static int missing;
static u8 video[1024];
static u32 hw_draws;
static const u8 *config_view = config_text;
static u32 config_size = sizeof(config_text);
int ring3_call_from_user(void) { return 0; }
int ring3_user_range_ok(u32 p, u32 n) { return p && n; }
int ring3_user_range_writable(u32 p, u32 n) { return p && n; }
static int read32(void *c, u32 off, u32 *value)
{ (void)c; *value = *(u32 *)(video + off); return 0; }
static int choose(void *c, const struct gfx2_exec *e) { (void)c; (void)e; return 0; }
static int draw(void *c, const struct gfx2_exec *e, u32 f)
{ (void)c; (void)e; CHECK(f); hw_draws++; return 0; }
static const GfxMode modes[] = {{64, 4, GFX2_FMT_XRGB8888, 8, 7, 0, 256}};
u32 cpu_loops_per_tick(void) { return 8125000; }
int gfx_fullscreen_owner(void) { return fullscreen; }
void gfx_policy_scan_changed(void) {}
int vfs_open(const char *path, int flags)
{ CHECK(!kstrcmp(path, SYS_GFXSYNC_CFG) && flags == O_RDONLY); config_pos = 0; return missing ? -1 : 1; }
int vfs_read_fd(int fd, void *buf, u32 n)
{
    CHECK(fd == 1);
    if (n > config_size - config_pos) n = config_size - config_pos;
    kmemcpy(buf, config_view + config_pos, n); config_pos += n; return n;
}
void vfs_close(int fd) { CHECK(fd == 1); }
static int set_mode(void *c, u32 h, u32 mask, const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{ (void)c; CHECK(!h && mask == 1 && m->id == 7 && s->w == 64); mode_calls++; return 0; }
static int flip(void *c, u32 h, const struct gfx2_surf *s, u32 flags, u32 dl)
{ (void)c; CHECK(!h && s->w == 64 && flags == GFX2_FLIP_VBLANK && dl == 1000000); return 0; }
static int flip_done(void *c, u32 h, u32 *sid)
{ struct gfx2_adapter *a = c; CHECK(!h); *sid = flip_ready ? a->head[h].scan_sid : 0; return flip_ready ? 0 : OS32_ERR_BUSY; }
static int vblank(void *c, u32 h, u32 after, u32 dl, u32 *seen)
{ (void)c; CHECK(!h && dl == 1000000); *seen = after + 1; return 0; }
void _start(void)
{
    u32 args[6] = {KERNEL_LOAD_ADDR, MEM_POOL_BASE + 0x4000000 - KERNEL_LOAD_ADDR,
                  3, 0x32, 0xffffffffUL, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == KERNEL_LOAD_ADDR);
    args[0] = MEM_GFX2_MAP_BASE; args[1] = 16 * PAGE_SIZE;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == MEM_GFX2_MAP_BASE);
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    v1.name = "I3 display"; v1.modes = modes; v1.mode_count = 1;
    CHECK(!gfx2_adapter_register(&v1, GFX2_KIND_PC98, GFX2_AF_ACTIVE));
    struct gfx2_adapter *a = gfx2_adapter_get(0);
    begin();
    be = gfx2_v1_shim; be.mode_set = set_mode; be.flip = flip;
    be.flip_done = flip_done; be.vblank_wait = vblank; a->be = &be;
    CHECK(gfx2_adapter_count() == 1 && a->heads == 1);
    GFX2_AdapterInfo ai; GFX2_ConnectorInfo ci; GFX2_ModeInfo mi;
    CHECK(!gfx2_adapter_info(0, &ai) && ai.heads == 1);
    CHECK(!gfx2_connector_info(0, 0, &ci) && ci.possible_heads == 1);
    CHECK(gfx2_mode_count(0, 0, GFX2_FMT_XRGB8888) == 1);
    CHECK(!gfx2_mode_get(0, 0, GFX2_FMT_XRGB8888, 0, &mi) && mi.id == 7);
    struct gfx2_surf *front = surface(GFX2_POOL_SYSMEM, 4);
    struct gfx2_surf *next = surface(GFX2_POOL_SYSMEM, 4);
    front->usage |= GFX2_USE_SCANOUT; next->usage |= GFX2_USE_SCANOUT;
    CHECK(!gfx2_mode_set(0, 0, 1, mi.id, front->sid));
    CHECK(mode_calls == 1 && a->restore_on_leave && front->scan_ref == 1);
    u32 owner_pages = ledger_owner_pages(as.owner), old_sid = front->sid;
    CHECK(!gfx2_surface_free(old_sid) && front->state == GFX2_ST_CLOSING);
    CHECK(front->scan_ref == 1 && ledger_owner_pages(as.owner) == owner_pages);
    CHECK(!gfx2_flip(0, 0, next->sid, GFX2_FLIP_VBLANK, 1000));
    CHECK(next->scan_ref == 1 && a->head[0].pend_sid == next->sid);
    CHECK(gfx2_flip(0, 0, next->sid, 0, 1000) == OS32_ERR_BUSY);
    u32 seen, done, failed;
    CHECK(!gfx2_vblank_wait(0, 0, 0, 1000, &seen));
    CHECK(front->state == GFX2_ST_CLOSING && ledger_owner_pages(as.owner) == owner_pages);
    flip_ready = 1;
    CHECK(!gfx2_vblank_wait(0, 0, seen, 1000, &seen));
    CHECK(front->state == GFX2_ST_FREE);
    CHECK(!gfx2_fence_query(0, &done, &failed));
    CHECK(a->head[0].scan_sid == next->sid && !a->head[0].pend_sid && next->scan_ref == 1);
    CHECK(!gfx2_vblank_wait(0, 0, seen, 1000, &seen));
    CHECK(front->state == GFX2_ST_FREE && ledger_owner_pages(as.owner) < owner_pages);
    GFX2_SurfaceDesc desc;
    CHECK(gfx2_surface_desc(old_sid, &desc) == OS32_ERR_STALE);
    /* Actual DISPLAY lease release must restore to the borrowed v1 surface. */
    struct gfx2_alias_desc ad = {.adapter=0, .head=0, .role=GFX2_ROLE_DISPLAY,
        .fmt=GFX2_FMT_XRGB8888, .pool=GFX2_POOL_SYSMEM, .w=64, .h=4,
        .pitch=256, .bytes=next->bytes, .kva=next->kva};
    struct gfx2_surf *alias;
    CHECK(!gfx2_surf_alias(a, &ad, &alias));
    ledger_surfaces[0] = (struct ledger_surface){.role=LEDGER_ROLE_DISPLAY,
        .first=next->sys_pfn, .npages=1, .perm_max=LEDGER_PERM_RW};
    struct lease_mapping map = {.base=MEM_LEASE_BASE, .phys=next->sys_pfn * PAGE_SIZE,
        .npages=1, .flags=PTE_PRESENT | PTE_USER | PTE_RW, .token=42, .sid=0};
    CHECK(!paging_lease_map(&as, &map, 1));
    CHECK(!lease_release(&as, 42));
    CHECK(mode_calls == 2 && !a->restore_on_leave && a->head[0].scan_sid == alias->sid);
    CHECK(!next->scan_ref);
    gfx2_head_detach(a, 0);
    /* D1/Y1/Y4: read the actual asset and bind its PEGC and PCI wildcard keys. */
    GfxSyncTarget target = {.va=video, .bytes=sizeof(video), .pitch=64, .fmt=GFX2_FMT_INDEX8};
    GfxSyncCtx *ctx;
    char profile[16];
    CHECK(!gfxsync_bind("PEGC", &target, &ctx));
    CHECK(ctx->cfg.unit == 4 && ctx->cfg.method == GFXSYNC_M_PLAIN &&
          ctx->cfg.flush == GFXSYNC_F_NONE && ctx->cfg.read_ns == 0 && ctx->cfg.read == GFXSYNC_R_SHADOW);
    gfxsync_profile(ctx, profile); CHECK(!kstrcmp(profile, "pegc"));
    CHECK(!gfx2_sync_publish(GFX2_KIND_PC98, 0, ctx));
    GFX2_Caps caps;
    CHECK(!gfx2_caps(0, &caps, sizeof(caps)) && caps.cpu_direct == 1 && caps.sync_mode == GFXSYNC_SHADOW);
    CHECK(!kstrcmp(caps.sync_profile, "pegc"));
    /* Match target shape to the DISPLAY alias for real present/rows. */
    ctx->t.pitch = 256; ctx->t.bytes = sizeof(video);
    alias->dirty = (struct gfx2_rect){1, 0, 3, 1};
    kmemset(alias->kva + 4, 0x5a, 12); ctx->t.fmt = GFX2_FMT_XRGB8888;
    CHECK(!gfx2_present(0, 0, 0, 0, 64, 1) && !alias->dirty.w);
    for (u32 i=4; i<16; i++) CHECK(video[i] == 0x5a);
    /* C7/C8/C9/C13 must all see the same published mode. */
    be.check = choose; be.emit = draw;
    GFX2_Op op = {.op=GFX2_OP_FILL, .dst=alias->sid, .dw=1, .dh=1,
                  .rop=GFX2_ROP_PATCOPY, .fg={.v=0x12345678}};
    struct gfx2_exec ex = {.op=&op, .dst=alias};
    u32 how;
    CHECK(!gfx2_check_exec(a, &ex, &how) && how == GFX2_HOW_SW);
    void *va; u32 pitch;
    CHECK(gfx2_lock(alias->sid, 0, &va, &pitch) == OS32_ERR_INVAL);
    CHECK(!gfx2_submit(0, &op, 1, a->gen, &done, &failed) && done == 1 && !hw_draws);
    CHECK(*(u32 *)alias->kva == 0x12345678 && alias->dirty.w);
    CHECK(!gfx2_present(0, 0, 0, 0, 64, 4) && *(u32 *)video == 0x12345678);
    CHECK(!gfx2_sync_publish(GFX2_KIND_PC98, 0, NULL)); gfxsync_unbind(ctx);
    static const u8 direct[] = "[direct]\nmode=DIRECT unit=4 read=DIRECT\n";
    config_view = direct; config_size = sizeof(direct) - 1;
    target.pitch = 256; target.fmt = GFX2_FMT_XRGB8888; target.read32 = read32;
    CHECK(!gfxsync_bind("direct", &target, &ctx));
    CHECK(!gfx2_sync_publish(GFX2_KIND_PC98, 0, ctx));
    CHECK(!gfx2_check_exec(a, &ex, &how) && how == GFX2_HOW_HW);
    CHECK(!gfx2_lock(alias->sid, GFX2_LOCK_DISCARD, &va, &pitch));
    CHECK((u32)va >= MEM_GFX2_MAP_BASE && alias->staging && alias->map_as == &as);
    paging_load_cr3(as.pd_phys); *(u32 *)va = 0xaabbccdd;
    paging_load_cr3(paging_kernel_pd_phys());
    CHECK(!gfx2_unlock(alias->sid) && !alias->staging && !alias->map_as);
    CHECK(*(u32 *)video == 0xaabbccdd);
    CHECK(!gfx2_submit(0, &op, 1, a->gen, &done, &failed) && done == 1 && hw_draws == 1);
    /* Force software to verify C9's DIRECT view reaches Y3, not alias RAM. */
    be.check = gfx2_v1_shim.check;
    op.fg.v = 0x01020304;
    CHECK(!gfx2_submit(0, &op, 1, a->gen, &done, &failed));
    CHECK(*(u32 *)video == 0x01020304 && *(u32 *)alias->kva == 0x12345678);
    alias->dirty = (struct gfx2_rect){0, 0, 1, 1};
    CHECK(!gfx2_present(0, 0, 0, 0, 1, 1) && *(u32 *)video == 0x01020304 && alias->dirty.w);
    CHECK(!gfx2_sync_publish(GFX2_KIND_PC98, 0, NULL));
    CHECK(!gfx2_present(0, 0, 0, 0, 1, 1));
    CHECK(gfx2_sync_get(a, 0, &ctx, &how) == OS32_ERR_NOTFOUND);
    gfxsync_unbind(&gfxsync_ctx_pool[0]);
    config_view = config_text; config_size = sizeof(config_text);
    CHECK(!gfxsync_bind("pci:1023:9660", &target, &ctx));
    CHECK(ctx->cfg.unit == 4 && ctx->cfg.method == GFXSYNC_M_GAP && ctx->cfg.gap_ns == 80);
    CHECK(ctx->cfg.read_ns == 1500 && ctx->gap_loops == 65);
    gfxsync_profile(ctx, profile); CHECK(!kstrcmp(profile, "pci:1023:*")); gfxsync_unbind(ctx);
    struct gfxsync_cfg cfg;
    CHECK(gfxsync_cfg_lookup("pci:10de:0221", &cfg) == OS32_ERR_NOTFOUND &&
          cfg.unit == 1 && cfg.mode == GFXSYNC_SHADOW && !cfg.key[0]);
    missing = 1;
    CHECK(!gfxsync_bind("pegc", &target, &ctx) && ctx->cfg.unit == 1 && ctx->cfg.mode == GFXSYNC_SHADOW);
    gfxsync_unbind(ctx);
    kmemset(&ledger_surfaces[0], 0, sizeof(ledger_surfaces[0]));
    CHECK(!gfx2_surface_free(next->sid));
    finish();
    SAY("GREEN I3 enumeration, mode, deferred scanout free, flip, lease restore, shipped cfg/bind/caps/present");
    die(0);
}
