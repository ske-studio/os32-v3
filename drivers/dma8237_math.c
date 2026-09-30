/* ======================================================================== */
/*  DMA8237_MATH.C — 8237 の算数とポート表だけ。I/O は 1 つも出さない       */
/*                                                                          */
/*  ここに置いたものは全部ホストで回せる (tools/tests/test_dma8237.py)。    */
/*  **エミュレータでは踏めない分岐**が多い — NP21/W は 0439h を実装せず、   */
/*  16MB 超も 64KB またぎも「たまたま動いて見える」ことがあるので、         */
/*  断る規則そのものをホストで固定する。                                    */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-2                             */
/*  記録: tools/tests/dma8237_tdd.md                                        */
/* ======================================================================== */

#include "dma8237.h"

/* ------------------------------------------------------------------------ */
/*  ポート表 — 番号は dma8237.h だけが持つ ([C4])                           */
/*                                                                          */
/*  **ch0 のバンクだけ 0027h で並びが飛ぶ** (io_dma.md 152〜155 行)。        */
/*  式で出そうとすると必ずここを間違えるので、表で持つ。                    */
/* ------------------------------------------------------------------------ */
struct dma_port_row {
    u16 addr;
    u16 count;
    u16 bank;
};

static const struct dma_port_row dma_ports[DMA_CHAN_COUNT] = {
    { DMA8237_CH0_ADDR, DMA8237_CH0_COUNT, DMA8237_CH0_BANK },
    { DMA8237_CH1_ADDR, DMA8237_CH1_COUNT, DMA8237_CH1_BANK },
    { DMA8237_CH2_ADDR, DMA8237_CH2_COUNT, DMA8237_CH2_BANK },
    { DMA8237_CH3_ADDR, DMA8237_CH3_COUNT, DMA8237_CH3_BANK }
};

void dma_split_addr(u32 phys, u16 *addr16, u8 *bank8)
{
    if (addr16) *addr16 = (u16)(phys & DMA_BANK_MASK);
    if (bank8)  *bank8  = (u8)((phys >> DMA_BANK_SHIFT) & 0xFF);
}

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

u32 dma_count_to_bytes(u32 count)
{
    return count + 1UL;
}

u32 dma_bytes_to_count(u32 bytes)
{
    return bytes - 1UL;
}

int dma_accept_pair(u32 c1, u32 c2, u32 limit)
{
    /* 設定長を超える値は、下位と上位が別の周期のものを合成したか、
     * TC 後の FFFFh を拾ったか。どちらにせよ使えない。 */
    if (c1 > limit || c2 > limit) return 0;
    if (c1 == c2) return 1;
    /* カウントは減る一方。増えていたら再ロードをまたいでいる。 */
    if (c1 < c2) return 0;
    return (c1 - c2 <= DMA_READ_SLACK) ? 1 : 0;
}

int dma_port_addr(unsigned int ch, u16 *port)
{
    if (ch >= DMA_CHAN_COUNT || !port) return DMA_ERR_ARG;
    *port = dma_ports[ch].addr;
    return 0;
}

int dma_port_count(unsigned int ch, u16 *port)
{
    if (ch >= DMA_CHAN_COUNT || !port) return DMA_ERR_ARG;
    *port = dma_ports[ch].count;
    return 0;
}

int dma_port_bank(unsigned int ch, u16 *port)
{
    if (ch >= DMA_CHAN_COUNT || !port) return DMA_ERR_ARG;
    *port = dma_ports[ch].bank;
    return 0;
}

u8 dma_mask_byte(unsigned int ch, int set)
{
    u8 v = (u8)(ch & 0x03);
    if (set) v |= DMA8237_MASK_SET_BIT;
    return v;
}

u8 dma_mode_byte(unsigned int ch, int dir, int mode)
{
    u8 v = (u8)(DMA8237_MODE_SINGLE_SEL | (ch & 0x03));

    /* dir は 8237 の呼び方と逆向きなので取り違えやすい。
     * 装置 → メモリ は 8237 にとって **ライト転送** (メモリへ書く)。 */
    v |= (dir == DMA_DIR_FROM_MEM) ? DMA8237_MODE_TR_READ
                                   : DMA8237_MODE_TR_WRITE;
    if (mode == DMA_MODE_CYCLIC) v |= DMA8237_MODE_AUTOINIT;
    return v;
}

/* 検査順は票 §1-6: ch → (表を引く前に) dir / mode → bytes / phys →
 * 終端の 64KB / 16MB 判定。ここを通ってから初めてステータスを触る。 */
int dma_check_args(unsigned int ch, u32 phys, u32 bytes, int dir, int mode)
{
    if (ch >= DMA_CHAN_COUNT) return DMA_ERR_ARG;
    if (dir != DMA_DIR_TO_MEM && dir != DMA_DIR_FROM_MEM) return DMA_ERR_ARG;
    if (mode != DMA_MODE_SINGLE && mode != DMA_MODE_CYCLIC) return DMA_ERR_ARG;
    if (bytes < DMA_XFER_MIN_BYTES || bytes > DMA_XFER_MAX_BYTES)
        return DMA_ERR_ARG;
    /* 拡張バンクを扱わないので、**終端**が 16MB に収まること。
     * 先頭だけ見ると 0xFFF000 + 8KB を通してしまう。 */
    if (phys >= DMA_PHYS_LIMIT) return DMA_ERR_ARG;
    if (bytes > DMA_PHYS_LIMIT - phys) return DMA_ERR_ARG;
    if (dma_crosses_64k(phys, bytes)) return DMA_ERR_ARG;
    return 0;
}
