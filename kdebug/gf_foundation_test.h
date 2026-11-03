#ifndef KDEBUG_GF_FOUNDATION_TEST_H
#define KDEBUG_GF_FOUNDATION_TEST_H
#include "gf_foundation_wire.h"
#include "../hwdev/gf/foundation.h"
#include "recover.h"

/* Synchronous foreground loans; callbacks/context are never retained.
 * read_row supplies one 640-byte BAR1 row (y=0..479), returning a gf1_rc.
 * The diagnostic callback owns its row safe points inside the trial. */
int gf_foundation_test(struct gf1_request *, struct gf_foundation *, const struct recover_ops *, int (*display)(bool), int (*read_row)(void *, u32, u8 *), void *row_owner);
#endif
