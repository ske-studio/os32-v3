/* G1 UNIT=12 integration, also included by gf_accel_c2_host.c.
 * Compile either test alone, plus the real B4 translation unit, with
 * kernel_includes.py flags, -m32 -nostdlib -ffunction-sections
 * -fdata-sections -Wl,--gc-sections. No libc, generated images or refs/.
 * GC drops unused backend/video-start/production-paging entry points;
 * it never replaces a dependency of the lifecycle exercised below.
 * F1/F2, B0-B3/B5-B10, V0, C0, C1 and C2 are included verbatim. Only
 * colliding private identifiers and privileged host IRQ primitives differ.
 * B4 must be linked as a real product (absent from the supplied base).
 * Tests check ownership/protocol, not GPU-rendered pixels. */
#include "../../include/io.h"
#define irq_save() 0U
#define irq_restore(flags) ((void)(flags))
#define GF3_HOST_UNIT 12
#include "gf_accel_host.h"
#include "../../hwdev/gf/production.h"
#include "../../hwdev/gf/accel_service.h"

#include "../../hwdev/gf/master.c"
#include "../../hwdev/gf/mc.c"
#include "../../hwdev/gf/instmem.c"
#include "../../hwdev/gf/instmem_map.c"
#include "../../hwdev/gf/engine_stop.c"
#define rd cint_irq_rd
#define wr cint_irq_wr
#define drain cint_irq_drain
#include "../../hwdev/gf/irq.c"
#undef drain
#undef wr
#undef rd
#define release_instances cint_foundation_release_instances
#include "../../hwdev/gf/foundation.c"
#undef release_instances
#include "../../drivers/video/geforce/geforce_tick.c"

#define rd cint_fifo_rd
#define wr cint_fifo_wr
#define stop cint_fifo_stop
#include "../../hwdev/gf/fifo.c"
#undef stop
#undef wr
#undef rd
#include "../../hwdev/gf/push.c"
#include "../../hwdev/gf/dmaobj.c"
#include "../../hwdev/gf/ramht.c"
#include "../../hwdev/gf/grctx.c"
#define rd cint_gr_rd
#define wr cint_gr_wr
#include "../../hwdev/gf/gr.c"
#undef wr
#undef rd
#include "../../hwdev/gf/fence.c"
#define rd cint_fault_rd
#define wr cint_fault_wr
#include "../../hwdev/gf/fault.c"
#undef wr
#undef rd
#include "../../hwdev/gf/command_platform.c"
#define release_instances cint_command_release_instances
#include "../../hwdev/gf/command.c"
#undef release_instances

#include "../../hwdev/gf/vram_view.c"
#include "../../hwdev/gf/accel_objects.c"
#include "../../hwdev/gf/accel_surface.c"
#include "../../hwdev/gf/accel_rop.c"
#include "../../hwdev/gf/accel_fill.c"
#include "../../hwdev/gf/accel_mono.c"
#include "../../hwdev/gf/accel_ifc.c"
#include "../../hwdev/gf/accel_m2mf.c"
#include "../../hwdev/gf/accel_transfer.c"
#include "../../hwdev/gf/accel_scale.c"
#include "../../drivers/video/geforce/gf_present_m2mf.c"
#include "../../drivers/video/geforce/gf_hook.c"

/* Use the real C0a getter and its borrowed metadata, not a custom getter. */
#ifndef OS32_GEFORCE_MODULE
#define OS32_GEFORCE_MODULE
#endif
#include "../../drivers/video/geforce/backend_geforce.c"
#undef gfx_backend_geforce
#undef gfx_current_height
#undef gfx_flip_enabled
#undef gfx_display_page
#ifndef GF3_SERVICE_SOURCE
#define GF3_SERVICE_SOURCE "../../hwdev/gf/service.c"
#endif
#include GF3_SERVICE_SOURCE
#if !defined(GF4_REAL_DISPLAY)
/* Legacy F3-only hosts never install display4. Full F4 integration links
 * the actual service; an accidental F4 dispatch here must fail visibly. */
struct gf4_ctx;
i32 gf4_service_start(struct gf4_ctx *ctx,u32 dl)
{ (void)ctx;(void)dl;assert(false);return GF3_ESTATE; }
#endif
#ifndef GF3_QUIESCE_SOURCE
#define GF3_QUIESCE_SOURCE "../../hwdev/gf/display/quiesce.c"
#endif
#include GF3_QUIESCE_SOURCE

/* Capacities are fixture budgets, not device limits. VRAM detection is the
 * smallest supported size (production.h); VS=0 reserves 1536 KiB:
 * (16 KiB * 32 + 1 MiB), instmem.c / nv40.c:183-206.
 * Host backing only covers the inst aperture, push and two independent CPU
 * windows. Their addresses are never used as device physical addresses. */
#define CINT_FB_PHYS 0x30000000U
#define CINT_IMEM_PHYS 0x40000000U
#define CINT_IMEM_BYTES (1536U * 1024U)
#define CINT_SCAN_OFF 123U
#define CINT_SCAN_PITCH 641U
#define CINT_TEST_PITCH 704U
#define CINT_SCAN_HEIGHT 480U
#define CINT_TEST_OFF (76U * MEM_PAGE_SIZE) /* ceil((123+641*480)/4096) */
#define CINT_WINDOW_OFF CINT_TEST_OFF
#define CINT_IO_FAILURE (-901)
#define CINT_RESTORE_FAILURE (-902)
#define CINT_EVENTS 16384U /* GR program upload + start/recover/stop traces */
static struct gf3_host cint_host;
static struct gf3_host_event cint_events[CINT_EVENTS];
static struct gf3_host_reg cint_regs[512], cint_writes[4096];
#ifdef GF3_CINT_WIRE
#include "gf_ownerless_body.inc"
#define cint_prod production
#else
static struct gf_production cint_prod;
#endif
static const struct gf3_test_ops *cint_slot;
static struct gf3_service *cint_service;
static u8 cint_imem[CINT_IMEM_BYTES] __attribute__((aligned(MEM_PAGE_SIZE)));
static u32 cint_push[GF2_PUSH_WORDS];
static u8 cint_windows[2][MEM_PAGE_SIZE];
static struct gf_vram_view cint_view[2];
static volatile u8 *cint_loan[2];
static u32 cint_close_calls[2], cint_maps[4], cint_unmaps[4];
enum { CINT_MAP_IMEM, CINT_MAP_PUSH, CINT_MAP_VIEW0, CINT_MAP_VIEW1 };
static int cint_map_fail, cint_unmap_fail, cint_restore_rc;
static u32 cint_restore_calls, cint_owner_stops, cint_dma_allocs, cint_dma_frees;
static u32 cint_consumed, cint_reference, cint_references, cint_stall_reference;
static u32 cint_pci_writes, cint_pci_fail_at;
static u16 cint_pci_command;
static bool cint_irq_registered, cint_dma_live, cint_expect_closed;
static bool cint_limit_new_imem, cint_restore_seen;
static bool cint_saw_unfinished, cint_fail_stop_after_put, cint_recover_timeout;
static u32 cint_old_epoch, cint_recover_delay;

