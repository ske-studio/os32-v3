#include "../../../kern/irq/tick.h"
#include "tick_order.h"
#include "tick_clock.h"
#include "sys/platform/pc98/tsc_clock.h"
#include "sys/hal/sound/snd2.h"
_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "PC98 tick slots exceed table");
/* Resident facade and IRQ/tick/DMA references, using the T5c-0 record. */
#include "pcm_module.h"
#include "pcm_imports.h"
#include "platform_module_pair.h"
#include "platform_irq.h"
#include "../../dma/dma8237.h"
#include "pcm_cs4231.h"
#include "config.h"
#include "os32_generations.h"
#include "kstring.h"
#include "kprintf.h"
#include "io.h"
#ifdef PCM_E4_DIAG
#include "pcm_e4_diag.h"
#endif

extern const u32 pcm_kexports[];
extern const struct module_symbol_contract pcm_import_contracts[];
extern const struct module_symbol_contract pcm_export_contracts[];
static struct module_handle pcm_handle;
static PcmModuleApi candidate;
static int candidate_valid;
static void (*tick_target)(void);
static irq_handler_fn irq_target;
static unsigned int irq_line;

#ifdef TICK_PROBE
#include "platform_pic.h"
#include "kernel_tick.h"
extern int res_owner_get(void);
static int diag_window;
static irq_handler_fn registered_handler;
static volatile unsigned diag_ack;

static int pcm_diag_irq(unsigned irq, void *arg)
{
    int playing = candidate.state() == PCM_ST_RUNNING;
    int rc = irq_target(irq, arg);
    if (playing && rc == IRQ_HANDLED && !(inp(PCM_PORT_R2) & 1)) ++diag_ack;
    return rc;
}
#endif

static const Snd2Stream *refill_stream;

/* Earliest possible boundary from a verified DMA movement. A PIT IRQ
 * count cannot bound elapsed time while interrupts were masked. */
static unsigned long long refill_due_us;

static void pcm_between_jiffies(void)
{
    const Snd2Stream *s = refill_stream;
    u32 lo, hi, previous = s->last_hw;
    int timed = pc98_tsc_read_us(&lo, &hi) == 0;
    unsigned long long now = timed ? ((unsigned long long)hi << 32) | lo : 0;
    if (s->written == s->copied && timed && now < refill_due_us)
        return;

    refill_due_us = 0;
    tick_target();
    if (timed && refill_stream == s && s->last_hw != previous) {
        u32 remaining = s->period - (s->last_hw & (s->period - 1));
        /* Start the interval before sampling DMA. Even 44.1 kHz takes
         * over 22 us per frame; allow one frame already in progress. */
        refill_due_us = now + (remaining - 1) * 22U;
    }
}

/* The module tick reference pins tick_target. The registry retains the
 * stream until stop; detach also removes the hook before dropping the ref. */
void pcm_module_refill_tick(int dev)
{
    Snd2Device *d = dev >= 0 ? snd2_device((u32)dev) : 0;
    refill_due_us = 0;
    refill_stream = d ? &d->stream : 0;
    tick_pcm_set(refill_stream && tick_target ? pcm_between_jiffies : 0);
}

void pcm_tick(void)
{
    if (tick_target) tick_target();
#ifdef PCM_E4_DIAG
    pcm_e4_tick();
#endif
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

int pcm_module_irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags)
{
    struct module_record *r = module_record_get(pcm_handle);
    if (arg || irq_target || !r) return IRQ_ERR_INVAL;
    unsigned int saved = irq_save();
    int rc = IRQ_ERR_BUSY;
    if (module_ref_acquire(pcm_handle, r->owner, MODULE_REF_IRQ, (u32)(uptr)fn)) {
#ifdef PCM_E4_DIAG
        pcm_e4_attach(fn);
        rc = irq_register(irq, pcm_e4_irq, arg, flags);
#elif defined(TICK_PROBE)
        registered_handler = diag_window ? pcm_diag_irq : fn;
        rc = irq_register(irq, registered_handler, arg, flags);
#else
        rc = irq_register(irq, fn, arg, flags);
#endif
        if (rc) module_ref_release(pcm_handle, MODULE_REF_IRQ);
        else { irq_line = irq; irq_target = fn; }
    }
    irq_restore(saved);
    return rc;
}

int pcm_module_irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg)
{
    if (arg || !irq_target || irq_line != irq || irq_target != fn) return IRQ_ERR_INVAL;
    unsigned int saved = irq_save();
#ifdef PCM_E4_DIAG
    int rc = irq_unregister(irq, pcm_e4_irq, arg);
#elif defined(TICK_PROBE)
    int rc = irq_unregister(irq, registered_handler, arg);
#else
    int rc = irq_unregister(irq, fn, arg);
#endif
    if (!rc) {
        irq_target = 0;
        irq_line = 0;
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
        if (!module_text_contains(r, entries[i]))
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
        pcm_module_refill_tick(-1);
        tick_target = 0;
        module_ref_release(pcm_handle, MODULE_REF_TICK);
    }
    int rc = !irq_target || !pcm_module_irq_unregister(irq_line, irq_target, 0);
    irq_restore(flags);
    return rc;
}

static int pcm_stop_dma(struct module_record *r)
{
    return r->dma_refs == 0;
}

static int bios_route = -1;
static int route_sampled;
static unsigned int pcm_irq_line;
static int pcm_quarantined;

unsigned int pcm_module_irq_line(void) { return pcm_irq_line; }

