/* A10 production assembly, with the scanlog PCI/MMIO/allocator seams.
 * MIT nouveau c03c1a1 therm/base.c:343-357, clk/base.c:591-598:
 * monitoring lifetime is independent of drawing admission. */
#define GF4_SCANLOG_ENTRY service_scanlog_unused_start
#include "gf4_scanlog_host.c"
#undef GF4_SCANLOG_ENTRY

static void live_rejects(void)
{
    struct gf5 *g=&cint_prod.feature5;
    struct gf2_chan *ch=g->ch;
    struct gf_foundation *f=ch->f;
    struct gf4_ctx *d=g->display;
    struct gf5_thermal sample;
    for (u32 i=0;i<17;++i) {
        switch (i) {
        case 0: g->ch=0; break;
        case 1: ch->f=0; break;
        case 2: ch->state=GF2_OFF; break;
        case 3: ch->fault.latched=true; break;
        case 4: f->running=false; break;
        case 5: g->accepting=false; break;
        case 6: d->ready=false; break;
        case 7: d->accepting=false; break;
        case 8: d->changing=true; break;
        case 9: d->saved_closed=true; break;
        case 10: ++d->epoch; break;
        case 11: g->phase=GF5_SAVED; break;
        case 12: g->phase=GF5_FAULT; break;
        case 13: g->phase=GF5_SAVING; break;
        case 14: g->phase=GF5_RESTORING; break;
        case 15: g->display=0; break;
        case 16: ch->epoch=0; break;
        }
        g->plan_guard.valid=true;
        c1_clear_trace();
        assert(scanlog_f5_ops->therm_read(g,&sample)==GF5_ESTATE);
        assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
        assert(!g->plan_guard.valid);
        g->ch=ch; ch->f=f; ch->state=GF2_RUN; ch->fault.latched=false;
        f->running=true; g->accepting=true; g->display=d;
        d->ready=d->accepting=true; d->changing=d->saved_closed=false;
        ch->epoch=d->epoch=1; g->phase=GF5_RUN;
    }
    g->busy=true;
    c1_clear_trace();
    assert(scanlog_f5_ops->therm_read(g,&sample)==GF5_EBUSY);
    assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
    g->busy=false;
    const struct gf5_test_ops *borrowed=scanlog_f5_ops;
    scanlog_f5_ops=0; /* save closes publication before its read-only capture. */
    assert(borrowed->therm_read(g,&sample)==GF5_EBUSY);
    assert(borrowed->state_restore(g,0)==GF5_EBUSY);
    assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
    scanlog_f5_ops=borrowed;
    assert(!scanlog_f5_ops->therm_read(g,&sample));
    assert(!g->busy && cint_host.calls[H_READ]);
    struct gf5 other={0};
    assert(scanlog_f5_ops->therm_read(&other,&sample)==GF5_ESTATE);
}

