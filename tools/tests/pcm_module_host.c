/* Real runtime + resident PCM brokers. Relocated body calls are a host seam;
 * the CS4231 fixture separately exercises the real device stop evidence. */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "pcm_cs4231.h" /* Device constants belong to the body fixture. */
#include "io.h"
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#ifndef PCM_ADAPTER
#define PCM_ADAPTER "../../kernel/pcm_module.c"
#endif
#include PCM_ADAPTER
#include "../../kernel/irq/tick.c"
volatile u32 kctx_irq_depth;
const u32 pcm_kexports[] = {0};
const struct module_symbol_contract pcm_import_contracts[] = {{0}};
const struct module_symbol_contract pcm_export_contracts[] = {{0}};
const struct bootinfo *bootinfo_get(void) { finish(2); }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }


static u32 ticks, mode, public_calls, dma_live, dma_freed, irq_live;
static unsigned int body_irq_line = PCM_IRQ;
static int register_error, unregister_error;
static u32 register_calls, unregister_calls;
static struct dma_buf ring;
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    CHECK(size == PCM_RING_BYTES && align == PCM_POOL_ALIGN && limit == DMA_PHYS_LIMIT);
    CHECK(!dma_live);
    dma_live = 1;
    *out = (struct dma_buf){.pa = MEM_DMA_POOL_BASE, .va = &ring, .size = size};
    CHECK((out->pa & 0xffffU) + size <= 0x10000U);
    return 0;
}
int dma_free(const struct dma_buf *buf)
{
    CHECK(buf->pa == MEM_DMA_POOL_BASE && dma_live);
    dma_live = 0; dma_freed++;
    return 0;
}
int irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags)
{
    register_calls++;
    CHECK(irq == body_irq_line && !arg && !irq_live);
    if (!fn || flags != IRQ_F_SHARED) return IRQ_ERR_INVAL;
    if (register_error) return register_error;
    irq_live = 1;
    return 0;
}
int irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg)
{
    unregister_calls++;
    CHECK(irq == body_irq_line && fn && !arg && irq_live);
    if (unregister_error) return unregister_error;
    irq_live = 0;
    return 0;
}
static int body_irq(unsigned int irq, void *arg) { (void)irq; (void)arg; return IRQ_HANDLED; }
static void body_tick(void) { ticks++; }
static int body_open(u32 rate)
{
    if (dma_live) return PCM_ERR_BUSY;
    CHECK(rate == PCM_RATE_44100);
    public_calls++;
    CHECK(!pcm_module_dma_alloc(PCM_RING_BYTES, PCM_POOL_ALIGN, DMA_PHYS_LIMIT, &ring));
    CHECK(!pcm_module_irq_register(body_irq_line, body_irq, 0, IRQ_F_SHARED));
    return 0;
}
static int body_quiet(void)
{
    /* Modes 4/5 leave IRQ cleanup to the resident detach path. */
    if (irq_target && mode != 4 && mode != 5)
        CHECK(!pcm_module_irq_unregister(body_irq_line, body_irq, 0));
    if (mode == 2) return 0; /* real driver leaves leaked ring and DMA ref */
    if (dma_live) CHECK(!pcm_module_dma_free(&ring));
    return 1;
}
static int init_pcm(PcmModuleApi *out)
{
    CHECK(!module_exports(pcm_handle));
    u32 entries[PCM_MODULE_API_SLOTS];
    for (u32 i = 0; i < PCM_MODULE_API_SLOTS; i++) entries[i] = (u32)(uptr)body_tick;
    kmemcpy(out, entries, sizeof(entries));
    out->tick = body_tick;
    out->quiet = body_quiet;
    out->open = body_open;
    return 0;
}
static int start_then_fail(struct module_record *r, u32 entry, u32 *ops)
{
    int rc = pcm_start_module(r, entry, ops);
    if (!rc && mode == 1) {
        body_open(PCM_RATE_44100);
        CHECK(r->irq_refs == 1 && r->dma_refs == 1 && !module_exports(pcm_handle));
        tick_dispatch();
        CHECK(pcm_open(PCM_RATE_44100) == OS32_ERR_NOSYS);
        CHECK(ticks == 1 && public_calls == 1);
        return MODULE_INIT_FAILED;
    }
    return rc;
}
void _start(void)
{
    kctx_irq_depth = 1;
    CHECK(pcm_module_load() == MODULE_INIT_FAILED);
    CHECK(!pcm_handle.generation && !slots[PC98_TICK_PCM].fn);
    CHECK(tick_register(PC98_TICK_PCM, pcm_tick) == TICK_ERR_CTX);
    kctx_irq_depth = 0;
    /* The retry reaches the existing busy check after publishing the facade. */
    pcm_handle.generation = 1;
    CHECK(pcm_module_load() == MODULE_BUSY);
    pcm_handle.generation = 0;
    CHECK(slots[PC98_TICK_PCM].fn == pcm_tick);
    CHECK(!tick_register(PC98_TICK_PCM, pcm_tick));
    CHECK(!tick_register(PC98_TICK_PCM, pcm_tick));
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = start_then_fail, .quiesce = pcm_quiesce,
                                           .detach = pcm_detach, .stop_dma = pcm_stop_dma};
    /* S4 invalid rate and missing-module stubs preserve their contract. */
    CHECK(pcm_open(PCM_RATE_44100 + 1) == OS32_ERR_INVAL);
    CHECK(pcm_open(PCM_RATE_44100) == OS32_ERR_NOSYS);
    CHECK(pcm_state() == PCM_ST_CLOSED && !pcm_present());
    CHECK(pcm_write(0, 0) == OS32_ERR_INVAL && pcm_close() == OS32_ERR_INVAL);
    CHECK(pcm_module_irq_register(PCM_IRQ, body_irq, 0, IRQ_F_SHARED) == IRQ_ERR_INVAL);
    tick_dispatch(); CHECK(!ticks);
    CHECK(!module_load(&q, &pcm_handle));
    struct module_record *r = module_record_get(pcm_handle);
    r->init = (u32)(uptr)init_pcm;
    r->text = (u32)(uptr)body_irq & ~(PAGE_SIZE - 1);
    r->text_size = 16 * PAGE_SIZE;
    int rc = module_start(pcm_handle);
    if (mode == 1 || mode == 3) {
        CHECK(rc == MODULE_INIT_FAILED);
        CHECK(!tick_target && !r->tick_refs && !irq_target && !r->irq_refs && !r->dma_refs);
        CHECK(!dma_live && !irq_live && r->state == MODULE_ABSENT);
        CHECK(dma_freed == (mode == 1 ? 1U : 0U));
        empty(); finish(0);
    }
    CHECK(!rc && r->tick_refs == 1 && tick_target);
    if (mode == 4) {
        /* A distinct line proves the facade has no device wiring constant. */
        body_irq_line = PCM_IRQ + 1;
        CHECK(pcm_module_irq_register(body_irq_line, body_irq, &ring, IRQ_F_SHARED) == IRQ_ERR_INVAL);
        CHECK(!register_calls && !r->irq_refs);
        /* The existing module reference guard rejects an out-of-text handler. */
        CHECK(pcm_module_irq_register(body_irq_line, 0, 0, IRQ_F_SHARED) == IRQ_ERR_BUSY);
        CHECK(!irq_target && !irq_line && !r->irq_refs && !irq_live);
        CHECK(pcm_module_irq_register(body_irq_line, body_irq, 0, 0) == IRQ_ERR_INVAL);
        CHECK(!irq_target && !irq_line && !r->irq_refs && !irq_live);
        register_error = IRQ_ERR_BUSY;
        CHECK(pcm_module_irq_register(body_irq_line, body_irq, 0, IRQ_F_SHARED) == IRQ_ERR_BUSY);
        CHECK(!irq_target && !irq_line && !r->irq_refs && !irq_live);
        register_error = 0;
    }
    CHECK(!pcm_open(PCM_RATE_44100)); tick_dispatch();
    CHECK(irq_line == body_irq_line && irq_target == body_irq);
    if (mode == 4) {
        u32 calls = register_calls;
        CHECK(pcm_module_irq_register(body_irq_line, body_irq, 0, IRQ_F_SHARED) == IRQ_ERR_INVAL);
        CHECK(register_calls == calls && r->irq_refs == 1);
        CHECK(pcm_module_irq_unregister(PCM_IRQ, body_irq, 0) == IRQ_ERR_INVAL);
        CHECK(pcm_module_irq_unregister(body_irq_line, 0, 0) == IRQ_ERR_INVAL);
        CHECK(pcm_module_irq_unregister(body_irq_line, body_irq, &ring) == IRQ_ERR_INVAL);
        CHECK(!unregister_calls && irq_live && r->irq_refs == 1);
        unregister_error = IRQ_ERR_BUSY;
        CHECK(pcm_module_irq_unregister(body_irq_line, body_irq, 0) == IRQ_ERR_BUSY);
        CHECK(irq_target == body_irq && irq_line == body_irq_line && irq_live && r->irq_refs == 1);
        unregister_error = 0;
    }
    CHECK(pcm_open(PCM_RATE_44100 + 1) == PCM_ERR_BUSY);
    CHECK(ticks == 1 && public_calls == 1 && r->dma_refs == 1 && r->irq_refs == 1);
    u32 before = returned;
    if (mode == 5) unregister_error = IRQ_ERR_BUSY;
    rc = pcm_module_abort();
    if (mode == 5) {
        CHECK(rc == MODULE_ISOLATED && r->state == MODULE_QUARANTINED);
        CHECK(irq_target == body_irq && irq_line == body_irq_line && irq_live && r->irq_refs == 1);
        CHECK(!tick_target && !r->tick_refs && !r->dma_refs);
        CHECK(returned == before && mask[0] == 2 && live[0] && !retired);
        CHECK(pcm_module_load() == MODULE_BUSY);
        finish(0);
    }
    CHECK(!tick_target && !r->tick_refs && !irq_target && !r->irq_refs && !irq_live);
    CHECK(!irq_line);
    tick_dispatch(); CHECK(ticks == 1);
    CHECK(pcm_open(PCM_RATE_44100) == OS32_ERR_NOSYS);
    if (mode == 2) {
        CHECK(rc == MODULE_ISOLATED && r->state == MODULE_QUARANTINED);
        CHECK(returned == before && mask[0] == 2 && live[0] && !retired);
        CHECK(dma_live && !dma_freed && r->dma_refs == 1);
        CHECK(pcm_module_abort() == MODULE_ISOLATED);
    } else {
        CHECK(!rc && r->state == MODULE_ABSENT && !dma_live && dma_freed == 1 && !r->dma_refs);
        CHECK(!pcm_module_abort()); empty();
        /* Reload the real runtime record after abort, then detach again. */
        CHECK(!module_load(&q, &pcm_handle));
        r = module_record_get(pcm_handle);
        r->init = (u32)(uptr)init_pcm;
        r->text = (u32)(uptr)body_irq & ~(PAGE_SIZE - 1);
        r->text_size = 16 * PAGE_SIZE;
        CHECK(!module_start(pcm_handle));
        CHECK(!pcm_open(PCM_RATE_44100));
        CHECK(irq_line == body_irq_line && irq_live && r->irq_refs == 1);
        CHECK(!pcm_module_abort());
        CHECK(!irq_line && !irq_target && !irq_live && !r->irq_refs && !r->dma_refs);
        empty();
    }
    finish(0);
}
