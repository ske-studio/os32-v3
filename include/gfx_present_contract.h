#ifndef OS32_GFX_PRESENT_CONTRACT_H
#define OS32_GFX_PRESENT_CONTRACT_H

#include "os32_kapi_shared.h"

/* PLANAR4 用。packed 専用機種では空で定義する。
 * 呼ぶ側は通常文脈、bb_b != 0、後端が packed でないことを保証する。
 * raster はさらに table != 0、0 < table->count <= GFX_RASTER_MAX_ENTRIES。
 * 後端は commits・present_bytes・io_accesses の計数、列の消去、prev_dirty の更新、
 * VSYNC 待ち (sync != 0 かつ非 flip の時だけ)、flip、ラスタの再生を担当する。 */
void gfx_planar_present_dirty(int sync);
void gfx_planar_present_raster(GFX_RasterPalTable *table);

/* present 後の診断。診断のない機種では空で定義する。 */
void gfx_present_diag(void);

#endif
