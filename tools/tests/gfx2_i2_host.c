/* Real allocator, surfaces, submit, SW, fence and v1 bridge in one image. */
#define _start i1_start
#include "gfx2_i1_host.c"
#undef _start
#include "os32gfx2u.h"
static u32 completed, waited, emitted, kicked;
int ring3_call_from_user(void) { return 0; }
int ring3_user_range_ok(u32 p, u32 n) { return p && n; }
int ring3_user_range_writable(u32 p, u32 n) { return p && n; }
static int choose(void *c, const struct gfx2_exec *e)
{ (void)c; return e->op->op == GFX2_OP_FILL ? 0 : OS32_ERR_NOSYS; }
static int draw(void *c, const struct gfx2_exec *e, u32 f)
{
    struct gfx2_rect touched;
    (void)c; CHECK(f && !gfx2_sw_fill(e, &touched)); emitted++;
    return 0;
}
static int mark(void *c, u32 f, u32 dl)
{ (void)c; (void)dl; CHECK(f == emitted); return 0; }
static int progress(void *c, u32 *done, u32 *fail)
{ (void)c; *done = completed; *fail = 0; return 0; }
static int wait(void *c, u32 f, u32 dl)
{ (void)c; CHECK(dl == 1000000); waited++; completed = f; return 0; }
static int kick(void *c) { (void)c; kicked++; return 0; }
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
    CHECK(!gfx2_adapter_register(&v1, GFX2_KIND_PC98, GFX2_AF_ACTIVE));
    struct gfx2_adapter *a = gfx2_adapter_get(0);
    begin();
    be = gfx2_v1_shim; be.check = choose; be.emit = draw; be.fence_emit = mark;
    be.fence_state = progress; be.fence_wait = wait; be.kick = kick; a->be = &be;
    struct gfx2_surf *s = surface(GFX2_POOL_SYSMEM, 4);
    u32 packed, done, fence, out[256];
    CHECK(!gfx2_color_pack(GFX2_FMT_XRGB8888, 0xff123456, 0, &packed));
    GFX2_Op ops[3] = {
        {.op=GFX2_OP_FILL, .dst=s->sid, .dw=64, .dh=4, .rop=GFX2_ROP_PATCOPY, .fg={.v=packed}},
        {.op=GFX2_OP_LINE, .dst=s->sid, .dx=0, .dy=0, .sx=4, .sy=0,
         .rop=GFX2_ROP_PATCOPY, .fg={.v=0xffabcdef}},
        {.op=GFX2_OP_FILL, .dst=s->sid, .dx=4, .dy=0, .dw=1, .dh=1,
         .rop=GFX2_ROP_PATCOPY, .fg={.v=packed}}
    };
    CHECK(!gfx2_submit(0, ops, 3, a->gen, &done, &fence));
    CHECK(done == 3 && fence == 2 && emitted == 2 && waited == 1 && kicked == 1);
    GFX2_Op rb = {.op=GFX2_OP_READBACK, .src=s->sid, .sw=64, .sh=4,
                  .pitch=256, .data=(u32)(uptr)out};
    CHECK(!gfx2_submit(0, &rb, 1, a->gen, &done, &fence) && done == 1);
    CHECK(waited == 2);
    for (u32 i=0; i<256; i++) CHECK(out[i] == (i<4 ? 0xffabcdefu : packed));
    /* The bridge must wait for the same real fence as a v1 present. */
    ledger_surfaces[0] = (struct ledger_surface){.backend=GFX2_KIND_PC98,
        .role=LEDGER_ROLE_CLIENT, .first=s->sys_pfn, .npages=s->sys_n,
        .width=64, .height=4, .pitch=s->pitch, .format=s->fmt, .backing=LEDGER_SB_RAM};
    v1_bridge_rebind();
    u32 alias_sid; int rc;
    CHECK(!gfx2_screen_surface(0, 0, GFX2_ROLE_CLIENT, &alias_sid));
    struct gfx2_surf *alias = gfx2_surf_get(alias_sid, as.owner, &rc);
    CHECK(alias && !rc);
    alias->last_write = a->fence.issued = 3;
    CHECK(!v1_bridge_begin(GFX2_ROLE_CLIENT, GFX2_CPU_W));
    CHECK(waited == 3 && completed == 3);
    v1_bridge_end(GFX2_ROLE_CLIENT, GFX2_CPU_W, 0);
    v1_bridge_rebind();
    GFX2_SurfaceDesc desc;
    CHECK(gfx2_surface_desc(alias_sid, &desc) == OS32_ERR_STALE);
    ledger_surfaces[0].npages=0; v1_bridge_rebind();
    CHECK(!gfx2_surface_free(s->sid));
    /* R3: real submit writes three discontiguous GART pages through the VA. */
    be = gfx2_v1_shim; be.alloc = alloc;
    be.gart_bind = gart_bind; be.gart_unbind = gart_unbind;
    a->be = &be;
    u32 holes[6];
    for (u32 i = 0; i < 6; i++)
        CHECK(pgalloc_alloc_n_owner(as.owner, 1, 0, DMA_PHYS_LIMIT / PAGE_SIZE,
                                   LEDGER_BOTTOM_UP, &holes[i]));
    for (u32 i = 0; i < 6; i += 2) CHECK(pgalloc_free_n_owner(as.owner, holes[i], 1));
    s = surface(GFX2_POOL_GART, 48);
    const u32 *pfns; u32 npages;
    CHECK(!gfx2_gart_pfns(s, &pfns, &npages) && npages == 3);
    for (u32 i = 0; i < 3; i++) CHECK(pfns[i] == holes[i * 2]);
    for (u32 i = 1; i < 6; i += 2) CHECK(pgalloc_free_n_owner(as.owner, holes[i], 1));
    ops[0] = (GFX2_Op){.op=GFX2_OP_FILL, .dst=s->sid, .dw=64, .dh=48,
                       .rop=GFX2_ROP_PATCOPY, .fg={.v=packed}};
    CHECK(gfx2_submit(0, ops, 1, a->gen, &done, &fence) == OS32_ERR_NOSYS && !done);
    paging_load_cr3(as.pd_phys);
    CHECK(!gfx2_submit(0, ops, 1, a->gen, &done, &fence) && done == 1);
    static u32 gart_pixels[64 * 48];
    rb = (GFX2_Op){.op=GFX2_OP_READBACK, .src=s->sid, .sw=64, .sh=48,
                  .pitch=256, .data=(u32)(uptr)gart_pixels};
    CHECK(!gfx2_submit(0, &rb, 1, a->gen, &done, &fence) && done == 1);
    for (u32 i = 0; i < 64 * 48; i++) CHECK(gart_pixels[i] == packed);
    paging_load_cr3(paging_kernel_pd_phys());
    for (u32 p = 0; p < npages; p++)
        for (u32 i = 0; i < PAGE_SIZE / sizeof(u32); i++)
            CHECK(((u32 *)P2V(pfns[p] * PAGE_SIZE))[i] == packed);
    CHECK(!gfx2_surface_free(s->sid)); finish();
    SAY("GREEN I2 mixed HW/SW/HW, real fence waits, pixels, U1, v1 bridge, stale alias and GART submit");
    die(0);
}
