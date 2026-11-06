/* Guard lifetime host API frozen at declaration stage (2026-11-06).
 * H1/H2/H3/H4 implement these seams in the existing freestanding fixtures;
 * no product behavior or fixture implementation is introduced here.
 * One caller-owned resident control per host process, initially all zero.
 * Bind to production-equivalent cold state: ch->f stays NULL until start;
 * no loans/maps/admission are pre-created. Keep the real MM/adapter in
 * integration tests; these controls inject at I/O/preflight boundaries.
 * Existing gf3_host_fail_at(H_READ/H_WRITE), gf4_host_fail_at(D_READ/D_WRITE)
 * and gf5_host_fail_at remain valid for their existing fixture operations. */
#ifndef TOOLS_TESTS_GF_GUARD_FAKE_H
#define TOOLS_TESTS_GF_GUARD_FAKE_H
#include "../../hwdev/gf/feature5.h"

struct gf3_host;
struct gf4_host;
enum gf_guard_host_op {
    GF_GUARD_READ, GF_GUARD_WRITE, GF_GUARD_MAP, GF_GUARD_UNMAP,
    GF_GUARD_OP_COUNT
};
struct gf_guard_host_failure {
    u32 nth; i32 rc; bool after_write;
};
struct gf_guard_host_counts {
    u32 device_maps, reservations, view_borrows, tile_borrows;
};
struct gf_guard_host {
    struct gf3_host *base;
    struct gf4_host *display;
    struct gf_guard_host_failure fail[GF_GUARD_OP_COUNT];
    u32 calls[GF_GUARD_OP_COUNT];
};
/* Bind existing storage without initializing a product or opening admission.
 * Missing base/control: -1, unchanged. display may be NULL for H1/H4. */
int gf_guard_host_bind(struct gf_guard_host *, struct gf3_host *, struct gf4_host *);
/* nth is 1-based since trace_clear, 0 disables. rc must be negative when
 * enabled. after_write applies only to WRITE: record/update register then
 * return rc (hardware may have observed the write). Other failures precede
 * side effects and preserve outputs. MAP/UNMAP fail only in preflight,
 * never during the real atomic PTE/ledger commit. Invalid input: -1 unchanged.
 * One selected failure per op; the selected occurrence fails only once. */
int gf_guard_host_fail_at(struct gf_guard_host *, enum gf_guard_host_op,
                           u32 nth, i32 rc, bool after_write);
/* Clear operation counts/traces only; preserve injections, hardware, time,
 * mappings, reservations and loans. */
void gf_guard_host_trace_clear(struct gf_guard_host *);
/* Seed all 12 raw PFB slots in the fake register image after devinit and
 * before tiles_init/F2 start; do not publish software FREE/OWNED or loans.
 * Slots are ordered 0..11, raw_addr/raw_limit/raw_pitch; state is ignored.
 * Invalid arguments/state: -1, no changes. Real GR start performs transfer. */
int gf_guard_host_tiles_seed(struct gf_guard_host *,
                              const struct gf5_tile_saved slots[GF5_TILE_COUNT]);
/* Observe real ledger/registry counts without changing ownership. No shadow
 * success counters: a failed unmap/release must retain the same counts.
 * Invalid arguments/channel: -1 and out unchanged, otherwise 0. */
int gf_guard_host_counts(struct gf_guard_host *, const struct gf2_chan *,
                          struct gf_guard_host_counts *out);
#endif
