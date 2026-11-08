/* A0 inert foundation; H4 supplies policy decisions and transitions. */
#include "gfx_policy.h"

u16 gfx_policy_gen = 1;
int gfx_scan_changed;

int gfx_direct_wanted(void) { return 0; }
int gfx_fullscreen_owner(void) { return 0; }
void gfx_policy_reeval(void) { }
int gfx_policy_blocks_map(u32 role) { (void)role; return 0; }