static inline u32 cint_used_count(void)
{
    u32 n = 0;
    for (struct gf_inst *p = cint_prod.foundation.imem.used; p; p = p->next) {
        assert(++n <= GF3_OBJECT_COUNT + 16U);
        assert(p->size && p->off + p->size <= cint_prod.foundation.imem.aperture_bytes);
    }
    return n;
}
static inline bool cint_inst_live(const struct gf_inst *inst)
{
    for (struct gf_inst *p = cint_prod.foundation.imem.used; p; p = p->next)
        if (p == inst) return true;
    return false;
}
static inline u32 cint_used_end(void)
{
    u32 end = 0;
    for (struct gf_inst *p = cint_prod.foundation.imem.used; p; p = p->next)
        if (end < p->off + p->size) end = p->off + p->size;
    return end;
}
static inline void cint_closed(void)
{
    assert(!cint_slot && !hook_active);
    assert(geforce_fill_rect(0, 0, 1, 1, 1) == OS32_ERR_NOSYS);
}

/* Fake device consumes the actual published ring. GET advances independently
 * of REFERENCE: only a submitted subc0/0050 packet completes a fence. No
 * gf2_emit/fence/stop or allocator is stubbed; other methods are only skipped.
 * Encoding: nouveau_dma.h:82-96; nv10_fence.c:33-41 (design sections 3/6). */
static void cint_consume(u32 raw_put)
{
    struct gf2_chan *ch = &cint_prod.chan;
    assert(ch->mapped && raw_put >= ch->B && !(raw_put & 3U));
    u32 put = (raw_put - ch->B) / sizeof(u32);
    assert(put <= ch->max);
    if (ch->put == 0) cint_consumed = 0; /* freshly initialized F2 ring */
    u32 budget = GF2_PUSH_WORDS;
    while (cint_consumed != put) {
        assert(budget-- && cint_consumed < GF2_PUSH_WORDS);
        u32 head = cint_push[cint_consumed++];
        if (!head) continue;
        if (head & GF2_PUSH_JUMP) {
            u32 address = head & ~GF2_PUSH_JUMP;
            assert(address >= ch->B && !(address & 3U));
            cint_consumed = (address - ch->B) / sizeof(u32);
            continue;
        }
        u32 count = (head >> GF2_METHOD_COUNT_SHIFT) & GF2_METHOD_COUNT_MAX;
        u32 subc = (head >> GF2_METHOD_SUBCHANNEL_SHIFT) & GF2_SUBCHANNEL_MAX;
        u32 method = head & GF2_METHOD_MAX;
        assert(count && count <= GF2_PUSH_WORDS - cint_consumed);
        if (!subc && method == GF2_REFERENCE_METHOD) {
            assert(count == 1);
            ++cint_references;
            if (cint_references != cint_stall_reference)
                cint_reference = cint_push[cint_consumed];
        }
        cint_consumed += count;
    }
    gf3_host_set(&cint_host, 0, GF2_USER_BASE + GF2_USER_GET, raw_put);
    gf3_host_set(&cint_host, 0, GF2_USER_BASE + GF2_USER_REFERENCE, cint_reference);
}
static void cint_observe(struct gf3_host *h, const struct gf3_host_event *e)
{
    assert(h == &cint_host);
    if (cint_expect_closed) cint_closed();
    if (!cint_slot) assert(!hook_active); /* neither is published during init */
    else {
        assert(cint_prod.accel.ready && hook_active);
        assert(cint_slot == &cint_service->ops);
        assert(cint_service->scan_node.value.owner && cint_service->test_node.value.owner);
    }
    if (!e->rc && e->op == H_WRITE && e->a[1] == GF2_USER_BASE + GF2_USER_PUT)
        cint_consume(e->a[3]);
    /* IRQ source status is W1C (real irq.c), not ordinary register storage. */
    if (!e->rc && e->op == H_WRITE) {
        static const u32 status[] = {GF_IRQ_FIFO_STATUS, GF_IRQ_GRAPH_STATUS,
            GF_IRQ_CRTC_STATUS, GF_IRQ_CRTC_STATUS + GF_IRQ_HEAD_STRIDE,
            0x008100U, 0x009100U, 0x001100U, 0x00b100U}; /* irq.c unused_status */
        for (unsigned i = 0; i < sizeof(status) / sizeof(status[0]); ++i)
            if (e->a[1] == status[i]) gf3_host_set(h, 0, status[i], 0);
    }
    if (e->rc && e->op == H_WRITE && e->a[1] == GF2_USER_BASE + GF2_USER_PUT && cint_fail_stop_after_put) {
        /* The injected publication failure is followed by a persistent bus
         * failure, so real F2 cannot establish quiescence or release inst. */
        gf3_host_fail_at(h, H_WRITE, 0, CINT_IO_FAILURE);
        cint_fail_stop_after_put = false;
    }
    if (!e->rc && e->op == H_READ && e->a[1] == GF2_USER_BASE + GF2_USER_REFERENCE &&
        *(const u32 *)e->p[1] != cint_prod.chan.seq) {
        assert(cint_consumed == cint_prod.chan.put);
        cint_saw_unfinished = true;
        if (cint_recover_timeout) {
            cint_host.now += GF3_TIMEOUT_US;
            cint_recover_timeout = false;
        }
    }
    if (e->op == H_READ && e->a[1] == GF2_USER_BASE + GF2_USER_REFERENCE &&
        cint_limit_new_imem && cint_prod.chan.epoch != cint_old_epoch &&
        cint_prod.chan.state == GF2_INIT) {
        /* Fault injection at the next real allocation, after new F2 exists. */
        cint_prod.foundation.imem.aperture_bytes = cint_used_end();
        cint_limit_new_imem = false;
    }
    if (cint_restore_seen && e->op == H_WRITE) {
        assert(cint_prod.foundation.revoked ||
            (!cint_prod.foundation.imem.used && cint_prod.chan.state == GF2_OFF));
        assert(cint_service->scan_node.value.owner); /* release follows all rows */
    }
}

