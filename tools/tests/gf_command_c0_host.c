/* C0 lifecycle oracle, GF_FEAT2_DESIGN sections 6/9 and INIT addendum.
 * Include the real command.c (override GF2_COMMAND_SOURCE for mutations),
 * and link the real hwdev/gf/fence.c to test old-epoch rejection too.
 * A2's GF2_HOST_B0..B9 fixtures have no C0 selector: dependency fakes below
 * are local until A2 incorporates them. No ports or privileged instructions.
 * Test names use underscores in C: c0-stop-order -> c0_stop_order, etc. */
#define IO_H
#include "gf_command_runtime_host.h"
#include "../../hwdev/gf/command.h"
#ifdef NDEBUG
#error "C0's ownership and ordering oracle requires assertions"
#endif
static unsigned c0_if = 0x202U;
static unsigned irq_save(void)
{ unsigned saved = c0_if; c0_if &= ~0x200U; return saved; }
static void irq_restore(unsigned saved)
{ assert(!(c0_if & 0x200U)); c0_if = saved; }
#ifndef GF2_COMMAND_SOURCE
#define GF2_COMMAND_SOURCE "../../hwdev/gf/command.c"
#endif
#include GF2_COMMAND_SOURCE

enum c0_event {
    C0_GATE, C0_IRQ_OFF, C0_SNAPSHOT, C0_IRQ_ON, C0_FB, C0_MAP,
    C0_DMA_NEW, C0_DMA_GET, C0_FIFO_INIT, C0_GR_INIT, C0_FIFO_START,
    C0_REFERENCE, C0_KICK, C0_READ_REFERENCE, C0_FIFO_STOP, C0_ENGINE,
    C0_RAMHT_DEL, C0_INST_FREE, C0_F1_STOP, C0_DMA_PUT, C0_UNMAP, C0_F1_START
};
static struct { enum c0_event kind; u32 value; } c0_trace[256];
static unsigned c0_count, c0_pos, c0_heads;
static struct gf_foundation c0_f;
static struct gf2_chan c0_ch;
static u32 c0_ring[GF2_PUSH_WORDS], c0_now, c0_reference;
static u32 c0_deadline, c0_emitted;
static int c0_emit_rc, c0_engine_rc, c0_f1_stop_rc;
static bool c0_complete, c0_engine_done, c0_f1_done;

static void c0_log(enum c0_event kind, u32 value)
{
    assert(c0_count < sizeof(c0_trace) / sizeof(c0_trace[0]));
    c0_trace[c0_count].kind = kind;
    c0_trace[c0_count++].value = value;
}
static void c0_expect(enum c0_event kind, u32 value)
{
    assert(c0_pos < c0_count);
    assert(c0_trace[c0_pos].kind == kind && c0_trace[c0_pos].value == value);
    ++c0_pos;
}
static void c0_done(void)
{ assert(c0_pos == c0_count && c0_if == 0x202U); }
static void c0_clear_trace(void) { c0_count = c0_pos = 0; }

