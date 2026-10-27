#include "dma_contract.h"

/* 0 バイトは「またぐ」扱いで断る (dma8237.h の註)。
 * 終端は phys + bytes - 1。加算は u32 で巻き得るので、**引き算で**見る。 */
int dma_crosses_64k(u32 phys, u32 bytes)
{
    u32 off;

    if (bytes == 0) return 1;
    off = phys & DMA_BANK_MASK;
    /* バンクの中の残り = DMA_BANK_SIZE - off。これより多ければまたぐ。 */
    if (bytes > (u32)(DMA_BANK_SIZE - off)) return 1;
    return 0;
}

/* 終端で見る (先頭だけ見ると 0xFFF000 + 8KB を通す)。加算は巻き得るので
 * 引き算で見る (dma_check_args と同じ形)。 */
int dma_range_ok(u32 pa, u32 size, u32 limit)
{
    if (size == 0) return 0;
    if (pa >= limit || size > limit - pa) return 0;
    if (dma_crosses_64k(pa, size)) return 0;
    return 1;
}