/* Host OS adapters below supply the external environment of real F1/F2.
 * They implement PCI storage, resident IRQ/tick registration and borrowed
 * map/DMA callbacks only, never GF lifecycle/object/VRAM/dispatch functions. */
u16 pci_cfg_read16(u32 bus, u32 dev, u32 fn, u32 reg)
{
    assert(bus == RECOVER_GF_BUS && dev == RECOVER_GF_DEV && fn == RECOVER_GF_FN);
    assert(reg == PCI_CFG_COMMAND || reg == PCI_CFG_CACHE_LINE);
    return reg == PCI_CFG_COMMAND ? cint_pci_command : 0;
}
u8 pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 reg)
{
    assert(bus == RECOVER_GF_BUS && dev == RECOVER_GF_DEV && fn == RECOVER_GF_FN);
    assert(reg == PCI_CFG_LATENCY_TIMER || reg == PCI_CFG_INT_LINE);
    return reg == PCI_CFG_INT_LINE ? 6U : 64U;
}
int pci_cfg_write16(u32 bus, u32 dev, u32 fn, u32 reg, u16 value)
{
    assert(bus == RECOVER_GF_BUS && dev == RECOVER_GF_DEV && fn == RECOVER_GF_FN);
    assert(reg == PCI_CFG_COMMAND);
    if (++cint_pci_writes == cint_pci_fail_at) return CINT_IO_FAILURE;
    cint_pci_command = value;
    return 0;
}
int irq_register(unsigned line, irq_handler_fn fn, void *arg, unsigned flags)
{
    assert(line == 6 && fn == gf_irq_handler && arg == &cint_prod.foundation);
    assert(flags == IRQ_F_SHARED && !cint_irq_registered);
    cint_irq_registered = true;
    return 0;
}
int irq_unregister(unsigned line, irq_handler_fn fn, void *arg)
{
    assert(line == 6 && fn == gf_irq_handler && arg == &cint_prod.foundation);
    assert(cint_irq_registered);
    cint_irq_registered = false;
    return 0;
}
int tick_register(unsigned slot, tick_fn fn)
{
    assert(slot == PC98_TICK_GF && fn == geforce_tick);
    return 0;
}
#ifndef GF_OBJWAIT_SERIAL
int serial_gate_active(void) { return 1; }
void serial_puts_polled(const char *s) { (void)s; }
#endif

