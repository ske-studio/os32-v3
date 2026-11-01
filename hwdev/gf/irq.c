/* Layer 1 records/ACKs only: no channel repair, GET advance or GR restart.
 * References: MIT nouveau c03c1a14507dfaf8b856c2608f9ac216021fc222,
 * paths relative to drivers/gpu/drm/nouveau/. Foundation io is the borrowed
 * permanent MMIO adapter (§2): no PCI, window changes, waits or IRQ logging.
 */
#include "foundation.h"
#include "../../kernel/irq/irq.h"
#include "../../include/io.h"
#include "../../drivers/pci/pci.h"
#include "../../lib/serial_diag.h"

#define GF_IRQ_LINE 6U
#define GF_IRQ_ALL 0xffffffffU
#define GF_IRQ_PMC_STATUS 0x000100U
#define GF_IRQ_PMC_ENABLE 0x000140U
#define GF_IRQ_FIFO_STATUS 0x002100U
#define GF_IRQ_FIFO_ENABLE 0x002140U
#define GF_IRQ_GRAPH_STATUS 0x400100U
#define GF_IRQ_GRAPH_ENABLE 0x40013cU
#define GF_IRQ_GRAPH_NSOURCE 0x400108U
#define GF_IRQ_GRAPH_NSTATUS 0x400104U
#define GF_IRQ_CRTC_STATUS 0x600100U
#define GF_IRQ_CRTC_ENABLE 0x600140U
#define GF_IRQ_HEAD_STRIDE RECOVER_HEAD_STRIDE
#define GF_IRQ_HEADS 2U
#define GF_IRQ_VBLANK 1U
#define GF_IRQ_PMC_FIFO 0x00000100U
#define GF_IRQ_PMC_GRAPH 0x00001000U
#define GF_IRQ_PMC_VIDEO 0x00010000U
#define GF_IRQ_PMC_DISPLAY 0x03010000U
#define GF_IRQ_PMC_KNOWN (GF_IRQ_PMC_FIFO | GF_IRQ_PMC_GRAPH | GF_IRQ_PMC_DISPLAY)
/* PVIDEO, PTIMER, PBUS, PMPEG: never enabled in layer 1.
 * dispnv04/hw.c:665 nv_load_state_ext (:683 PVIDEO mask),
 * dispnv04/nvreg.h:468 NV_PVIDEO_INTR_EN. */
static const u32 unused_status[] = {0x008100U, 0x009100U, 0x001100U, 0x00b100U};
#define GF_IRQ_ENABLE_DELTA 0x40U
#define GF_IRQ_UNUSED_COUNT (sizeof(unused_status) / sizeof(unused_status[0]))

static int rd(struct gf_foundation *f, u32 off, u32 *v)
{ return f->io.read(f->io.opaque, GF_MODE_REGISTER_BAR, off, sizeof(*v), v); }
static int wr(struct gf_foundation *f, u32 off, u32 v)
{ return f->io.write(f->io.opaque, GF_MODE_REGISTER_BAR, off, sizeof(v), v); }

/* nvkm/subdev/mc/nv04.c:82 nv04_mc_intr_unarm (write then flush). */
static int unarm(struct gf_foundation *f)
{
    u32 v;
    if (wr(f, GF_IRQ_PMC_ENABLE, 0) || rd(f, GF_IRQ_PMC_ENABLE, &v)) return -1;
    return 0;
}

/* nvkm/engine/mpeg/nv31.c:224 nv31_mpeg_init, :240-241 ACK/enable;
 * nvkm/subdev/bus/nv04.c:57 nv04_bus_init, :60-61 ACK/enable;
 * nvkm/subdev/timer/nv04.c:57 nv04_timer_alarm_fini, :72 nv04_timer_intr.
 * Unused sources stay masked; their W1C status is drained in one pass.
 */
static int unused_drain(struct gf_foundation *f, bool initial, u32 *left)
{
    for (u32 i = 0; i < GF_IRQ_UNUSED_COUNT; i++) {
        u32 status;
        if (wr(f, unused_status[i] + GF_IRQ_ENABLE_DELTA, 0) ||
            rd(f, unused_status[i], &status) ||
            wr(f, unused_status[i], initial ? GF_IRQ_ALL : status) ||
            rd(f, unused_status[i], &status)) return -1;
        *left |= status;
    }
    return 0;
}

/* Foreground diagnostics use the serial facade, outside the IRQ paths. */
static void log_irq(const char *op, u32 line, int rc)
{
    if (serial_gate_active()) return;
    serial_puts_polled("[gf1] PCI IRQ="); serial_diag_u32(line);
    serial_puts_polled(op); serial_diag_i32(rc);
    serial_puts_polled("\r\n");
}

