/* GeForce layer 2 internal ABI. Canonical values and sequencing:
 * docs/decisions/realhw/GF_FEAT2_DESIGN.md sections 2-7 and final decisions.
 * Foreground only, one cooperative owner; never submit from IRQ/tick or
 * reenter the channel from a wait. Production owns one immovable instance.
 * Component-local register aliases stay with their implementation. */
#ifndef HWDEV_GF_COMMAND_H
#define HWDEV_GF_COMMAND_H
#include "foundation.h"
#include "../../include/memmap.h"

enum gf2_rc {
    GF2_OK=0, GF2_EINVAL=-201, GF2_ENOSPC=-202, GF2_EMAP=-203,
    GF2_EIO=-204, GF2_ETIME=-205, GF2_EFAULT=-206, GF2_ESTATE=-207,
    GF2_EEXIST=-208, GF2_ECLASS=-209, GF2_ESTOP=-210
};
enum gf2_state { GF2_OFF, GF2_INIT, GF2_RUN, GF2_DRAIN, GF2_FAULT };

/* Section 2: one NV40 DMA channel, last chid reserved as invalid. */
#define GF2_CHID 0U
#define GF2_CHID_INVALID 31U
#define GF2_CHANNEL_CLASS 0x406eU
#define GF2_RAMFC_BYTES 128U
#define GF2_USER_BASE 0x00c00000U
#define GF2_USER_STRIDE 0x1000U
#define GF2_USER_PUT 0x40U
#define GF2_USER_GET 0x44U
#define GF2_USER_REFERENCE 0x48U

/* Section 3: bytes in VRAM, words in cur/put/free/max. Reserve dynamically
 * below V-R; no new fixed general-memory reservation in memmap.h. */
#define GF2_PUSH_WORDS (GF2_PUSH_BYTES / sizeof(u32))
#define GF2_PUSH_MAX (GF2_PUSH_WORDS - 2U)
#define GF2_PUSH_SKIPS 32U
#define GF2_PUSH_MIN_BASE \
    ((GF_MODE_VRAM_BYTES + GF2_PUSH_ALIGN - 1U) & ~(GF2_PUSH_ALIGN - 1U))
#define GF2_PUSH_JUMP 0x20000000U
#define GF2_PUSH_NONINC 0x40000000U
#define GF2_METHOD_MAX 0x1ffcU
#define GF2_METHOD_COUNT_MAX 2047U
#define GF2_SUBCHANNEL_MAX 7U
#define GF2_METHOD_COUNT_SHIFT 18U
#define GF2_METHOD_SUBCHANNEL_SHIFT 13U

/* Section 4: target/access are logical API selectors, not descriptor flags.
 * B2 translates these to VRAM/PCI and RO/WO/RW hardware encodings. */
#define GF2_DMA_TARGET_VRAM 0U
#define GF2_DMA_TARGET_PCI 1U
#define GF2_DMA_ACCESS_RO 0U
#define GF2_DMA_ACCESS_WO 1U
#define GF2_DMA_ACCESS_RW 2U
#define GF2_DMA_FROM_MEMORY 0x0002U
#define GF2_DMA_TO_MEMORY 0x0003U
#define GF2_DMA_IN_MEMORY 0x003dU
#define GF2_DMA_OBJECT_BYTES 16U
#define GF2_OBJECT_ALIGN 16U
#define GF2_RAMHT_BITS 12U
#define GF2_RAMHT_ENTRIES (1U << GF2_RAMHT_BITS)
#define GF2_RAMHT_ENTRY_BYTES 8U
#define GF2_RAMHT_BITMAP_BYTES (GF2_RAMHT_ENTRIES / 8U)
#define GF2_RAMHT_ENGINE_DMA 0U
#define GF2_RAMHT_ENGINE_GR 1U
#define GF2_HANDLE_PUSH_DMA 0x80000001U
#define GF2_HANDLE_VRAM_DMA 0x80000002U
#define GF2_HANDLE_PCI_DMA 0x80000003U
#define GF2_HANDLE_LAYER3_FIRST 0x80000100U
#define GF2_STAGING_BYTES MEM_PAGE_SIZE
#define GF2_STAGING_ALIGN MEM_PAGE_SIZE
#define GF2_STAGING_MASK U32_MAX

