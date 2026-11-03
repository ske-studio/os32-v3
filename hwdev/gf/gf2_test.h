/* Internal foreground dispatch for layer 2 diagnostics. */
#ifndef HWDEV_GF2_TEST_H
#define HWDEV_GF2_TEST_H
#include "foundation.h"
enum gf2_test_op { GF2T_START, GF2T_REFERENCE, GF2T_SNAPSHOT, GF2T_STOP, GF2T_RECOVER, GF2T_BAD_HANDLE, GF2T_BAD_METHOD };
struct gf2_test_request { u32 size; enum gf2_test_op op; u32 deadline_us; };
struct gf2_test_reply { int rc, dma_rc; u32 state, epoch, seq, put, get, reference, B, ctx_off, ctx_bytes, ctx_words, fault_epoch, chid, fault_put, fault_get, fault_reference, caches, push1, cache_get, pull0, method, data, dma_state, dma_fetch, dma_push, dma_dcount, gr_trap, gr_method, gr_data, gr_class; bool latched; struct gf_irq_stats stats, first, latest; };
struct gf2_test_ops { int (*prepare)(struct gf_foundation *, const struct recover_bar_extent *, u32); int (*call)(struct gf_foundation *, const struct gf2_test_request *, struct gf2_test_reply *); };
#endif
