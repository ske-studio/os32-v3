#ifndef GFX_POLICY_H
#define GFX_POLICY_H

#include "types.h"

/* H4 owns these definitions. Generation is nonzero, modulo u16, skipping 0.
 * H6/G3 mark a confirmed scan change then call the sole transition entry.
 * gfx_policy_reeval consumes gfx_scan_changed, including same-policy changes. */
extern u16 gfx_policy_gen;
extern int gfx_scan_changed;
int gfx_direct_wanted(void);
int gfx_fullscreen_owner(void); /* nonzero iff fullscreen owner is not GUI */
void gfx_policy_reeval(void);
/* Module-safe scan notification; policy and generation remain kernel-owned. */
void gfx_policy_scan_changed(void);
u16 gfx_policy_gen_get(void);
int gfx_policy_blocks_map(u32 role); /* LEDGER_ROLE_*; query stays permitted */

#endif