/* Section 5: complete NV44 class list, including later 3D consumers. */
#define GF2_GR_BETA1 0x0012U
#define GF2_GR_CLIP 0x0019U
#define GF2_GR_NULL 0x0030U
#define GF2_GR_M2MF 0x0039U
#define GF2_GR_ROP 0x0043U
#define GF2_GR_PATTERN 0x0044U
#define GF2_GR_GDI 0x004aU
#define GF2_GR_SURFACE2D 0x0062U
#define GF2_GR_SURFACE2D_NV30 0x3062U
#define GF2_GR_BETA4 0x0072U
#define GF2_GR_SIFM 0x0089U
#define GF2_GR_SIFM_NV30 0x3089U
#define GF2_GR_IFC 0x008aU
#define GF2_GR_IMAGEBLIT 0x009fU
#define GF2_GR_SWZSURF 0x309eU
#define GF2_GR_CURIE 0x4497U
#define GF2_GR_OBJECT_BYTES 20U
#define GF2_GRCTX_PROGRAM_WORDS 256U
#define GF2_GRCTX_LABEL_WORDS 32U
#define GF2_GR_TILE_COUNT 12U

/* Section 6: one absolute deadline per operation, never renewed on progress.
 * timeout and outstanding sequence distance must each be below 2^31.
 * Compare with (s32)(now-deadline)>=0 / (s32)(observed-seq)>=0. */
#define GF2_TIMEOUT_US 2000000U
#define GF2_WAIT_MIN_US 1U
#define GF2_WAIT_MAX_US 50U
#define GF2_REFERENCE_SUBCHANNEL 0U
#define GF2_REFERENCE_METHOD 0x0050U

struct gf2_fence { u32 epoch, seq; };
/* inst is linked by address in the foundation allocator: never copy/move a
 * live object. cookie=slot+1; push DMA has no RAMHT entry. */
struct gf2_object {
    u32 handle, cookie;
    struct gf_inst inst;
    u16 class_id;
};
struct gf2_dma_buf { u32 pa, size; void *va; };
/* B9 resident adapter converts dma_buf without importing the pool ABI.
 * alloc/free return the raw pool rc. On failed free, call leak; do not reuse
 * memory that may still be device-owned. The table lives until reboot. */
struct gf2_dma_ops {
    int (*alloc)(u32 bytes, u32 align, u32 mask, struct gf2_dma_buf *);
    int (*free)(struct gf2_dma_buf *);
    void (*leak)(struct gf2_dma_buf *);
};
struct gf2_fault {
    bool latched;
    u32 epoch, chid, put, get, reference;
    /* first uses F1's pre-ACK status/trap latch and foreground counters;
     * latest aggregates later counts and OR masks. */
    struct gf_irq_stats first, latest;
    u32 caches, push1, cache_get, pull0, method, data, dma_state, dma_fetch;
    u32 dma_push, dma_dcount, gr_trap, gr_method, gr_data, gr_class;
};
struct gf2_chan {
    struct gf_foundation *f;
    enum gf2_state state;
    unsigned heads;
    /* B is an absolute VRAM byte offset; va is the UC push mapping.
     * Inst offsets, VRAM offsets and staging physical addresses differ. */
    u32 B, cur, put, free, max, seq, epoch;
    volatile u32 *va;
    struct gf2_fault fault;
    struct gf_irq_stats seen;
    u8 bitmap[GF2_RAMHT_BITMAP_BYTES];
    struct gf2_object push_dma, vram_dma, pci_dma;
    struct gf_inst grctx;
    /* Borrowed adapters/owner and checked BAR1 snapshot outlive successful
     * stop, including failed cleanup. push_extent is the partial BAR1 map. */
    const struct gf_instmem_map_ops *map_ops;
    void *map_owner;
    struct recover_bar_extent bar1, push_extent;
    u32 map_bytes;
    bool mapped, vram_reserved, staging_live;
    /* Transfer owns this until a later reference completes; retain on error
     * and across F3 reinitialization, since staging belongs to F2. */
    bool staging_pending;
    int dma_rc, last_rc;
    const struct gf2_dma_ops *dma_ops;
    struct gf2_dma_buf staging;
};

/* B0 / fifo.c, final decisions: FB init precedes PCI DMA descriptors and
 * follows the closed FIFO gates. IRQ ACK/enable always belongs to F1. */
int  gf2_fb_init(struct gf2_chan *);
int  gf2_fifo_init(struct gf2_chan *);
int  gf2_fifo_bind_gr(struct gf2_chan *, u32 ctx_off);
int  gf2_fifo_start(struct gf2_chan *);
void gf2_fifo_stop(struct gf2_chan *);
int  gf2_fifo_pause(struct gf2_chan *);
void gf2_fifo_resume(struct gf2_chan *);
/* bind_gr receives ctx.off, not an absolute VRAM address. pause requires a
 * drained channel; timeout leaves gates closed. resume: PULL0 then CACHES.
 * stop saves/clears the complete NV40 RAMFC layout, selects invalid chid,
 * clears MODE; the lifecycle owner follows it with the common stop. */

/* B1 / push.c. Reserve count+1 contiguous words before writing a packet.
 * count=1..2047, subc<=7, method aligned to 4 and <=1ffc; incrementing
 * packets must also end <=1ffc. Invalid input EINVAL, non-RUN ESTATE;
 * INIT/DRAIN reference goes through gf2_push_reference. Bind: method=0, one object.handle word.
 * kick drains posted writes by reading va[0] before PUT=B+4*cur. Neither
 * emit nor kick proves completion; a fault prevents any further kick. */