static int cint_map(void *owner, const struct recover_bar_extent *bar,
                    u32 bytes, volatile u8 **out)
{
    assert(owner == &cint_prod && bar->valid && bytes <= bar->length);
    u32 which;
    volatile u8 *ptr;
    if (bar->base == CINT_IMEM_PHYS) {
        which = CINT_MAP_IMEM; ptr = cint_imem;
        assert(bytes == CINT_IMEM_BYTES);
        cint_host.now += cint_recover_delay;
        cint_recover_delay = 0;
    } else if (bytes == GF2_PUSH_BYTES) {
        which = CINT_MAP_PUSH; ptr = (volatile u8 *)cint_push;
        assert(bar->base == CINT_FB_PHYS + cint_prod.chan.B);
    } else {
        assert(bytes == MEM_PAGE_SIZE && bar->base >= CINT_FB_PHYS + CINT_WINDOW_OFF);
        u32 i = (bar->base - CINT_FB_PHYS - CINT_WINDOW_OFF) / MEM_PAGE_SIZE;
        assert(i < 2 && bar->base == CINT_FB_PHYS + CINT_WINDOW_OFF + i * MEM_PAGE_SIZE);
        which = CINT_MAP_VIEW0 + i; ptr = cint_windows[i];
    }
    ++cint_maps[which];
    if (cint_map_fail == (int)which) return CINT_IO_FAILURE;
    if (which == CINT_MAP_PUSH) cint_consumed = cint_reference = 0;
    *out = ptr;
    return 0;
}
static int cint_unmap(void *owner, volatile u8 *ptr, u32 bytes)
{
    assert(owner == &cint_prod);
    u32 which;
    if (ptr == cint_imem) { which = CINT_MAP_IMEM; assert(bytes == CINT_IMEM_BYTES); }
    else if (ptr == (volatile u8 *)cint_push) { which = CINT_MAP_PUSH; assert(bytes == GF2_PUSH_BYTES); }
    else {
        assert(ptr == cint_windows[0] || ptr == cint_windows[1]);
        which = ptr == cint_windows[0] ? CINT_MAP_VIEW0 : CINT_MAP_VIEW1;
        assert(bytes == MEM_PAGE_SIZE);
    }
    ++cint_unmaps[which];
    return cint_unmap_fail == (int)which ? CINT_IO_FAILURE : 0;
}
static const struct gf_instmem_map_ops cint_map_ops = {cint_map, cint_unmap};
static int cint_dma_alloc(u32 bytes, u32 align, u32 mask, struct gf2_dma_buf *out)
{
    assert(!cint_dma_live && bytes == GF2_STAGING_BYTES && align == MEM_PAGE_SIZE && mask == U32_MAX);
    /* Synthetic PCI address; never convert a host VA into a device address. */
    *out = (struct gf2_dma_buf){.pa = 0x01000000U, .size = bytes, .va = cint_host.staging};
    cint_dma_live = true; ++cint_dma_allocs;
    return 0;
}
static int cint_dma_free(struct gf2_dma_buf *buf)
{
    assert(cint_dma_live && buf->va == cint_host.staging && buf->size == GF2_STAGING_BYTES);
    cint_dma_live = false; ++cint_dma_frees;
    return 0;
}
static void cint_dma_leak(struct gf2_dma_buf *buf)
{ (void)buf; assert(0); }
static const struct gf2_dma_ops cint_dma_ops = {cint_dma_alloc, cint_dma_free, cint_dma_leak};
static struct gf2_chan *cint_command_get(struct gf_display_context *c, struct gf_foundation **f)
{
    assert(c == &cint_prod.display);
    *f = &cint_prod.foundation;
    return &cint_prod.chan;
}
static int cint_foundation_stop(struct gf_display_context *c)
{
    assert(c == &cint_prod.display);
    assert(cint_prod.chan.state == GF2_OFF || cint_prod.foundation.revoked);
    cint_closed(); ++cint_owner_stops;
    int rc = gf_foundation_stop(&cint_prod.foundation);
    if (rc) c->last_rc = rc;
    return rc ? -1 : 0;
}
static const struct gf_display_owner cint_display_owner = {
    .foundation_stop = cint_foundation_stop, .command_get = cint_command_get
};
static int cint_restore(void *owner, struct gf_init_context *cleanup)
{
    assert(owner == &cint_host && cleanup->io.opaque == owner);
    cint_closed();
    if (cint_prod.foundation.revoked) {
        /* CPU cleanup is still attempted, even when the bus lease no longer
         * permits restoration of device registers. Hardware ownership stays. */
        assert(cint_service->scan_node.value.owner);
        ++cint_restore_calls; cint_restore_seen = true;
        return CINT_IO_FAILURE;
    }
    assert(cint_prod.chan.state == GF2_OFF && !cint_prod.accel.ready);
    assert(!cint_prod.foundation.imem.used && cint_owner_stops);
    assert(cint_service->scan_node.value.owner);
    ++cint_restore_calls; cint_restore_seen = true;
    return cint_restore_rc;
}
static i32 cint_close_window(void *owner, struct gf_vram_view *view)
{
    unsigned i = owner == &cint_loan[0] ? 0U : 1U;
    assert(owner == &cint_loan[i] && view == &cint_view[i]);
    cint_closed(); ++cint_close_calls[i];
    assert(cint_loan[i] == cint_windows[i]);
    /* This is the owner's CPU-use/posted-write boundary, before unmap. */
    __asm__ volatile("" ::: "memory");
    (void)cint_loan[i][0];
    int rc = gf_vram_view_unmap(view);
    if (!rc) cint_loan[i] = 0;
    return rc;
}
static inline void cint_open_windows(void)
{
    for (unsigned i = 0; i < 2; ++i) {
        cint_view[i].close = cint_close_window;
        cint_view[i].close_owner = &cint_loan[i];
        assert(!gf_vram_view_map(&cint_view[i], &cint_prod.chan,
            CINT_WINDOW_OFF + i * MEM_PAGE_SIZE, MEM_PAGE_SIZE, &cint_loan[i]));
        assert(cint_view[i].close == cint_close_window && cint_view[i].close_owner == &cint_loan[i]);
    }
}
static inline void cint_trace_clear(void)
{
    gf3_host_trace_clear(&cint_host);
    gf2_host_memset(cint_host.fail_at, 0, sizeof(cint_host.fail_at));
    gf2_host_memset(cint_host.fail_rc, 0, sizeof(cint_host.fail_rc));
    cint_restore_calls = cint_owner_stops = 0;
    cint_restore_seen = false;
}
static inline void cint_owned_reservations(void)
{
    struct gf_vram_reservation key;
    struct gf_vram_reservation_node *nodes[] = {&cint_service->scan_node, &cint_service->test_node};
    for (unsigned i = 0; i < 2; ++i) {
        const struct gf_vram_reservation *v = &nodes[i]->value;
        assert(v->owner == nodes[i]);
        assert(!gf_vram_match(&cint_prod.chan, nodes[i], v->reservation_id,
            v->generation, v->off, v->bytes, &key));
    }
}
static inline void cint_no_resources(void)
{
    cint_closed();
    assert(cint_prod.chan.state == GF2_OFF && !cint_prod.accel.ready);
    assert(!cint_prod.foundation.imem.used && !cint_prod.foundation.mapped);
    assert(!cint_prod.chan.mapped && !cint_prod.chan.staging_live && !cint_dma_live);
    assert(!cint_irq_registered && !tick_poll && !cint_prod.foundation.bm);
    assert(!views && !registry.head);
    assert(!cint_service->scan_node.value.owner && !cint_service->test_node.value.owner);
    for (u32 i = 0; i < sizeof(cint_prod.chan.bitmap); ++i) assert(!cint_prod.chan.bitmap[i]);
    for (unsigned i = 0; i < 2; ++i) assert(!cint_view[i].mapped && !cint_loan[i]);
    assert(cint_dma_allocs == cint_dma_frees);
}
static inline void cint_reset_cold(void)
{
    /* Previous case must clean up via the product, never erase live lists.
     * Preserve registry/node ID history and the single service binding. */
    if (cint_service) cint_no_resources();
    gf2_host_memset(&cint_prod, 0, sizeof(cint_prod));
    gf3_host_init(&cint_host);
    cint_host.events = cint_events; cint_host.cevents = CINT_EVENTS;
    cint_host.regs = cint_regs; cint_host.cregs = sizeof(cint_regs) / sizeof(cint_regs[0]);
    cint_host.writes = cint_writes; cint_host.cwrites = sizeof(cint_writes) / sizeof(cint_writes[0]);
    cint_host.observe = cint_observe;
    cint_host.now = 100;
    cint_recover_delay = 0;
    cint_recover_timeout = false;
    cint_map_fail = cint_unmap_fail = -1;
    cint_restore_rc = 0; cint_restore_seen = cint_expect_closed = false;
    cint_limit_new_imem = false;
    cint_saw_unfinished = cint_fail_stop_after_put = false;
    cint_stall_reference = cint_references = 0;
    cint_pci_writes = cint_pci_fail_at = 0;
    gf2_host_memset(cint_close_calls, 0, sizeof(cint_close_calls));
    gf2_host_memset(cint_maps, 0, sizeof(cint_maps));
    gf2_host_memset(cint_unmaps, 0, sizeof(cint_unmaps));
    cint_prod.foundation.io = cint_host.f.io;
    cint_prod.foundation.map_ops = &cint_map_ops;
    cint_prod.foundation.map_owner = &cint_prod;
    cint_prod.foundation.instmem_bar = (struct recover_bar_extent){
        .base = CINT_IMEM_PHYS, .length = sizeof(cint_imem), .valid = true};
    cint_prod.chan.bar1 = (struct recover_bar_extent){
        .base = CINT_FB_PHYS, .length = GF_PRODUCTION_PFB_MIN, .valid = true};
    cint_prod.chan.map_ops = &cint_map_ops; cint_prod.chan.map_owner = &cint_prod;
    cint_prod.chan.dma_ops = &cint_dma_ops;
    cint_prod.display.owner = &cint_display_owner;
    cint_prod.display.io = cint_host.f.io;
    cint_prod.display.io.restore = cint_restore;
    cint_prod.display.engaged = cint_prod.display.active = cint_prod.display.reusable = true;
    framebuffer = (struct video_fb){.phys = CINT_FB_PHYS + CINT_SCAN_OFF,
        .bytes = CINT_SCAN_PITCH * CINT_SCAN_HEIGHT, .pitch = CINT_SCAN_PITCH,
        .format = GFX_FMT_PACKED8};
    gf3_host_set(&cint_host, 0, GF_PRODUCTION_PFB_AMOUNT, GF_PRODUCTION_PFB_MIN);
    gf3_host_set(&cint_host, 0, GR_UNITS, 0);
#ifndef GF3_CINT_WIRE
    assert(!gf3_service_bind(&cint_prod, &cint_slot));
#endif
    cint_service = gf3_service_get(&cint_prod.chan);
#ifndef GF3_CINT_WIRE
    assert(cint_service);
#endif
    if (cint_service)
        cint_service->scan_pitch = cint_service->scan_width = cint_service->scan_height = cint_service->scan_phys = 0;
    cint_trace_clear();
}
static inline void cint_reset(void)
{
    cint_reset_cold();
    assert(!gf_foundation_start(&cint_prod.foundation));
    assert(!gf2_start(&cint_prod.chan, &cint_prod.foundation));
    assert(cint_prod.chan.state == GF2_RUN && cint_prod.chan.mapped && cint_dma_live);
    assert(cint_used_count() == 8U); /* four fixed + three DMA + GR context */
    cint_trace_clear();
}
static inline u32 cint_deadline(void) { return cint_host.now + GF3_TIMEOUT_US; }
static inline void cint_start(void)
{
    assert(!gf3_service_start(cint_service, cint_deadline()));
    assert(cint_slot == &cint_service->ops && cint_slot->ctx == &cint_prod.accel);
    assert(cint_slot->test_surface == &cint_service->test && cint_prod.accel.ready);
    assert(hook_active && hook_ctx == &cint_prod.accel);
    assert(!gf2_host_memcmp(&hook_scan, &cint_service->scan, sizeof(hook_scan)));
    cint_owned_reservations();
    assert(cint_used_count() == 8U + 2U + GF3_OBJECT_COUNT); /* storage + notify DMA + GRs */
    assert(cint_inst_live(&cint_prod.accel.notify_storage));
    assert(cint_inst_live(&cint_prod.accel.notify.inst));
    for (unsigned i = 0; i < GF3_OBJECT_COUNT; ++i)
        assert(cint_inst_live(&cint_prod.accel.obj[i].inst));
    assert(cint_service->scan.off == CINT_SCAN_OFF && cint_service->scan.pitch == CINT_SCAN_PITCH);
    assert(cint_service->scan.width == 640 && cint_service->scan.height == CINT_SCAN_HEIGHT);
    assert(cint_service->test.off == CINT_TEST_OFF && cint_service->test.pitch == CINT_TEST_PITCH);
    assert(cint_service->test.bytes == (cint_prod.chan.B & ~(MEM_PAGE_SIZE - 1U)) - CINT_TEST_OFF);
    assert(cint_service->test.height == CINT_SCAN_HEIGHT);
}
static inline void cint_stop_display(void)
{
    cint_expect_closed = true;
    assert(!gf_display_stop(&cint_prod.display));
    cint_no_resources();
}

