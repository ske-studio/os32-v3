/* GeForce layer 1 internal ABI; no change to public video_ops. */
#ifndef HWDEV_GF_FOUNDATION_H
#define HWDEV_GF_FOUNDATION_H
#include "internal.h"

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
/* Natural u32 wrap; vblank counts observations, not all elapsed frames. */
struct gf_irq_stats {
    u32 entries, fifo, graph, vblank[2];
    u32 pmc, pfifo, pgraph, nsource, nstatus, unknown;
};
struct gf_foundation {
    struct gf_io io;
    struct gf_imem imem;
    struct gf_irq_stats stats;
    volatile u8 *bar0, *bar2;
    u32 wanted_fifo, wanted_graph, wanted_heads; /* requested IRQ masks */
    u32 expected_pmc;
    u16 expected_command;
    u8 irq_line;
    bool bm, mapped, registered, running;
    int last_rc;
};
/* Production owns the adapter and owner; fake MMIO replaces this contract.
 * map reserves then maps UC, rolling back internally on failure. mapped is
 * set only after success. unmap removes the mapping then the reservation. */
struct gf_instmem_map_ops {
    int (*map)(void *owner, const struct recover_bar_extent *bar,
               u32 bytes, volatile u8 **out);
    int (*unmap)(void *owner, volatile u8 *ptr, u32 bytes);
};
int gf_master_enable(struct gf_foundation *); /* cfg16/8, enter RUN every start */
int gf_master_disable(struct gf_foundation *); /* only after engine stop */
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
#endif