/* Independent gate oracle: masked writes preserve every bit except bit0. */
static bool c0_masked_gate(u32 off)
{ return off == 0x3220U || off == 0x3250U || off == 0x400720U; }
static int c0_read(void *owner, u32 bar, u32 off, u32 width, u32 *value)
{
    assert(owner == &c0_f && bar == 0 && width == 4);
    if (off == 0x00c00048U) {
        c0_log(C0_READ_REFERENCE, c0_ch.state);
        *value = c0_reference;
    } else {
        assert(c0_masked_gate(off));
        *value = 0xa5U;
    }
    return 0;
}
static int c0_write(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    assert(owner == &c0_f && bar == 0 && width == 4);
    assert(off == 0x2500U || off == 0x3200U || c0_masked_gate(off));
    assert(value == (c0_masked_gate(off) ? 0xa4U : 0U));
    c0_log(C0_GATE, off);
    return 0;
}
static int c0_clock(void *owner, u32 *now)
{ assert(owner == &c0_f); *now = c0_now; return 0; }
static int c0_wait(void *owner, u32 us)
{
    assert(owner == &c0_f && us && us <= GF2_WAIT_MAX_US);
    /* Advance to expiry in one fake wait; avoid 40,000 logged polls. */
    c0_now = c0_deadline;
    return 0;
}
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *out)
{ assert(f == &c0_f); *out = f->stats; c0_log(C0_SNAPSHOT, 0); }
int gf_irq_enable(struct gf_foundation *f, bool fifo, bool graph, unsigned heads)
{
    assert(f == &c0_f && !(c0_if & 0x200U) && fifo == graph);
    /* Check the first close too: a zero-initialized channel must inherit F1. */
    assert(heads == c0_heads);
    if (fifo) assert(c0_ch.seen.fifo == f->stats.fifo &&
                     c0_ch.seen.graph == f->stats.graph);
    f->wanted_fifo = fifo; f->wanted_graph = graph; f->wanted_heads = heads;
    c0_log(fifo ? C0_IRQ_ON : C0_IRQ_OFF, heads);
    return GF1_OK;
}
int gf2_fb_init(struct gf2_chan *ch)
{ assert(ch == &c0_ch); c0_log(C0_FB, 0); return GF2_OK; }
int gf2_map_vram(struct gf2_chan *ch, u32 off, u32 bytes, volatile u32 **out)
{
    assert(ch == &c0_ch && off == ch->B && bytes == GF2_PUSH_BYTES && !ch->mapped);
    ch->mapped = ch->vram_reserved = true; *out = c0_ring;
    c0_log(C0_MAP, 0); return GF2_OK;
}
int gf2_unmap_vram(struct gf2_chan *ch)
{
    assert(ch == &c0_ch && c0_f1_done && ch->mapped && !ch->staging_live);
    ch->mapped = ch->vram_reserved = false; ch->va = 0;
    c0_log(C0_UNMAP, 0); return GF2_OK;
}
int gf2_dma_get(struct gf2_chan *ch)
{
    assert(ch == &c0_ch && !ch->staging_live);
    ch->staging = (struct gf2_dma_buf){0x12345000U, GF2_STAGING_BYTES, c0_ring};
    ch->staging_live = true; c0_log(C0_DMA_GET, 0); return GF2_OK;
}
void gf2_dma_put(struct gf2_chan *ch)
{
    assert(ch == &c0_ch && c0_f1_done);
    ch->staging_live = false; c0_log(C0_DMA_PUT, 0);
}
static void c0_link(struct gf_inst *inst)
{
    inst->off = 0x40000U; inst->size = 16;
    inst->next = c0_f.imem.used; c0_f.imem.used = inst;
}
int gf2_dma_new(struct gf2_chan *ch, u32 handle, u16 class_id,
                u8 target, u8 access, u32 start, u32 limit, struct gf2_object *obj)
{
    assert(ch == &c0_ch && ch->state == GF2_INIT && access == GF2_DMA_ACCESS_RW);
    if (handle == GF2_HANDLE_PUSH_DMA) {
        assert(class_id == GF2_DMA_FROM_MEMORY && obj == &ch->push_dma);
    } else {
        assert(class_id == GF2_DMA_IN_MEMORY);
        assert(obj == (handle == GF2_HANDLE_VRAM_DMA ? &ch->vram_dma : &ch->pci_dma));
        obj->cookie = handle == GF2_HANDLE_VRAM_DMA ? 1 : 2;
        ch->bitmap[0] |= 1U << (obj->cookie - 1U);
    }
    assert(target == (handle == GF2_HANDLE_PCI_DMA ? GF2_DMA_TARGET_PCI : GF2_DMA_TARGET_VRAM));
    assert(start == (target == GF2_DMA_TARGET_PCI ? ch->staging.pa : 0));
    assert(limit == (target == GF2_DMA_TARGET_PCI ? start + ch->staging.size - 1U :
                    c0_f.imem.vram_bytes - c0_f.imem.reserved - 1U));
    obj->handle = handle; c0_link(&obj->inst); c0_log(C0_DMA_NEW, handle);
    return GF2_OK;
}
int gf2_fifo_init(struct gf2_chan *ch)
{ assert(ch == &c0_ch); c0_log(C0_FIFO_INIT, 0); return GF2_OK; }
int gf2_gr_init(struct gf2_chan *ch)
{ assert(ch == &c0_ch); c0_link(&ch->grctx); c0_log(C0_GR_INIT, 0); return GF2_OK; }
int gf2_fifo_start(struct gf2_chan *ch)
{
    assert(ch == &c0_ch && ch->state == GF2_INIT);
    c0_log(C0_FIFO_START, 0); return GF2_OK;
}
void gf2_fifo_stop(struct gf2_chan *ch)
{ assert(ch == &c0_ch && ch->state == GF2_DRAIN); c0_log(C0_FIFO_STOP, 0); }
int gf2_fault_check(struct gf2_chan *ch)
{ assert(ch == &c0_ch); return ch->state == GF2_FAULT ? GF2_EFAULT : GF2_OK; }
int gf2_push_reference(struct gf2_chan *ch, u32 seq, u32 deadline)
{
    assert(ch == &c0_ch && (ch->state == GF2_INIT || ch->state == GF2_DRAIN));
    assert(c0_f.wanted_fifo && c0_f.wanted_graph);
    if (ch->state == GF2_INIT) {
        assert(ch->cur == GF2_PUSH_SKIPS && ch->put == 0);
        assert(ch->free == ch->max - ch->cur);
        for (unsigned i = 0; i < GF2_PUSH_SKIPS; ++i) assert(ch->va[i] == 0);
    }
    c0_deadline = deadline; c0_emitted = seq;
    assert(deadline == c0_now + GF2_TIMEOUT_US);
    c0_log(C0_REFERENCE, ch->state); return c0_emit_rc;
}
int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool noninc,
             const u32 *data, u16 count, u32 deadline)
{
    (void)ch; (void)subc; (void)method; (void)noninc;
    (void)data; (void)count; (void)deadline;
    assert(0); return GF2_ESTATE; /* Lifecycle references use INIT/DRAIN. */
}
int gf2_kick(struct gf2_chan *ch)
{
    assert(ch == &c0_ch); c0_log(C0_KICK, ch->state);
    if (c0_complete) c0_reference = c0_emitted;
    return GF2_OK;
}
int gf_engine_stop(struct gf_foundation *f)
{
    assert(f == &c0_f); c0_log(C0_ENGINE, 0);
    if (!c0_engine_rc) c0_engine_done = true;
    return c0_engine_rc;
}
int gf2_ramht_del(struct gf2_chan *ch, u32 cookie)
{
    assert(ch == &c0_ch && c0_engine_done && cookie >= 1 && cookie <= 2);
    assert(ch->bitmap[0] & (1U << (cookie - 1U)));
    ch->bitmap[0] &= ~(1U << (cookie - 1U));
    c0_log(C0_RAMHT_DEL, cookie); return GF2_OK;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *inst)
{
    assert(f == &c0_f && c0_engine_done && !c0_ch.bitmap[0]);
    struct gf_inst **link = &f->imem.used;
    while (*link && *link != inst) link = &(*link)->next;
    assert(*link == inst && inst != &f->imem.ramfc);
    *link = inst->next; inst->size = 0; c0_log(C0_INST_FREE, 0);
}
int gf_foundation_stop(struct gf_foundation *f)
{
    assert(f == &c0_f && c0_engine_done && f->imem.used == &f->imem.ramfc);
    c0_log(C0_F1_STOP, 0); f->running = false;
    if (c0_f1_stop_rc) return c0_f1_stop_rc;
    f->imem.used = 0; f->mapped = f->registered = f->bm = false;
    c0_f1_done = true; return GF1_OK;
}
int gf_foundation_start(struct gf_foundation *f)
{
    assert(f == &c0_f && c0_ch.state == GF2_OFF && c0_f1_done);
    c0_log(C0_F1_START, 0);
    f->running = f->mapped = f->registered = f->bm = true;
    f->imem.used = &f->imem.ramfc; f->wanted_heads = c0_heads;
    c0_engine_done = c0_f1_done = false; c0_reference = 0;
    return GF1_OK;
}