#ifndef GF3_CINT_C2
static void cint_start_and_diagnostic_stop(void)
{
    cint_reset(); cint_start(); cint_open_windows();
    u32 id = cint_service->scan.reservation_id;
    cint_trace_clear(); cint_expect_closed = true;
    assert(!gf3_service_stop(cint_service));
    cint_closed(); cint_owned_reservations();
    assert(cint_prod.display.engaged && !cint_restore_calls && !cint_owner_stops);
    assert(cint_prod.chan.state == GF2_OFF && !cint_prod.foundation.imem.used);
    assert(!cint_prod.accel.ready && !cint_prod.accel.epoch && !cint_prod.accel.notify_storage.size);
    assert(cint_close_calls[0] == 1 && cint_close_calls[1] == 1 && !views);
    /* Diagnostic START reuses the saved scan and the same live keys. */
    framebuffer = (struct video_fb){0};
    assert(!gf_foundation_start(&cint_prod.foundation));
    assert(!gf2_start(&cint_prod.chan, &cint_prod.foundation));
    cint_expect_closed = false; cint_start();
    assert(cint_service->scan.reservation_id == id);
    cint_stop_display();
}
static void cint_real_dispatch(void)
{
    cint_reset(); cint_start();
    struct gf3_surface a = *cint_slot->test_surface;
    struct gf3_rect r = {0, 0, 8, 2};
    u8 data[64] = {0};
    struct gf3_ram ram = {data, sizeof(data), 16};
    assert(cint_slot->fill == gf3_fill && cint_slot->copy == gf3_copy && cint_slot->mono == gf3_mono);
    assert(cint_slot->map == gf_vram_view_map && cint_slot->unmap == gf_vram_view_unmap);
    /* Reacquire dispatch/ctx at each synchronous call; never cache the table. */
    assert(!cint_slot->fill(cint_slot->ctx, &a, r, 3, 3, 0xff, cint_deadline()));
    assert(!cint_slot->copy(cint_slot->ctx, &a, &a, r, 16, 2, 3, 0xff, cint_deadline()));
    assert(!cint_slot->mono(cint_slot->ctx, &a, r, &ram, 1, 0, r, cint_deadline()));
    a.fmt = GF3_RGB565; a.width = 8; a.height = 2; a.bytes = a.pitch + 16U;
    assert(!cint_slot->ifc(cint_slot->ctx, &a, r, &ram, cint_deadline()));
    assert(!cint_slot->upload(cint_slot->ctx, &a, r, &ram, false, cint_deadline()));
    assert(!cint_slot->download(cint_slot->ctx, &a, r, &ram, cint_deadline()));
    assert(!cint_slot->present_m2mf(cint_slot->ctx, &a, r, &ram, cint_deadline()));
    struct gf3_surface b = a;
    b.off += MEM_PAGE_SIZE; /* both descriptors stay in the real test reservation */
    assert(!cint_slot->scale(cint_slot->ctx, &a, &b, r, r, r, false, cint_deadline()));
    /* The deliberately unaligned legacy scan remains valid for CPU scanout,
     * but is outside the drawing surface contract. */
    assert(geforce_fill_rect(0, 0, 2, 2, 1)==OS32_ERR_IO);
    assert(geforce_blit(2, 2, 0, 0, 2, 2)==OS32_ERR_IO);
    cint_owned_reservations();
    cint_stop_display();
}
static void cint_init_allocation_failures(void)
{
    /* Exhaust the real allocator at each of 14 allocation boundaries, without
     * replacing alloc/free/object_new. Earlier allocations must be unlinked. */
    for (unsigned allowed = 0; allowed < GF3_OBJECT_COUNT + 2U; ++allowed) {
        cint_reset();
        u32 original = cint_prod.foundation.imem.aperture_bytes;
        u32 end = cint_used_end();
        for (unsigned i = 0; i < allowed; ++i) {
            end = (end + GF2_OBJECT_ALIGN - 1U) & ~(GF2_OBJECT_ALIGN - 1U);
            end += i == 0 ? GF3_NOTIFY_BYTES : i == 1 ? GF2_DMA_OBJECT_BYTES : GF2_GR_OBJECT_BYTES;
        }
        cint_prod.foundation.imem.aperture_bytes = end;
        int rc = gf3_service_start(cint_service, cint_deadline());
        assert(rc == (allowed ? GF2_ENOSPC : GF1_ENOSPC));
        cint_closed(); cint_owned_reservations();
        assert(cint_prod.chan.state == GF2_RUN && !cint_prod.accel.ready);
        assert(cint_used_count() == 8U && !cint_prod.accel.notify_storage.size);
        u32 id = cint_service->test.reservation_id;
        cint_prod.foundation.imem.aperture_bytes = original;
        cint_start();
        assert(cint_service->test.reservation_id == id);
        cint_stop_display();
    }
}
static void cint_init_io_failures(void)
{
    /* Every successful-init MMIO/clock boundary is retried with one failing
     * callback. The real unwind may leave RUN or stop F2; both retain keys. */
    cint_reset(); cint_start();
    u32 counts[] = {cint_host.calls[H_READ], cint_host.calls[H_WRITE], cint_host.calls[H_CLOCK]};
    cint_stop_display();
    const enum gf3_host_op ops[] = {H_READ, H_WRITE, H_CLOCK};
    for (unsigned k = 0; k < sizeof(ops) / sizeof(ops[0]); ++k)
        for (u32 n = 1; n <= counts[k]; ++n) {
            cint_reset();
            gf3_host_fail_at(&cint_host, ops[k], n, CINT_IO_FAILURE);
            assert(gf3_service_start(cint_service, cint_deadline()) < 0);
            cint_closed(); cint_owned_reservations();
            assert(!cint_prod.accel.ready);
            cint_trace_clear();
            if (cint_prod.chan.state != GF2_RUN) {
                assert(cint_prod.chan.state == GF2_OFF);
                assert(!cint_prod.foundation.imem.used && !cint_prod.chan.mapped);
                assert(!gf_foundation_start(&cint_prod.foundation));
                assert(!gf2_start(&cint_prod.chan, &cint_prod.foundation));
            } else assert(cint_used_count() == 8U);
            cint_start(); cint_stop_display();
        }
}
static void cint_recover_success_and_fault(void)
{
    for (unsigned fault = 0; fault < 3; ++fault) {
        cint_reset(); cint_start(); cint_open_windows();
        struct gf3_surface old = cint_service->test;
        u32 epoch = cint_prod.chan.epoch, gen = cint_service->scan.generation;
        if (fault == 1) {
            /* Real F1 IRQ statistics feed real F2 foreground fault capture. */
            ++cint_prod.foundation.stats.graph;
            assert(gf2_fault_check(&cint_prod.chan) == GF2_EFAULT);
        }
        framebuffer = (struct video_fb){0};
        cint_trace_clear(); cint_expect_closed = true;
        /* F4 stage.h / nouveau nv04.c:1220: object setup gets its own
         * budget even when F1 reinitialization consumed the caller's dl. */
        u32 old_dl = cint_deadline();
        if (fault == 2) cint_recover_delay = GF3_TIMEOUT_US + 1U;
        assert(!gf3_service_recover(cint_service, old_dl));
        if (fault == 2) assert((i32)(cint_host.now - old_dl) > 0);
        cint_expect_closed = false;
        assert(cint_slot && hook_active && cint_prod.accel.ready);
        assert(cint_prod.chan.epoch == epoch + 1U && cint_prod.accel.epoch == epoch + 1U);
        assert(cint_service->scan.generation == gen + 1U && cint_service->test.reservation_id != old.reservation_id);
        assert(cint_service->scan.pitch == CINT_SCAN_PITCH && cint_service->test.pitch == CINT_TEST_PITCH);
        assert(!views && !cint_loan[0] && !cint_loan[1]);
        u32 writes = cint_host.calls[H_WRITE];
        assert(gf3_fill(&cint_prod.accel, &old, (struct gf3_rect){0,0,1,1}, 1, 3, 0xff, cint_deadline()) == GF3_ESTATE);
        assert(cint_host.calls[H_WRITE] == writes);
        cint_stop_display();
    }
}
static void cint_recover_close_retry(void)
{
    cint_reset(); cint_start(); cint_open_windows();
    u32 id = cint_service->scan.reservation_id, count = cint_used_count();
    struct gf_inst *head = cint_prod.foundation.imem.used;
    cint_trace_clear(); cint_expect_closed = true; cint_unmap_fail = CINT_MAP_VIEW0;
    assert(gf3_service_recover(cint_service, cint_deadline()) == GF2_EMAP);
    cint_closed(); cint_owned_reservations();
    assert(cint_prod.chan.state == GF2_RUN && cint_prod.accel.ready);
    assert(cint_prod.foundation.imem.used == head && cint_used_count() == count);
    assert(cint_view[0].mapped && !cint_view[1].mapped);
    assert(!cint_host.calls[H_WRITE] && !cint_host.calls[H_CLOCK]);
    cint_unmap_fail = -1;
    assert(!gf3_service_recover(cint_service, cint_deadline()));
    cint_expect_closed = false;
    assert(cint_slot && cint_prod.accel.ready && cint_service->scan.reservation_id != id);
    assert(cint_close_calls[0] == 2 && cint_close_calls[1] == 1);
    cint_stop_display();
}
static void cint_failure_cleanup(void)
{
    cint_reset();
    /* Exercise the real C1 failure sink with a partial real initialization;
     * metadata identifying the initial error must survive C2's cleanup. */
    int rc = gf3_service_start(cint_service, cint_host.now);
    assert(rc == GF2_ETIME);
    cint_prod.display.section = GF_DISPLAY_CALC;
    cint_prod.display.ordinal = 7;
    cint_prod.display.target = 0x1234U;
    cint_prod.display.last_rc = rc;
    cint_expect_closed = true;
    assert(gf_failure(&cint_prod.display) == -1);
    assert(cint_prod.display.section == GF_DISPLAY_CALC && cint_prod.display.ordinal == 7);
    assert(cint_prod.display.target == 0x1234U && cint_prod.display.last_rc == rc);
    cint_no_resources();
}
static void cint_reference_failure_and_retained_init(void)
{
    for (unsigned wait_error = 0; wait_error < 2; ++wait_error) {
        cint_reset();
        cint_stall_reference = cint_references + 1U;
        if (wait_error) gf3_host_fail_at(&cint_host, H_SLEEP, 1, CINT_IO_FAILURE);
        assert(gf3_service_start(cint_service, cint_host.now + 3U) ==
            (wait_error ? GF2_EIO : GF2_ETIME));
        assert(cint_saw_unfinished); /* GET==PUT was not treated as completion */
        cint_closed(); cint_owned_reservations();
        assert(cint_prod.chan.state == GF2_OFF && !cint_prod.foundation.imem.used);
        cint_trace_clear(); cint_stop_display();
    }
    /* Fail after all real F3 inst have been published, then deny stop.
     * This distinguishes retained live descriptors from the old stub's
     * simulated sizes/handles and catches invalidate-before-OFF mutations. */
    cint_reset(); cint_start();
    u32 nth = 0;
    for (u32 i = 0; i < cint_host.nevents; ++i)
        if (cint_events[i].op == H_WRITE && cint_events[i].a[1] == GF2_USER_BASE + GF2_USER_PUT)
            nth = cint_events[i].nth;
    assert(nth); cint_stop_display(); cint_reset();
    gf3_host_fail_at(&cint_host, H_WRITE, nth, CINT_IO_FAILURE);
    cint_fail_stop_after_put = true;
    assert(gf3_service_start(cint_service, cint_deadline()) == GF2_ESTOP);
    cint_closed(); cint_owned_reservations();
    assert(cint_prod.chan.state == GF2_FAULT && !cint_prod.accel.ready);
    assert(cint_prod.accel.obj[0].handle && cint_inst_live(&cint_prod.accel.obj[0].inst));
    assert(cint_inst_live(&cint_prod.accel.notify_storage));
    assert(cint_used_count() == 8U + 2U + GF3_OBJECT_COUNT);
    assert(cint_prod.chan.mapped && cint_prod.foundation.mapped && cint_dma_live);
    cint_trace_clear(); cint_expect_closed = true;
    assert(gf_display_stop(&cint_prod.display) == -1 && cint_prod.display.last_rc == GF2_EFAULT);
    cint_no_resources();
}
static void cint_recover_failures(void)
{
    /* Concrete stages: old stop, new F1 master/map, new F2 map/GR/reference,
     * and F3 allocation after new F2's initial reference has completed. */
    for (unsigned stage = 0; stage < 7; ++stage) {
        cint_reset(); cint_start();
        u32 old_id = cint_service->scan.reservation_id;
        cint_old_epoch = cint_prod.chan.epoch;
        cint_trace_clear(); cint_expect_closed = true;
        if (stage == 1) cint_pci_fail_at = cint_pci_writes + 2U; /* old disable, then new enable */
        if (stage == 2) cint_map_fail = CINT_MAP_IMEM;
        if (stage == 3) cint_map_fail = CINT_MAP_PUSH;
        if (stage == 0 || stage == 4 || stage == 5) {
            /* Locate the real new-F2 operation from a successful trace below. */
            cint_expect_closed = false;
            assert(!gf3_service_recover(cint_service, cint_deadline()));
            u32 nth = 0, matches = 0;
            for (u32 i = 0; i < cint_host.nevents; ++i) {
                const struct gf3_host_event *e = &cint_events[i];
                u32 off = stage == 0 ? COMMAND_CACHES : stage == 4 ? GR_PROGRAM_DATA : GF2_USER_BASE + GF2_USER_PUT;
                if (e->op == H_WRITE && e->a[1] == off &&
                    (stage != 0 || e->a[3] == 0) && ++matches == (stage == 4 ? 1U : 2U)) {
                    nth = e->nth; break;
                }
            }
            assert(nth); cint_stop_display(); cint_reset(); cint_start();
            old_id = cint_service->scan.reservation_id;
            cint_old_epoch = cint_prod.chan.epoch;
            cint_trace_clear(); cint_expect_closed = true;
            gf3_host_fail_at(&cint_host, H_WRITE, nth, CINT_IO_FAILURE);
        }
        if (stage == 6) cint_limit_new_imem = true;
        assert(gf3_service_recover(cint_service, cint_deadline()) < 0);
        cint_closed();
        assert(cint_service->scan_node.value.owner && cint_service->test_node.value.owner);
        if (stage == 6) {
            assert(cint_prod.chan.state == GF2_RUN && !cint_prod.accel.ready);
            assert(cint_service->scan.reservation_id != old_id && cint_used_count() == 8U);
            cint_prod.foundation.imem.aperture_bytes = CINT_IMEM_BYTES;
        } else assert(cint_service->scan.reservation_id == old_id);
        cint_map_fail = -1; cint_pci_fail_at = 0; cint_trace_clear();
        /* Real F2 preserves EFAULT when it reaches OFF from FAULT
         * (command.c:gf2_stop); C2 must retain that first error while
         * completing display cleanup and releasing every reservation. */
        if (cint_prod.chan.state == GF2_FAULT) {
            assert(gf_display_stop(&cint_prod.display) == -1);
            assert(cint_prod.display.last_rc == GF2_EFAULT);
            cint_no_resources();
        } else cint_stop_display();
    }
}
static void cint_recover_without_display(void)
{
    cint_reset(); cint_start();
    u32 id = cint_service->scan.reservation_id;
    cint_prod.display.engaged = false;
    cint_expect_closed = true;
    assert(!gf3_service_recover(cint_service, cint_deadline()));
    cint_closed();
    assert(cint_prod.chan.state == GF2_RUN && !cint_prod.accel.ready);
    assert(cint_service->scan.reservation_id == id && cint_used_count() == 8U);
    /* !engaged with RUN must stop F2 before releasing the live reservations. */
    cint_stop_display();
}
static void cint_recover_f3_submission_failure(void)
{
    cint_reset(); cint_start(); cint_trace_clear();
    assert(!gf3_service_recover(cint_service, cint_deadline()));
    u32 nth = 0;
    for (u32 i = 0; i < cint_host.nevents; ++i)
        if (cint_events[i].op == H_WRITE && cint_events[i].a[1] == GF2_USER_BASE + GF2_USER_PUT)
            nth = cint_events[i].nth; /* final new-F3 setup reference */
    assert(nth); cint_stop_display();
    for (unsigned persistent = 0; persistent < 3; ++persistent) {
        cint_reset(); cint_start();
        u32 id = cint_service->scan.reservation_id, epoch = cint_prod.chan.epoch;
        cint_trace_clear(); cint_expect_closed = true;
        if (persistent < 2) {
            gf3_host_fail_at(&cint_host, H_WRITE, nth, CINT_IO_FAILURE);
            cint_fail_stop_after_put = persistent != 0;
        } else {
            /* Old stop + new F2 reference + new F3 pre-drain + setup drain.
             * Stall only the fourth: F2's later cleanup reference can finish. */
            cint_stall_reference = cint_references + 4U;
            cint_recover_timeout = true;
        }
        int rc = gf3_service_recover(cint_service, cint_deadline());
        assert(rc == (persistent == 0 ? GF2_EIO : persistent == 1 ? GF2_ESTOP : GF2_ETIME));
        cint_closed(); cint_owned_reservations();
        assert(cint_prod.chan.epoch == epoch + 1U && cint_service->scan.reservation_id != id);
        assert(!cint_prod.accel.ready);
        if (persistent == 1) {
            assert(cint_prod.chan.state == GF2_FAULT && cint_prod.accel.obj[0].handle);
            assert(cint_inst_live(&cint_prod.accel.obj[0].inst));
            assert(cint_used_count() == 8U + 2U + GF3_OBJECT_COUNT);
            cint_trace_clear();
            assert(gf_display_stop(&cint_prod.display) == -1);
            assert(cint_prod.display.last_rc == GF2_EFAULT);
            cint_no_resources();
        } else {
            assert(cint_prod.chan.state == GF2_OFF && !cint_prod.foundation.imem.used);
            if (persistent == 2) assert(cint_saw_unfinished);
            cint_trace_clear(); cint_stop_display();
        }
    }
}
static void cint_id_exhaustion(void)
{
    cint_reset(); cint_start();
    cint_trace_clear(); cint_expect_closed = true;
    registry.next_id = U32_MAX; /* last scan ID is issued; test reserve fails */
    assert(gf3_service_recover(cint_service, cint_deadline()) == GF2_ENOSPC);
    /* F2: ID exhaustion during reservation update is an error, not the
     * nonfatal geometric no-test-capacity case. Current service_publish
     * conflates them and rebinds the hook: retain this regression assertion. */
    cint_closed();
    assert(cint_prod.chan.state == GF2_RUN && !cint_prod.accel.ready);
    assert(cint_service->scan_node.value.owner && !cint_service->test_node.value.owner);
    assert(cint_used_count() == 8U);
    /* Next recover fails at scan reserve; no key issuer reset or wrap. */
    assert(gf3_service_recover(cint_service, cint_deadline()) == GF2_ENOSPC);
    assert(!registry.head && !cint_prod.accel.ready && cint_prod.chan.state == GF2_RUN);
    /* No display surface exists after this failed update; stop F2 then C2's
     * !engaged/OFF path, which needs neither restoration nor a fake reserve. */
    assert(!gf3_service_stop(cint_service));
    cint_prod.display.engaged = false;
    assert(!gf_display_stop(&cint_prod.display));
    cint_no_resources();
}
void _start(void)
{
    assert(!gf3_service_get(0) && !gf3_service_get(&cint_prod.chan));
    cint_start_and_diagnostic_stop();
    assert(!gf3_service_get(&cint_host.ch)); /* diagnostic channel stays separate */
    cint_real_dispatch();
    cint_init_allocation_failures();
    cint_init_io_failures();
    cint_reference_failure_and_retained_init();
    cint_recover_success_and_fault();
    cint_recover_close_retry();
    cint_recover_failures();
    cint_recover_f3_submission_failure();
    cint_recover_without_display();
    cint_failure_cleanup();
    cint_id_exhaustion(); /* identity exhaustion is deliberately the final case */
    gf2_host_exit(0);
}
#endif