int gf_irq_attach(struct gf_foundation *f)
{
    u32 left = 0;
    if (unarm(f) || wr(f, GF_IRQ_FIFO_ENABLE, 0) ||
        wr(f, GF_IRQ_GRAPH_ENABLE, 0)) goto io_error;
    for (u32 h = 0; h < GF_IRQ_HEADS; h++)
        if (wr(f, GF_IRQ_CRTC_ENABLE + h * GF_IRQ_HEAD_STRIDE, 0)) goto io_error;
    if (unused_drain(f, true, &left)) goto io_error;
    f->irq_line = pci_cfg_read8(RECOVER_GF_BUS, RECOVER_GF_DEV,
                               RECOVER_GF_FN, PCI_CFG_INT_LINE);
    int rc = irq_register(GF_IRQ_LINE, gf_irq_handler, f, IRQ_F_SHARED);
    /* Foreground only: retain the original IRQ_ERR_* in the diagnostic. */
    log_irq(" register(6)=", f->irq_line, rc);
    if (rc) return f->last_rc = GF1_EIRQ;
    f->registered = true;
    return f->last_rc = GF1_OK;
io_error:
    return f->last_rc = GF1_EIO;
}

/* nvkm/engine/fifo/nv40.c:223-224 nv40_fifo_init;
 * nvkm/engine/gr/nv40.c:302-303 nv40_gr_init;
 * nvkm/engine/disp/nv04.c:37 nv04_head_vblank_get;
 * nvkm/subdev/mc/nv04.c:72 nv04_mc_intr_rearm.
 * Caller sets running and calls under irq_save/restore. Pre-enable PCRTC
 * ACK discards pending from the previous observation period (OS32 policy).
 */
int gf_irq_enable(struct gf_foundation *f, bool fifo, bool graph, unsigned heads)
{
    f->wanted_fifo = fifo ? GF_IRQ_ALL : 0;
    f->wanted_graph = graph ? GF_IRQ_ALL : 0;
    f->wanted_heads = heads;
    if (unarm(f) || wr(f, GF_IRQ_FIFO_STATUS, GF_IRQ_ALL) ||
        wr(f, GF_IRQ_FIFO_ENABLE, f->wanted_fifo) ||
        wr(f, GF_IRQ_GRAPH_STATUS, GF_IRQ_ALL) ||
        wr(f, GF_IRQ_GRAPH_ENABLE, f->wanted_graph)) goto io_error;
    for (u32 h = 0; h < GF_IRQ_HEADS; h++) {
        u32 stride = h * GF_IRQ_HEAD_STRIDE;
        if (wr(f, GF_IRQ_CRTC_STATUS + stride, GF_IRQ_VBLANK) ||
            wr(f, GF_IRQ_CRTC_ENABLE + stride, (heads >> h) & GF_IRQ_VBLANK))
            goto io_error;
    }
    if (wr(f, GF_IRQ_PMC_ENABLE, 1)) goto io_error;
    return f->last_rc = GF1_OK;
io_error:
    return f->last_rc = GF1_EIO;
}

/* nvkm/engine/fifo/nv04.c:406 nv04_fifo_intr (:411-412,455-461).
 * Poll uses requested masks to recover sources masked for residual status.
 */
static int fifo_drain(struct gf_foundation *f, bool poll, u32 *left)
{
    u32 mask, status, residual;
    if (poll) mask = f->wanted_fifo;
    else if (rd(f, GF_IRQ_FIFO_ENABLE, &mask)) return -1;
    if (rd(f, GF_IRQ_FIFO_STATUS, &status)) return -1;
    status &= mask;
    f->stats.pfifo = status;
    if (status) f->stats.fifo++;
    if (wr(f, GF_IRQ_FIFO_STATUS, status) ||
        rd(f, GF_IRQ_FIFO_STATUS, &residual)) return -1;
    residual &= poll ? f->wanted_fifo : mask;
    *left |= residual;
    return wr(f, GF_IRQ_FIFO_ENABLE, mask & ~residual);
}

/* nvkm/engine/gr/nv40.c:232 nv40_gr_intr (:238-240,267).
 * Do not write 400720h: channel execution restart belongs to layer 2.
 */
static int graph_drain(struct gf_foundation *f, bool poll, u32 *left)
{
    u32 status, mask, residual;
    if (rd(f, GF_IRQ_GRAPH_STATUS, &status) ||
        rd(f, GF_IRQ_GRAPH_NSOURCE, &f->stats.nsource) ||
        rd(f, GF_IRQ_GRAPH_NSTATUS, &f->stats.nstatus)) return -1;
    if (poll) status &= f->wanted_graph;
    f->stats.pgraph = status;
    if (status) f->stats.graph++;
    if (wr(f, GF_IRQ_GRAPH_STATUS, status) ||
        rd(f, GF_IRQ_GRAPH_STATUS, &residual)) return -1;
    if (poll) mask = f->wanted_graph;
    else if (rd(f, GF_IRQ_GRAPH_ENABLE, &mask)) return -1;
    residual &= poll ? f->wanted_graph : GF_IRQ_ALL;
    *left |= residual;
    return wr(f, GF_IRQ_GRAPH_ENABLE, mask & ~residual);
}

