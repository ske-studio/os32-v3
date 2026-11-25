#ifndef ENDIAN_DMA_H
#define ENDIAN_DMA_H
#include "types.h"
/* Aligned device words. Only the architecture accessor may touch raw.
 * Accessors convert values; ownership/cache maintenance is usbp_dma_sync's job. */
typedef struct { unsigned int raw; } le32_t;
STATIC_ASSERT(sizeof(le32_t) == 4, dma_word_size);
#include "arch_endian_dma.h"
#endif
