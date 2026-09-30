/* ======================================================================== */
/*  NE2000_RING.C — DP8390 受信リングの純粋な計算 (I/O 無し)                 */
/*                                                                          */
/*  ne2000.c の受信回収と OVW 復旧が使う。ホスト試験:                        */
/*  tools/tests/ne2000_ring_test.c (make check → check-ne2000-ring)。        */
/* ======================================================================== */

#include "ne2000_ring.h"

#define RING_PAGE_SIZE   256
#define RING_HDR_LEN     4
#define RING_FCS_LEN     4
#define RING_ETH_MIN     60
#define RING_ETH_MAX     1514
#define RING_STATUS_PRX  0x01

void ne2k_ring_decode_hdr(const u8 raw[4], struct ne2k_rx_hdr *h)
{
    h->status = raw[0];
    h->next   = raw[1];
    h->count  = (u16)(raw[2] | ((u16)raw[3] << 8));
}

u8 ne2k_ring_pages_for(u16 count_on_ring)
{
    return (u8)((count_on_ring + RING_PAGE_SIZE - 1) / RING_PAGE_SIZE);
}

u8 ne2k_ring_advance(u8 pstart, u8 pstop, u8 cur, u8 pages)
{
    unsigned int size = (unsigned int)pstop - pstart;
    unsigned int p = (unsigned int)cur + pages;
    if (size == 0) return pstart;
    while (p >= pstop) p -= size;
    return (u8)p;
}

u8 ne2k_ring_bnry_for(u8 pstart, u8 pstop, u8 next)
{
    if (next == pstart) return (u8)(pstop - 1);
    return (u8)(next - 1);
}

u16 ne2k_ring_first_chunk(u8 pstop, u16 addr, u16 len)
{
    u32 end = (u32)pstop << 8;
    if ((u32)addr + len <= end) return len;
    if ((u32)addr >= end) return 0;
    return (u16)(end - addr);
}

int ne2k_ring_check(u8 pstart, u8 pstop, u8 cur,
                    const struct ne2k_rx_hdr *h, int fcs_in_count,
                    u16 *data_len)
{
    unsigned int on_ring;   /* リング上で占めるバイト数 (FCS の場所を含む) */
    unsigned int dlen;
    u8 pages;

    /* next ページの妥当性: 範囲内で、自分自身を指さない */
    if (h->next < pstart || h->next >= pstop) return NE2K_RING_BAD_NEXT;
    if (h->next == cur) return NE2K_RING_BAD_NEXT;

    /* 長さ: ヘッダ + データ (+ FCS)。実カードは count に FCS を含み、
     * NP21/W は含まないが、どちらもリング上には FCS 分の場所を取る。 */
    on_ring = h->count;
    if (!fcs_in_count) on_ring += RING_FCS_LEN;
    if (on_ring < RING_HDR_LEN + RING_ETH_MIN + RING_FCS_LEN) return NE2K_RING_BAD_COUNT;
    if (on_ring > RING_HDR_LEN + RING_ETH_MAX + RING_FCS_LEN) return NE2K_RING_BAD_COUNT;
    dlen = on_ring - RING_HDR_LEN - RING_FCS_LEN;

    /* ページ進行との整合: next は cur + 占有ページ数 でなければならない */
    pages = ne2k_ring_pages_for((u16)on_ring);
    if (ne2k_ring_advance(pstart, pstop, cur, pages) != h->next) return NE2K_RING_BAD_COUNT;

    if (data_len) *data_len = (u16)dlen;
    if (!(h->status & RING_STATUS_PRX)) return NE2K_RING_RX_ERROR;
    return NE2K_RING_OK;
}