/* nvkm/engine/disp/nv04.c:80 nv04_disp_intr (:84-95): ACK both heads,
 * but count only enabled/requested heads. Unsigned counters naturally wrap.
 */
static int display_drain(struct gf_foundation *f, bool poll, u32 *left)
{
    for (u32 h = 0; h < GF_IRQ_HEADS; h++) {
        u32 stride = h * GF_IRQ_HEAD_STRIDE, status, enable, residual;
        if (rd(f, GF_IRQ_CRTC_STATUS + stride, &status)) return -1;
        if (poll) enable = (f->wanted_heads >> h) & GF_IRQ_VBLANK;
        else if (rd(f, GF_IRQ_CRTC_ENABLE + stride, &enable)) return -1;
        if (status & GF_IRQ_VBLANK) {
            if (enable & GF_IRQ_VBLANK) f->stats.vblank[h]++;
            if (wr(f, GF_IRQ_CRTC_STATUS + stride, GF_IRQ_VBLANK)) return -1;
        }
        if (rd(f, GF_IRQ_CRTC_STATUS + stride, &residual)) return -1;
        residual &= GF_IRQ_VBLANK;
        *left |= residual;
        if (wr(f, GF_IRQ_CRTC_ENABLE + stride, enable & ~residual)) return -1;
    }
    return 0;
}

/* nvkm/core/intr.c:164 nvkm_intr (:178-194,210-225): unarm, pending,
 * dispatch, rearm. One pass; residual/unknown stays closed for tick.
 * PMC status is read-only here: never guess an ACK for unknown bits.
 */
static int drain(struct gf_foundation *f, bool poll)
{
    u32 pmc, left = 0, after;
    if (unarm(f) || rd(f, GF_IRQ_PMC_STATUS, &pmc)) goto io_error;
    f->stats.pmc = pmc;
    f->stats.unknown |= pmc & ~GF_IRQ_PMC_KNOWN;
    if ((poll || (pmc & GF_IRQ_PMC_FIFO)) && fifo_drain(f, poll, &left)) goto io_error;
    if ((poll || (pmc & GF_IRQ_PMC_GRAPH)) && graph_drain(f, poll, &left)) goto io_error;
    if ((poll || (pmc & GF_IRQ_PMC_DISPLAY)) && display_drain(f, poll, &left)) goto io_error;
    if (poll) {
        if (unused_drain(f, false, &left)) goto io_error;
    } else if (pmc & GF_IRQ_PMC_VIDEO) {
        u32 status;
        if (rd(f, unused_status[0], &status) || wr(f, unused_status[0], status) ||
            rd(f, unused_status[0], &status)) goto io_error;
        left |= status;
    }
    if (rd(f, GF_IRQ_PMC_STATUS, &after)) goto io_error;
    f->stats.unknown |= after & ~GF_IRQ_PMC_KNOWN;
    if (left || after) return IRQ_DEFERRED;
    if (wr(f, GF_IRQ_PMC_ENABLE, 1)) goto io_error; /* gf1 rearm anchor */
    return pmc || poll ? IRQ_HANDLED : IRQ_NONE;
io_error:
    f->last_rc = GF1_EIO;
    return IRQ_DEFERRED;
}

int gf_irq_handler(unsigned irq, void *arg)
{
    struct gf_foundation *f = arg;
    (void)irq;
    if (!f->running) return IRQ_NONE;
    f->stats.entries++;
    return drain(f, false);
}
void gf_irq_poll(struct gf_foundation *f)
{ if (f->running) (void)drain(f, true); }
void gf_tick_poll(void *arg)
{ gf_irq_poll(arg); }

void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *out)
{
    unsigned saved = irq_save();
    *out = f->stats;
    irq_restore(saved);
}

/* Caller has stopped/unbound tick, closed/ACKed sources and stopped engines
 * before detach (§3). Failure retains ownership needed for cleanup.
 */
int gf_irq_detach(struct gf_foundation *f)
{
    if (!f->registered) return GF1_OK;
    int rc = irq_unregister(GF_IRQ_LINE, gf_irq_handler, f);
    log_irq(" unregister(6)=", f->irq_line, rc);
    if (rc) return f->last_rc = GF1_EIRQ;
    f->registered = false;
    return f->last_rc = GF1_OK;
}
