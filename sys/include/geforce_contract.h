#ifndef GEFORCE_CONTRACT_H
#define GEFORCE_CONTRACT_H
/* 共通 (exec・sys・gfx_core) と GeForce の部品の間の口。常駐側の前景の poll と、
 * 表示が止まった・失敗した時に部品が共通へ知らせる口 (実体は sys/hal/gfx/gfx_core.c)。 */
#include <stdbool.h>
#include "types.h"

/* Resident foreground adapter: reread the F5 slot on every dispatch, never
 * cache module ctx/ops. No IRQ calls. Consume pending heat stop at the common
 * exit while retaining reentry protection; backend stop owns active/routed. */
void geforce_foreground_poll(void);
/* Resident tick/pending query only; no module callback or MMIO. */
bool geforce_fg_due(u32 now_tick);
/* The module reports that its display stopped / failed (sys/hal/gfx/gfx_core.c). */
void geforce_display_stopped(void);
void geforce_display_failed(void);

void gf_display_owner_exit(int id);

#endif