int gf2_emit(struct gf2_chan *, u8 subc, u16 method, bool noninc,
             const u32 *data, u16 count, u32 deadline_us);
int gf2_push_reference(struct gf2_chan *, u32 seq, u32 deadline_us);
int gf2_kick(struct gf2_chan *);

/* B2 / dmaobj.c. Inclusive [start,limit], class FROM/TO/IN_MEMORY.
 * Register/delete only during INIT or after fence drain + FIFO pause.
 * Push DMA is RAMFC-only; other DMA objects use RAMHT engine 0.
 * object_del removes RAMHT before inst free; caller ensures quiescence. */
int gf2_dma_new(struct gf2_chan *, u32 handle, u16 class_id,
                u8 target, u8 access, u32 start, u32 limit, struct gf2_object *);
int gf2_object_del(struct gf2_chan *, struct gf2_object *);

/* B3 / ramht.c. inst_off is aperture-relative. cookie=slot+1.
 * Deletion changes table/bitmap only; caller owns instance free. Duplicate
 * handle EEXIST, full table ENOSPC. No NV04 bit31 valid encoding. */
int gf2_ramht_add(struct gf2_chan *, u32 handle, u8 engine, u32 inst_off,
                  u32 *cookie);
int gf2_ramht_del(struct gf2_chan *, u32 cookie);

/* B4 / grctx.c. Generate only: caller owns code storage, upload/allocation
 * and bind. PROG/VALS have the same final byte size; no embedded ctx image.
 * Scratch is private to B4 and reusable after initialization. */
int gf2_grctx_program(u32 code[GF2_GRCTX_PROGRAM_WORDS], u32 *words, u32 *bytes);
int gf2_grctx_fill(struct gf_foundation *, const struct gf_inst *ctx);

/* B5 / gr.c. Init owns ctx allocation, fill, ctx[0], publish and FIFO bind;
 * failures free in reverse order. GR objects use engine 1; ECLASS rejects
 * classes outside the complete list above. Deletion is the B2 common API. */
int gf2_gr_init(struct gf2_chan *);
int gf2_gr_object_new(struct gf2_chan *, u32 handle, u16 class_id,
                      struct gf2_object *);

/* B6 / fence.c. ++seq then subc0/method0050 and kick. Wait checks fault
 * before REFERENCE success; GET==PUT never means graphics completion.
 * Old epoch EFAULT, expired absolute deadline ETIME, failed clock EIO. */
int gf2_fence_emit(struct gf2_chan *, struct gf2_fence *out, u32 deadline_us);
int gf2_fence_wait(struct gf2_chan *, struct gf2_fence, u32 deadline_us);
int gf2_fence_poll(struct gf2_chan *, struct gf2_fence, int *done);

/* B7 / fault.c. Foreground API entry, each wait iteration and before kick:
 * snapshot FIFO/GR count differences, capture first fault, close IRQ requests
 * (preserve heads), close gates, FAULT. No repair/replay/IRQ ACK here. */
int gf2_fault_check(struct gf2_chan *); /* OK or EFAULT */

/* B8 / command_platform.c. off/len are 4-byte multiples; checked against
 * BAR1 length and addition overflow. Map only the partial push extent via
 * map_ops, UC; the u8* to u32* conversion belongs to this one boundary.
 * Failed map/unmap retains owned state. dma_get requests one physical page,
 * saves raw dma_rc; pool exhaustion ENOSPC leaves VRAM operation possible.
 * dma_put is allowed only after quiescence; free failure invokes leak. */
int gf2_map_vram(struct gf2_chan *, u32 off, u32 len, volatile u32 **va);
int gf2_unmap_vram(struct gf2_chan *);
int gf2_dma_get(struct gf2_chan *);
void gf2_dma_put(struct gf2_chan *);

/* B9 / drivers/video/geforce/geforce_dma.c: one typed resident import. */
const struct gf2_dma_ops *geforce_dma_ops(void);

/* C0 / command.c. Start follows an already running F1. Stop drains, stops
 * FIFO, closes gates/requests, then engine_stop before freeing variable
 * instances; F1 stop precedes staging/map release. Failed stop retains
 * remaining ownership, returns ESTOP and forbids detach. Repeated stop is
 * idempotent. Explicit recover retries failed stop, restarts F1, rebuilds
 * all state and increments epoch; old handles/fences must be recreated.
 * Reset never counts as completion and never causes automatic replay. */
int gf2_start(struct gf2_chan *, struct gf_foundation *);
int gf2_stop(struct gf2_chan *);
int gf2_recover(struct gf2_chan *);
#endif