static void thermal_lifetime(void)
{
    struct gf5 *g=&cint_prod.feature5;
    struct gf_foundation *f=g->ch->f;
    assert(!scanlog_f5_ops->service_poll(g)); /* RUN initializes sensor. */
    assert(g->fan.sensor_setup && g->fan.sensor_poll);
    svc_slot=0; /* The simulated closed display publishes no drawing ops. */
    for (u32 i=0;i<2;++i) {
        g->phase=i ? GF5_SAVED : GF5_FAULT;
        g->ch->fault.latched=true; f->running=false;
        g->ch->state=GF2_OFF;
        g->accepting=g->display->ready=g->display->accepting=false;
        g->display->saved_closed=true;
        cint_host.now=(u32)g->next_sensor;
        c1_clear_trace();
        assert(!scanlog_f5_ops->service_poll(g));
        assert(cint_host.calls[H_READ] && g->next_sensor>cint_host.now);
        cint_host.now=(u32)g->next_sensor;
        c1_clear_trace();
        scanlog_f5_ops->fan_tick(g,cint_host.now);
        assert(cint_host.calls[H_READ] && !g->busy);
    }
    void *bar=g->display->legacy->bar0_map;
    for (u32 i=0;i<4;++i) {
        switch (i) {
        case 0: g->display->legacy->bar0_map=0; break;
        case 1: f->bar0=0; break;
        case 2: f->revoked=true; break;
        case 3: ++g->display->epoch; break;
        }
        cint_host.now=(u32)g->next_sensor;
        c1_clear_trace();
        assert(!scanlog_f5_ops->service_poll(g));
        scanlog_f5_ops->fan_tick(g,cint_host.now);
        assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
        g->display->legacy->bar0_map=bar; f->bar0=bar; f->revoked=false;
        g->display->epoch=g->ch->epoch;
    }
    /* Management does not require a healthy/running channel. */
    for (u32 i=0;i<2;++i) {
        g->phase=i ? GF5_SAVED : GF5_FAULT;
        g->ch=0;
        void *allocation=0;
        assert(!g->links.alloc(g, sizeof(struct gf5_image), &allocation));
        g->image=allocation;
        g->last_rc=-903;
        assert(scanlog_f5_ops->last_rc(g)==-903);
        scanlog_f5_ops->state_free(g,allocation);
        assert(!g->image);
        assert(scanlog_f5_ops->state_restore(g,0)==GF5_EINVAL);
        g->ch=&cint_prod.chan;
    }
    g->phase=GF5_RUN; g->accepting=true; g->ch->state=GF2_RUN;
    g->ch->fault.latched=false; f->running=true;
    g->display->ready=g->display->accepting=true;
    g->display->saved_closed=false;
    g->plan_guard.valid=true;
    assert(scanlog_f5_ops->state_save(g,0)==GF5_EINVAL);
    assert(!g->plan_guard.valid && scanlog_f5_ops);
}

static void (*live_old_observe)(struct gf3_host *,const struct gf3_host_event *);
static void operation_deadlines(void)
{
    struct gf5 *g=&cint_prod.feature5;
    struct gf_vram_reservation_node node={0};
    assert(!gf_vram_reserve(g->ch,&node,g->ch->B-65536U,65536U,&node));
    struct gf5_tile tile={.base=node.value.off,.bytes=65536U,.pitch=1024U,
        .reservation_id=node.value.reservation_id,.generation=node.value.generation};
    u32 slot;
    assert(!scanlog_f5_ops->tile_find(g,&slot));
    /* nouveau nv04_gr_idle starts a fresh 2000ms wait on every operation.
     * Simulate user think time beyond the boot and previous call budgets. */
    cint_host.now=g->deadline+100000000U;
    u32 start=cint_host.now;
    assert(!scanlog_f5_ops->tile_set(g,slot,&tile));
    assert(g->deadline==start+GF2_TIMEOUT_US && g->tile_live[slot] && !g->busy);
    cint_host.now=g->deadline+100000000U;
    start=cint_host.now;
    assert(!scanlog_f5_ops->tile_clear(g,slot));
    assert(g->deadline==start+GF2_TIMEOUT_US && !g->tile_live[slot] && !g->busy);
    assert(!gf_vram_release(g->ch,&node.value));
    /* Even rejected requests enter with a current outer budget. */
    cint_host.now=g->deadline+100000000U;
    start=cint_host.now;
    assert(scanlog_f5_ops->ram_prog(g,0)==GF5_EINVAL);
    assert(g->deadline==start+GF2_TIMEOUT_US && !g->busy);
    cint_host.now=g->deadline+100000000U;
    start=cint_host.now;
    assert(scanlog_f5_ops->perf_select(g,255)==GF5_ENOTSUP); /* fixture's v0x20 */
    assert(g->deadline==start+GF2_TIMEOUT_US && !g->busy);
    scanlog_clock_error=true;
    c1_clear_trace();
    assert(scanlog_f5_ops->tile_clear(g,slot)==GF5_EIO);
    assert(g->source_rc==-901 && g->last_rc==GF5_EIO && !g->busy);
    assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
    scanlog_clock_error=false;
}

/* Kernel allocation is the only replaced service seam. Two snapshots need
 * distinct live allocations (the older scanlog allocator returns one page). */