static void c0_fresh(unsigned heads)
{
    c0_f = (struct gf_foundation){.running = true, .mapped = true,
        .registered = true, .bm = true, .wanted_heads = heads,
        .stats = {.fifo = 7, .graph = 11},
        .io = {.opaque = &c0_f, .read = c0_read, .write = c0_write,
               .timing = {.clock_us = c0_clock, .wait_us = c0_wait}},
        .imem = {.vram_bytes = 0x04000000U, .reserved = 0x200000U}};
    c0_f.imem.used = &c0_f.imem.ramfc;
    c0_ch = (struct gf2_chan){0}; /* heads deliberately not provided by caller */
    c0_heads = heads; c0_now = c0_reference = 0;
    c0_emit_rc = c0_engine_rc = c0_f1_stop_rc = 0;
    c0_complete = true; c0_engine_done = c0_f1_done = false;
    for (unsigned i = 0; i < GF2_PUSH_WORDS; ++i) c0_ring[i] = 0xdeadbeefU;
    c0_clear_trace();
}
static void c0_expect_gates(void)
{
    c0_expect(C0_GATE, 0x2500); c0_expect(C0_GATE, 0x3220);
    c0_expect(C0_GATE, 0x3200); c0_expect(C0_GATE, 0x3250);
    c0_expect(C0_GATE, 0x400720);
}
static void c0_expect_start(void)
{
    c0_expect(C0_SNAPSHOT, 0); c0_expect(C0_IRQ_OFF, c0_heads);
    c0_expect_gates(); c0_expect(C0_FB, 0); c0_expect(C0_MAP, 0);
    c0_expect(C0_DMA_NEW, GF2_HANDLE_PUSH_DMA);
    c0_expect(C0_DMA_NEW, GF2_HANDLE_VRAM_DMA); c0_expect(C0_DMA_GET, 0);
    c0_expect(C0_DMA_NEW, GF2_HANDLE_PCI_DMA);
    c0_expect(C0_FIFO_INIT, 0); c0_expect(C0_GR_INIT, 0);
    c0_expect(C0_FIFO_START, 0); c0_expect(C0_SNAPSHOT, 0);
    c0_expect(C0_IRQ_ON, c0_heads); c0_expect(C0_REFERENCE, GF2_INIT);
}
static void c0_expect_release(void)
{
    c0_expect(C0_RAMHT_DEL, 1); c0_expect(C0_RAMHT_DEL, 2);
    for (unsigned i = 0; i < 4; ++i) c0_expect(C0_INST_FREE, 0);
}
static void c0_expect_cleanup(void)
{ c0_expect(C0_DMA_PUT, 0); c0_expect(C0_UNMAP, 0); }
static void c0_owned(bool instances)
{
    assert(c0_ch.state == GF2_FAULT && c0_ch.last_rc == GF2_ESTOP);
    assert(c0_ch.mapped && c0_ch.vram_reserved && c0_ch.staging_live);
    assert(c0_ch.va == c0_ring && c0_ch.staging.pa == 0x12345000U);
    assert(c0_f.mapped && c0_f.registered && c0_f.bm);
    assert(c0_ch.bitmap[0] == (instances ? 3 : 0));
    assert(c0_f.imem.used == (instances ? &c0_ch.grctx : &c0_f.imem.ramfc));
    assert(c0_ch.push_dma.inst.size == (instances ? 16U : 0U));
    assert(c0_ch.vram_dma.inst.size == (instances ? 16U : 0U));
    assert(c0_ch.pci_dma.inst.size == (instances ? 16U : 0U));
    assert(c0_ch.grctx.size == (instances ? 16U : 0U));
}
static void c0_released(void)
{
    assert(c0_ch.state == GF2_OFF && !c0_ch.mapped && !c0_ch.vram_reserved);
    assert(!c0_ch.staging_live && !c0_f.imem.used && !c0_ch.bitmap[0]);
    assert(!c0_f.mapped && !c0_f.registered && !c0_f.bm);
}
static void c0_running(void)
{
    assert(gf2_start(&c0_ch, &c0_f) == GF2_OK);
    assert(c0_ch.state == GF2_RUN && c0_ch.epoch == 1 && c0_ch.seq == 1);
    c0_expect_start(); c0_expect(C0_KICK, GF2_INIT);
    c0_expect(C0_READ_REFERENCE, GF2_INIT); c0_done(); c0_clear_trace();
}
/* c0-stop-order: gate/IRQ/engine/instances/F1/staging/map, and idempotence. */
static void c0_stop_order(void)
{
    for (unsigned heads = 1; heads <= 3; ++heads) {
        c0_fresh(heads); c0_running();
        assert(gf2_stop(&c0_ch) == GF2_OK);
        c0_expect(C0_REFERENCE, GF2_DRAIN); c0_expect(C0_KICK, GF2_DRAIN);
        c0_expect(C0_READ_REFERENCE, GF2_DRAIN); c0_expect(C0_FIFO_STOP, 0);
        c0_expect_gates(); c0_expect(C0_IRQ_OFF, heads); c0_expect(C0_ENGINE, 0);
        c0_expect_release(); c0_expect(C0_F1_STOP, 0); c0_expect_cleanup();
        c0_done(); c0_released();
        c0_clear_trace(); assert(gf2_stop(&c0_ch) == GF2_OK); c0_done();
    }
}
/* c0-initial-reference-failure: neither emit error nor timeout becomes OK. */
static void c0_initial_reference_failure(void)
{
    for (unsigned wait_failure = 0; wait_failure < 2; ++wait_failure) {
        c0_fresh(1); c0_emit_rc = wait_failure ? 0 : GF2_EIO;
        c0_complete = false;
        int expected = wait_failure ? GF2_ETIME : GF2_EIO;
        assert(gf2_start(&c0_ch, &c0_f) == expected && c0_ch.last_rc == expected);
        c0_expect_start();
        if (wait_failure) {
            c0_expect(C0_KICK, GF2_INIT); c0_expect(C0_READ_REFERENCE, GF2_INIT);
        }
        c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1); c0_expect(C0_ENGINE, 0);
        c0_expect_release(); c0_expect(C0_F1_STOP, 0); c0_expect_cleanup();
        c0_done(); c0_released();
    }
}
/* c0-f1-stop-failure: only variable instances may have been released. */
static void c0_f1_stop_failure(void)
{
    c0_fresh(1); c0_running(); c0_ch.state = GF2_FAULT;
    c0_f1_stop_rc = GF1_ESTOP;
    assert(gf2_stop(&c0_ch) == GF2_ESTOP); c0_owned(false);
    c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1); c0_expect(C0_ENGINE, 0);
    c0_expect_release(); c0_expect(C0_F1_STOP, 0); c0_done();
    c0_clear_trace(); c0_f1_stop_rc = 0;
    assert(gf2_stop(&c0_ch) == GF2_EFAULT);
    c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1); c0_expect(C0_ENGINE, 0);
    c0_expect(C0_F1_STOP, 0); c0_expect_cleanup(); c0_done(); c0_released();
}
/* c0-engine-stop-failure: no RAMHT/instance/F1/staging/map release. */
static void c0_engine_stop_failure(void)
{
    c0_fresh(1); c0_running(); c0_ch.state = GF2_FAULT;
    c0_engine_rc = GF1_ESTOP;
    assert(gf2_stop(&c0_ch) == GF2_ESTOP); c0_owned(true);
    c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1); c0_expect(C0_ENGINE, 0); c0_done();
    c0_clear_trace(); c0_engine_rc = 0;
    assert(gf2_stop(&c0_ch) == GF2_EFAULT);
    c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1); c0_expect(C0_ENGINE, 0);
    c0_expect_release(); c0_expect(C0_F1_STOP, 0); c0_expect_cleanup();
    c0_done(); c0_released();
}
/* c0-recover-old-epoch: retry stop before restart; reject the old fence even
 * when the new epoch has already completed the same reference sequence. */
