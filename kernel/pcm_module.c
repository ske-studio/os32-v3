#include "tick.h"
#include "tick_order.h"
_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "PC98 tick slots exceed table");
/* Resident facade and IRQ/tick/DMA references, using the T5c-0 record. */
#include "pcm_module.h"
#include "pcm_imports.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "kstring.h"
#include "kprintf.h"
#include "io.h"

extern const u32 pcm_kexports[];
extern const struct module_symbol_contract pcm_import_contracts[];
extern const struct module_symbol_contract pcm_export_contracts[];
static struct module_handle pcm_handle;
static PcmModuleApi candidate;
static int candidate_valid;
static void (*tick_target)(void);
static irq_handler_fn irq_target;

void pcm_tick(void)
{
    if (tick_target) tick_target();
}

int pcm_open(u32 rate)
{
    if (module_exports(pcm_handle)) return candidate.open(rate);
    if (rate != PCM_RATE_44100 && rate != PCM_RATE_22050) return OS32_ERR_INVAL;
    return OS32_ERR_NOSYS;
}
int pcm_write_check(void)
{
    if (module_exports(pcm_handle)) return candidate.write_check();
    return OS32_ERR_INVAL;
}
int pcm_write(const void *buf, u32 bytes)
{
    if (module_exports(pcm_handle)) return candidate.write(buf, bytes);
    return OS32_ERR_INVAL;
}
int pcm_status(u32 *free_bytes, u32 *counters)
{
    if (module_exports(pcm_handle)) return candidate.status(free_bytes, counters);
    return OS32_ERR_INVAL;
}
int pcm_close(void)
{
    if (module_exports(pcm_handle)) return candidate.close();
    return OS32_ERR_INVAL;
}
int pcm_set_volume(u32 percent)
{
    if (module_exports(pcm_handle)) return candidate.set_volume(percent);
    return OS32_ERR_INVAL;
}
int pcm_state(void)
{
    if (module_exports(pcm_handle)) return candidate.state();
    return PCM_ST_CLOSED;
}
int pcm_present(void)
{
    if (module_exports(pcm_handle)) return candidate.present();
    return 0;
}
void pcm_reclaim(int owner)
{
    if (module_exports(pcm_handle)) candidate.reclaim(owner);
}

int pcm_module_irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags)
{
    struct module_record *r = module_record_get(pcm_handle);
    if (irq != PCM_IRQ || arg || irq_target || !r) return IRQ_ERR_INVAL;
    unsigned int saved = irq_save();
    int rc = IRQ_ERR_BUSY;
    if (module_ref_acquire(pcm_handle, r->owner, MODULE_REF_IRQ, (u32)(uptr)fn)) {
        rc = irq_register(irq, fn, arg, flags);
        if (rc) module_ref_release(pcm_handle, MODULE_REF_IRQ);
        else irq_target = fn;
    }
    irq_restore(saved);
    return rc;
}

int pcm_module_irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg)
{
    if (irq != PCM_IRQ || arg || !irq_target || irq_target != fn) return IRQ_ERR_INVAL;
    unsigned int saved = irq_save();
    int rc = irq_unregister(irq, fn, arg);
    if (!rc) {
        irq_target = 0;
        module_ref_release(pcm_handle, MODULE_REF_IRQ);
    }
    irq_restore(saved);
    return rc;
}

int pcm_module_dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    struct module_record *r = module_record_get(pcm_handle);
    if (!r || !module_ref_acquire(pcm_handle, r->owner, MODULE_REF_DMA, 0))
        return DMA_POOL_ERR_STATE;
    int rc = dma_alloc(size, align, limit, out);
    if (rc) module_ref_release(pcm_handle, MODULE_REF_DMA);
    return rc;
}

int pcm_module_dma_free(const struct dma_buf *buf)
{
    /* The body calls here only after its stop evidence, or before any start.
     * dma_mark_leaked deliberately leaves this reference and ring in place. */
    int rc = dma_free(buf);
    if (!rc) module_ref_release(pcm_handle, MODULE_REF_DMA);
    return rc;
}

static int pcm_start_module(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(PcmModuleApi *) = (int (*)(PcmModuleApi *))(uptr)entry;
    int rc = init(&candidate);
    u32 entries[PCM_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), pcm_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < PCM_MODULE_API_SLOTS; i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    candidate_valid = 1;
    if (rc) return rc;
    unsigned int flags = irq_save();
    int acquired = module_ref_acquire(pcm_handle, r->owner, MODULE_REF_TICK,
                                      (u32)(uptr)candidate.tick);
    if (acquired) tick_target = candidate.tick;
    irq_restore(flags);
    return acquired ? 0 : MODULE_INIT_FAILED;
}

static int pcm_quiesce(struct module_record *r)
{
    (void)r;
    /* Reclaim claims the state under IF=0 before it can restart via tick. */
    return candidate_valid && candidate.quiet();
}

static int pcm_detach(struct module_record *r)
{
    (void)r;
    unsigned int flags = irq_save();
    if (tick_target) {
        tick_target = 0;
        module_ref_release(pcm_handle, MODULE_REF_TICK);
    }
    int rc = !irq_target || !pcm_module_irq_unregister(PCM_IRQ, irq_target, 0);
    irq_restore(flags);
    return rc;
}

static int pcm_stop_dma(struct module_record *r)
{
    return r->dma_refs == 0;
}

int pcm_module_load(void)
{
    if (tick_register(PC98_TICK_PCM, pcm_tick) != 0) return MODULE_INIT_FAILED;
    if (pcm_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_PCM_MOD, .name = "pcm",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, PCM_MODULE_ABI_GEN,
            bi->img_crc, PCM_MODULE_OPS_VERSION, bi->img_valid,
            PCM_IMPORT_COUNT, pcm_import_contracts, pcm_export_contracts,
            MODULE_ID_PCM
        },
        .imports = pcm_kexports,
        .lifecycle = {.start = pcm_start_module, .quiesce = pcm_quiesce,
                      .detach = pcm_detach, .stop_dma = pcm_stop_dma}
    };
    int rc = module_load(&request, &pcm_handle);
    if (!rc) rc = module_start(pcm_handle);
    if (!rc && !module_exports(pcm_handle)) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[pcm] module rc=%d pair=%08x tick=%d\n",
            rc, bi->img_crc, tick_target != 0);
    return rc;
}

int pcm_module_abort(void) { return module_abort(pcm_handle); }