static u8 boot_arena[16U*1024U*1024U] __attribute__((aligned(4096)));
static u32 boot_used,boot_freed;
static i32 boot_alloc(void *owner,u32 bytes,void **out)
{
    assert(owner==&cint_prod.feature5);
    u32 aligned=(bytes+15U)&~15U;
    assert(aligned<=sizeof(boot_arena)-boot_used);
    *out=boot_arena+boot_used; boot_used+=aligned; return 0;
}
static void boot_free(void *owner,void *ptr)
{
    assert(owner==&cint_prod.feature5 && ptr);
    ++boot_freed;
}
/* Extend C1's two-page map fake to cover the real display reservations. */
static u8 boot_vram[2U*1024U*1024U] __attribute__((aligned(4096)));
static int boot_map(void *owner,const struct recover_bar_extent *bar,
                    u32 bytes,volatile u8 **out)
{
    if (bar->base>=CINT_FB_PHYS && bar->base-CINT_FB_PHYS<sizeof(boot_vram)) {
        u32 off=bar->base-CINT_FB_PHYS;
        assert(bytes<=sizeof(boot_vram)-off);
        *out=boot_vram+off; return 0;
    }
    return cint_map(owner,bar,bytes,out);
}
static int boot_unmap(void *owner,volatile u8 *ptr,u32 bytes)
{
    if ((uptr)ptr>=(uptr)boot_vram && (uptr)ptr<(uptr)boot_vram+sizeof(boot_vram))
        return 0;
    return cint_unmap(owner,ptr,bytes);
}
static const struct gf_instmem_map_ops boot_map_ops={boot_map,boot_unmap};
/* Full production assembly: ROM performance selection, real RAM/core ops,
 * ordinary A/B save/restore, retained extents, public boot_target and free. */
static void boot_roundtrip(void)
{
    struct gf5 *g=&cint_prod.feature5;
    scanlog_heat_fixture();
    /* NV44A boot has no separate shader source. Keep zero available.
     * Populate all four v2x PLL records, not only scanlog's pixel PLL. */
    gf3_host_set(&cint_host,0,GF_PLL_GATE,0x3);
    gf3_host_set(&cint_host,0,0x4008,0);
    u32 first=GF_INIT_PLL_TABLE+GF_PLL_TBL_HDR_MIN;
    for (u32 i=1;i<GF_PLL_TBL_COUNT;++i)
        gf2_host_memcpy(scanlog_rom+first+i*GF_PLL_TBL_ENTRY_MIN,
                       scanlog_rom+first,GF_PLL_TBL_ENTRY_MIN);
    scanlog_put(S_PERF+2,1,1);
    scanlog_put(S_PERF+8+6,108,2);
    scanlog_put(S_PERF+8+11,216,2);
    gf5_rom_release(g);
    assert(!gf5_rom_acquire(g,scanlog_rom,sizeof(scanlog_rom),0));
    struct gf5_links old=g->links;
    const struct gf_instmem_map_ops *old_maps=g->ch->map_ops;
    struct recover_bar_extent old_bar=g->ch->bar1;
    void *old_fb=g->display->legacy->bar1_map;
    g->display->legacy->bar1_map=(void *)(uptr)CINT_FB_PHYS;
    g->ch->map_ops=&boot_map_ops;
    g->ch->map_owner=&cint_prod;
    g->ch->bar1=(struct recover_bar_extent){.base=CINT_FB_PHYS,
        .length=GF_PRODUCTION_PFB_MIN,.valid=true};
    g->links.alloc=boot_alloc; g->links.free=boot_free;
    struct gf5_clock before,after,restored;
    assert(!scanlog_f5_ops->clock_read(g,&before));
    struct gf5_image *a=0,*b=0;
    assert(!scanlog_f5_ops->state_save(g,&a) && a && a->complete);
    assert(!scanlog_f5_ops->state_restore(g,a));
    assert(!scanlog_f5_ops->perf_select(g,0));
    assert(!scanlog_f5_ops->clock_read(g,&after));
    assert(after.core_khz!=before.core_khz && after.mem_khz!=before.mem_khz);
    assert(!scanlog_f5_ops->state_save(g,&b) && b && b!=a && b->complete);
    u32 epoch=b->saved_epoch;
    assert(!scanlog_f5_ops->state_boot_target(g,b,a));
    assert(g->image==b && b->saved_epoch==epoch && a->saved_epoch!=epoch);
    assert(!gf2_host_memcmp(&b->clock_raw,&a->clock_raw,sizeof(a->clock_raw)));
    g->heat.down_active=true; g->heat.ceiling=(struct gf5_clock){1,1,1};
    assert(scanlog_f5_ops->state_restore(g,b)==GF5_EBUSY);
    g->heat.down_active=false;
    assert(!scanlog_f5_ops->state_restore(g,b));
    assert(!scanlog_f5_ops->clock_read(g,&restored));
    assert(restored.core_khz==before.core_khz && restored.shader_khz==before.shader_khz);
    assert(restored.mem_khz==before.mem_khz);
    struct gf5_clock_raw raw;
    assert(!gf5_clock_raw_save(g,&raw));
    assert(!gf2_host_memcmp(&raw,&a->clock_raw,sizeof(raw)));
    scanlog_f5_ops->state_free(g,a); scanlog_f5_ops->state_free(g,b);
    assert(boot_freed==2 && !g->image);
    g->links=old; g->ch->map_ops=old_maps; g->ch->bar1=old_bar;
    g->display->legacy->bar1_map=old_fb;
}
/* The production service's memory-only management path works in SAVED,
 * even with drawing closed. It must not pre-set busy before the body. */
