/* libc-free host seam, TRIDENT_S3_DESIGN §5. No host-only product hooks. */
#ifndef TRIDENT_DEVICE_FAKE_H
#define TRIDENT_DEVICE_FAKE_H
#include "drivers/video/trident/trident.h"
#define TRI_FAKE_BLOCKED_WORD 0xFA7F3F6Fu
#define TRI_FAKE_REAL_BYTES 0x00200000u
/* Trace records an actual access, never two synthetic bytes for OUTW.
 * width is bytes (1/2/4), PORT uses space=2. Failed access has its own rc. */
#define TRI_FAKE_PORT_SPACE 2u
struct trident_fake_event {
    u32 space, offset, value;
    u32 count; /* elements: 1 for scalar, dword count for one span event */
    u8 width;
    bool write;
    int rc;
};
struct trident_fake {
    struct trident_resources resources;
    /* Inferred VGA decode: MISC bit0 selects color(1)/mono(0); inactive
     * index/data/status read FF, ignore writes and do not reset AR flip. */
    u8 regs[TRI_BAR1_DECODE_BYTES];
    u8 sr[256], cr_color[256], cr_mono[256], gr[256], ar[32];
    u8 sr_index, color_index, mono_index, gr_index, ar_index;
    bool ar_data;
    u8 dac[TRI_DAC_BYTES], hidden_dac, ext_dac[256], ext_index;
    u32 dac_read, dac_write, hidden_reads;
    bool dac_readable;
    u8 *vram;
    u32 storage_bytes, real_bytes;
    enum trident_upper upper;
    bool block_after_restore; /* inferred failure mode, reset default off */
    bool pc98_initialized; /* false at reset, never inferred from ModeInit */
    /* Failure: one-based operation number; 0 disables. Failed access has no
     * side effect; returned rc is preserved. hole_start is an optional LFB
     * fault boundary (U32_MAX = none), independently of absent upper memory. */
    u32 operations, fail_at, hole_start;
    int fail_rc;
    struct trident_fake_event *trace;
    u32 trace_count, trace_capacity;
    bool trace_overflow, trace_lfb;
    u32 reads[3], writes[3], port_writes;
    u16 last_port;
    u8 last_port_value;
};
/* Observed defaults only: Command=0002, SR09=10, SPR=7F (FF variant via sr),
 * GR06=0C, 2MiB non-mirror at sampled endpoints, BAR0 decode 4MiB/BAR1 64KiB.
 * Inference: all blocked BAR0 addresses read FA7F3F6F/ignore writes; all upper
 * 2MiB reads return FF and writes are ignored. Hardware sampled endpoints
 * only; neither full-range behavior was measured. Unobserved register
 * initial values are zero placeholders, not claims about a cold machine;
 * individual state tests seed distinct boot values before Save. No ready,
 * arm, pin, valid save or init flags are installed by bind. RAM starts zero. */
int trident_fake_reset(struct trident_fake *, u8 *storage, u32 storage_bytes,
                       struct trident_fake_event *trace, u32 trace_capacity);
void trident_fake_bind(struct trident_fake *, struct trident_device *);
int trident_fake_memory(struct trident_fake *, u32 real_bytes,
                        enum trident_upper upper);
void trident_fake_trace_reset(struct trident_fake *);
void trident_fake_fail(struct trident_fake *, u32 operation, int rc);
/* Inference, not a measured hardware condition: full Init + CR21 linear bit
 * permits BAR0 writes. The host init/lifecycle test must supply an INDEPENDENT
 * full Init trace oracle (both SR0C branches, protect included). Only an exact
 * captured match may open this gate. Not a production callback or rc==0 hook.
 * Minimal foundation supplies no full Init oracle; 3b init test owns it.
 * first/count address captured trace; failed/truncated traces never qualify. */
int trident_fake_accept_init(struct trident_fake *, u32 first,
                             const struct trident_fake_event *, u32 count);
/* Host state/lifecycle tests call this AFTER independently checking Restore
 * trace. Optional reblocking models an unmeasured reentry failure (S3 N2),
 * not a claim that a particular register write causes it. */
void trident_fake_restored(struct trident_fake *);
extern const struct trident_io trident_fake_io;
#endif