static void c0_recover_old_epoch(void)
{
    c0_fresh(1); c0_running();
    struct gf2_fence old = {c0_ch.epoch, c0_ch.seq};
    c0_ch.state = GF2_FAULT; c0_engine_rc = GF1_ESTOP;
    assert(gf2_recover(&c0_ch) == GF2_ESTOP && c0_ch.epoch == old.epoch);
    c0_owned(true); c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1);
    c0_expect(C0_ENGINE, 0); c0_done(); c0_clear_trace(); c0_engine_rc = 0;
    assert(gf2_recover(&c0_ch) == GF2_OK && c0_ch.state == GF2_RUN);
    assert(c0_ch.epoch == old.epoch + 1U && c0_ch.seq == old.seq);
    c0_expect_gates(); c0_expect(C0_IRQ_OFF, 1); c0_expect(C0_ENGINE, 0);
    c0_expect_release(); c0_expect(C0_F1_STOP, 0); c0_expect_cleanup();
    c0_expect(C0_F1_START, 0); c0_expect_start();
    c0_expect(C0_KICK, GF2_INIT); c0_expect(C0_READ_REFERENCE, GF2_INIT); c0_done();
    c0_clear_trace();
    assert(gf2_fence_wait(&c0_ch, old, c0_now + GF2_TIMEOUT_US) == GF2_EFAULT);
    c0_done(); /* Old fence must fail before any reference read. */
    struct gf2_fence current = {c0_ch.epoch, c0_ch.seq};
    assert(gf2_fence_wait(&c0_ch, current, c0_now + GF2_TIMEOUT_US) == GF2_OK);
    c0_expect(C0_READ_REFERENCE, GF2_RUN); c0_done();
}
void _start(void)
{
    c0_stop_order(); c0_initial_reference_failure(); c0_f1_stop_failure();
    c0_engine_stop_failure(); c0_recover_old_epoch();
    gf2_host_exit(0);
}