static void boot_management(void)
{
    struct gf5 *g=&cint_prod.feature5;
    struct gf5_image a={.version=GF5_IMAGE_VERSION,.complete=true,
        .clock={135000,0,270000},.clock_available=true,.saved_epoch=1},
        b={.version=GF5_IMAGE_VERSION,.complete=true,.saved_epoch=2};
    g->phase=GF5_SAVED; g->image=&b;
    c1_clear_trace();
    assert(scanlog_f5_ops->state_boot_target);
    assert(!scanlog_f5_ops->state_boot_target(g,&b,&a));
    assert(b.clock_available && b.clock.mem_khz==270000 && b.saved_epoch==2);
    assert(!g->busy && g->phase==GF5_SAVED && g->image==&b);
    assert(!cint_host.calls[H_READ] && !cint_host.calls[H_WRITE]);
    g->busy=true;
    assert(scanlog_f5_ops->state_boot_target(g,&b,&a)==GF5_EBUSY);
    g->busy=false;
    const struct gf5_test_ops *borrowed=scanlog_f5_ops;
    scanlog_f5_ops=0;
    assert(borrowed->state_boot_target(g,&b,&a)==GF5_EBUSY);
    scanlog_f5_ops=borrowed;
    g->phase=GF5_RUN;
    assert(scanlog_f5_ops->state_boot_target(g,&b,&a)==GF5_ESTATE);
    g->image=0;
}
static bool live_in_observe;
static u32 live_reentries;
static void live_observe(struct gf3_host *h,const struct gf3_host_event *e)
{
    if (live_old_observe) live_old_observe(h,e);
    if (live_in_observe || e->op!=H_READ) return;
    live_in_observe=true;
    struct gf5 *g=&cint_prod.feature5;
    struct gf5_thermal sample;
    u32 reads=h->calls[H_READ],writes=h->calls[H_WRITE];
    assert(scanlog_f5_ops->therm_read(g,&sample)==GF5_EBUSY);
    assert(reads==h->calls[H_READ] && writes==h->calls[H_WRITE]);
    ++live_reentries;
    live_in_observe=false;
}
void _start(void)
{
    scanlog_map(CINT_FB_PHYS,GF_MODE_VRAM_BYTES);
    scanlog_map(SCANLOG_BAR0,0x101000U);
    scanlog_init();
    live_rejects();
    operation_deadlines();
    thermal_lifetime();
    boot_roundtrip();
    boot_management();
    live_old_observe=cint_host.observe;
    cint_host.observe=live_observe;
    struct gf5_thermal sample;
    assert(!scanlog_f5_ops->therm_read(&cint_prod.feature5,&sample));
    assert(live_reentries && !cint_prod.feature5.busy);
    cint_host.observe=live_old_observe;
    scanlog_finish();
    gf2_host_exit(0);
}
