/* GeForce layer 1 internal ABI; no change to public video_ops. */
#ifndef HWDEV_GF_FOUNDATION_H
#define HWDEV_GF_FOUNDATION_H
#include "internal.h"
#include "vram_layout.h"

enum gf1_rc {
    GF1_OK=0, GF1_EPCI=-101, GF1_EMAP=-102, GF1_ENOSPC=-103,
    GF1_EINVAL=-104, GF1_EIRQ=-105, GF1_ESTOP=-106, GF1_EIO=-107
};
/* Caller-owned and immovable until free. off is aperture-relative, not V-R. */
struct gf_inst { u32 off, size; struct gf_inst *next; };
struct gf_imem {
    u32 vram_bytes, reserved, aperture_bytes;
    struct gf_inst vbios, ramht, ramro, ramfc, *used;
};
/* Small pre-ACK record, one latch per engine observation period. */
struct gf_irq_first {
    u32 pmc, pfifo, pgraph, nsource, nstatus;
    u32 gr_trap, gr_method, gr_data, gr_class;
    bool fifo_latched, graph_latched;
};
/* Natural u32 wrap; vblank counts observations, not all elapsed frames. */
struct gf_irq_stats {
    u32 entries, fifo, graph, vblank[2];
    u32 pmc, pfifo, pgraph, nsource, nstatus, unknown;
    /* Survives clean polls and head changes. Re-enabling an engine clears
     * that engine's latch; latest observations above retain their meaning. */
    struct gf_irq_first first;
};
struct gf_instmem_map_ops;
struct gf_foundation {
    struct gf_io io;
    /* Production lends these before start; callbacks and owner stay valid
     * through stop/rollback and successful map release, including failures.
     * Independent of io.opaque; fake MMIO lends its own map_ops/owner. */
    const struct gf_instmem_map_ops *map_ops;
    void *map_owner;
    struct recover_bar_extent instmem_bar; /* Checked third memory resource, copied before start. */
    struct gf_imem imem;
    struct gf_irq_stats stats;
    volatile u8 *bar0, *bar2;
    u32 wanted_fifo, wanted_graph, wanted_heads; /* requested IRQ masks */
    u32 expected_pmc;
    u16 expected_command;
    u8 irq_line;
    bool bm, mapped, registered, running;
    int last_rc;
    bool revoked; /* sticky until reboot; not a successful hardware stop */
};
/* Production owns adapter/owner; fake MMIO supplies the same contract.
 * BAR reservation is permanent from boot. map only maps UC; failure leaves no new map.
 * mapped is set only on success. unmap removes access, never the BAR reservation. */
struct gf_instmem_map_ops {
    int (*map)(void *owner, const struct recover_bar_extent *bar,
               u32 bytes, volatile u8 **out);
    int (*unmap)(void *owner, volatile u8 *ptr, u32 bytes);
};
extern const struct gf_instmem_map_ops gf_production_instmem_map_ops;
int gf_master_enable(struct gf_foundation *); /* cfg16/8, enter RUN every start */
int gf_master_disable(struct gf_foundation *); /* only after engine stop */
/* B4: BIOS/display init complete, IRQ sources closed. Uses f->io for the
 * nv44 MC sequence and updates expected_pmc. GF1_OK or GF1_EIO; no owned
 * mapping/allocation to release. Caller rolls back start via foundation_stop. */
int gf_mc_init(struct gf_foundation *);
/* B5: bar is the checked third memory resource (borrowed for this call),
 * bytes is R, its leading UC aperture size. Uses f->map_ops/map_owner.
 * GF1_OK publishes bar2/aperture_bytes and mapped=true; GF1_EMAP leaves no new map.
 * Foundation owns the successful map; the permanent boot reservation is unchanged. */
int gf_instmem_map(struct gf_foundation *, const struct recover_bar_extent *bar,
                   u32 bytes);
/* After engine stop, IRQ detach and BM clear: release variable handles,
 * then fixed regions, before this call. Uses the same borrowed adapter and
 * bar2/aperture_bytes; GF1_OK clears mapped/bar2, GF1_EMAP retains ownership
 * needed for cleanup. foundation_stop owns this release, not the caller. */
int gf_instmem_unmap(struct gf_foundation *);
int gf_instmem_init(struct gf_foundation *);
/* Foreground only; align=0 means 1. Fixed handles are not individually freed. */
int gf_inst_alloc(struct gf_foundation *, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out);
void gf_inst_free(struct gf_foundation *, struct gf_inst *);
int gf_inst_read32(struct gf_foundation *, const struct gf_inst *, u32 off, u32 *);
int gf_inst_write32(struct gf_foundation *, const struct gf_inst *, u32 off, u32);
int gf_irq_attach(struct gf_foundation *); /* shared IRQ, sources still closed */
int gf_irq_enable(struct gf_foundation *, bool fifo, bool graph, unsigned heads);
int gf_irq_handler(unsigned irq, void *arg); /* irq_handler_fn */
void gf_irq_snapshot(struct gf_foundation *, struct gf_irq_stats *);
int gf_irq_detach(struct gf_foundation *);
/* IRQ0: one pass whenever running; no wait, allocation or printf. */
void gf_irq_poll(struct gf_foundation *);
void gf_tick_poll(void *arg); /* module callback connects to gf_irq_poll */
int gf_engine_stop(struct gf_foundation *); /* reset pulses, no submission restart */
/* Start: master/MC/instmem/bind/attach, then running and ACK/enable under CLI.
 * Stop (also rollback): running=false/unbind/mask, engine stop, unregister,
 * BM clear, map release. A repeated stopped stop succeeds. Errors retain the
 * resources required for safe cleanup. last_rc preserves the gf1_rc detail;
 * display owner adapters translate to the existing 0/-1 video contract. */
int gf_foundation_start(struct gf_foundation *);
int gf_foundation_stop(struct gf_foundation *);
void gf_foundation_revoke(struct gf_foundation *);
#endif