int pcm_module_bios_route(void) { return bios_route; }

int pcm_module_dma_acquire(const void *owner)
{
    return dma_chan_acquire(PCM_DMA_CHAN, owner);
}

void pcm_module_dma_release(const void *owner, int quarantine)
{
    if (quarantine) {
        dma_chan_quarantine(PCM_DMA_CHAN, owner);
        pcm_quarantined = 1;
    } else {
        dma_chan_release(PCM_DMA_CHAN, owner);
    }
}

/* A460h ID 6 identifies the Nf/Np built-in sound family. DMA #1 is
 * unavailable on Np (PC-98 io_sound, A460h and 0F40h). */
static const char *pcm_boot_refusal(void)
{
    if (pcm_quarantined) return "DMA #1 quarantined";
    if (!route_sampled) {
        pcm_irq_line = platform_irq_reserved(PCM_IRQ) ? PCM_IRQ_INT41 : PCM_IRQ;
        route_sampled = 1;
        if (((u8)inp(PCM_PORT_WSSID) & PCM_WSSID_MASK) == PCM_WSSID_VALUE)
            bios_route = (u8)inp(PCM_PORT_ROUTE);
    }
    if (bios_route < 0 || bios_route == 0xff) return "WSS unavailable";
    if (pcm_irq_line == PCM_IRQ && (bios_route & 0x38) != 0x20) return "BIOS WSS IRQ is not INT5";
    if ((bios_route & 7) > 3 && (bios_route & 7) != 7) return "reserved WSS DMA code";
    if (((u8)inp(0xa460) >> 4) == 6) return "Nf/Np sound family";
    return 0;
}

int pcm_module_load(void)
{
    const char *reason = pcm_boot_refusal();
    if (reason) {
        kprintf(KLOG_ATTR, "[pcm] skipped: %s (0F40h=%x)\n", reason, bios_route);
        return MODULE_INIT_FAILED;
    }
    if (tick_register(PC98_TICK_PCM, pcm_tick) != 0) return MODULE_INIT_FAILED;
    if (pcm_handle.generation) return MODULE_BUSY;
    u32 pair = 0;
    int pair_valid = platform_module_pair(&pair);
    const struct module_request request = {
        .path = SYS_PCM_MOD, .name = "pcm",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, PCM_MODULE_ABI_GEN,
            pair, PCM_MODULE_OPS_VERSION, pair_valid,
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
    kprintf(KLOG_ATTR, "[pcm] BIOS 0F40h=%x irq %u (%s) dma 1 module rc=%d pair=%08x tick=%d\n",
            bios_route, pcm_irq_line, pcm_irq_line == PCM_IRQ ? "FM absent, BIOS INT5" :
            "FM present or unknown, INT41", rc, pair, tick_target != 0);
    return rc;
}

int pcm_module_abort(void) { return module_abort(pcm_handle); }

#ifdef TICK_PROBE
/* The released line means both FM banks were absent at boot. The module
 * alone owns the WSS IRQ and DMA during this foreground window. */
int pcm_diag_wss_irq(void)
{
    if (res_owner_get() <= 0) return OS32_ERR_INVAL;
    int idx = irq_dyn_index(PCM_IRQ);
    if (idx < 0 || pcm_irq_line != PCM_IRQ || !module_exports(pcm_handle))
        return OS32_ERR_NOTFOUND;
    if (irq_lines[idx].count || irq_lines[idx].quarantined ||
        irq_lines[idx].storm_masked || pcm_quarantined || pcm_state() != PCM_ST_CLOSED)
        return OS32_ERR_BUSY;
    if ((u8)inp(PCM_PORT_ROUTE) != bios_route) return OS32_ERR_IO;
    unsigned bit = 1u << (PCM_IRQ & PIC_IRQ_MASK);
    unsigned masked = inp(PIC2_DATA) & bit;
    diag_ack = 0;
    diag_window = 1;
    int rc = candidate.open(PCM_RATE_44100);
    if (!rc) {
        static const u32 silence[64];
        for (unsigned frames = 0; frames < PCM_RING_FRAMES; frames += 64) {
            if (candidate.write(silence, sizeof(silence)) != sizeof(silence)) {
                rc = OS32_ERR_IO;
                break;
            }
        }
        u32 begin = tick_count;
        while (!rc && !diag_ack && tick_count - begin < 2 * PIT_HZ) _halt();
        int observed = diag_ack != 0;
        int closed = candidate.close();
        if (closed) rc = closed;
        else if (!observed) rc = OS32_ERR_IO;
    }
    unsigned flags = irq_save();
    if (pcm_quarantined || irq_lines[idx].count || pcm_state() != PCM_ST_CLOSED ||
        (u8)inp(PCM_PORT_ROUTE) != bios_route) {
        irq_quarantine_line(PCM_IRQ);
        rc = OS32_ERR_IO;
    } else if (!irq_lines[idx].storm_masked && !irq_lines[idx].quarantined) {
        if ((inp(PIC2_DATA) & bit) != masked) {
            if (masked) platform_irq_mask(PCM_IRQ);
            else platform_irq_unmask(PCM_IRQ);
        }
        if ((inp(PIC2_DATA) & bit) != masked) {
            irq_quarantine_line(PCM_IRQ);
            rc = OS32_ERR_IO;
        }
    } else {
        irq_quarantine_line(PCM_IRQ);
        rc = OS32_ERR_IO;
    }
    diag_window = 0;
    irq_restore(flags);
    return rc;
}
#endif
