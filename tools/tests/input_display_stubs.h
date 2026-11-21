#include "types.h"
volatile u32 kctx_irq_depth, kctx_exc_depth;
int appslot_gfx_resource_snapshot(int owner, u32 *gen)
{ (void)owner; (void)gen; return -1; }
int appslot_gfx_owner_exit_if(int owner, u32 gen)
{ (void)owner; (void)gen; return 0; }
