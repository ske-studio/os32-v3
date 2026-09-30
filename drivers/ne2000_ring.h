/* ======================================================================== */
/*  NE2000_RING.H — DP8390 受信リングの純粋な計算 (I/O 無し)                 */
/*                                                                          */
/*  ホストでも試験する (tools/tests/ne2000_ring_test.c, make check)。         */
/*  NE2K_HOST_TEST 定義時はカーネルの types.h を使わない。                    */
/*  仕様: DP8390D §7 (受信リング, 4 バイトヘッダ, BNRY/CURR)。               */
/* ======================================================================== */

#ifndef NE2000_RING_H
#define NE2000_RING_H

#ifdef NE2K_HOST_TEST
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
#else
#include "types.h"
#endif

/* ヘッダを 4 バイトから復号したもの。count はヘッダ 4B を含む
 * リング上のバイト数 (実カード: FCS 4B も含む / NP21/W: 含まない)。 */
struct ne2k_rx_hdr {
    u8  status;     /* RSR と同じビット */
    u8  next;       /* 次フレームの先頭ページ */
    u16 count;
};

#define NE2K_RING_OK          0
#define NE2K_RING_BAD_NEXT   -1   /* next が [pstart, pstop) 外か自己参照 */
#define NE2K_RING_BAD_COUNT  -2   /* 長さが範囲外、またはページ進行と矛盾 */
#define NE2K_RING_RX_ERROR   -3   /* status に PRX が無い (エラーフレーム)。next は妥当 */

void ne2k_ring_decode_hdr(const u8 raw[4], struct ne2k_rx_hdr *h);

/* 現在ページ cur にあるフレームのヘッダを検証し、上位へ渡す長さを返す。
 * fcs_in_count: count に FCS 4B が含まれるなら 1。
 * data_len (OK のときだけ有効) = ヘッダと FCS を除いた長さ (60〜1514)。 */
int  ne2k_ring_check(u8 pstart, u8 pstop, u8 cur,
                     const struct ne2k_rx_hdr *h, int fcs_in_count,
                     u16 *data_len);

/* リング上で count_on_ring バイト (ヘッダ + データ + FCS 分) が占めるページ数 */
u8   ne2k_ring_pages_for(u16 count_on_ring);

/* cur から pages 進めたページ (pstop で pstart に折り返す) */
u8   ne2k_ring_advance(u8 pstart, u8 pstop, u8 cur, u8 pages);

/* next まで回収したときに書く BNRY (= next の 1 ページ前、先頭なら pstop-1) */
u8   ne2k_ring_bnry_for(u8 pstart, u8 pstop, u8 next);

/* バイトアドレス addr から len 読むとき、pstop で切れる前に読める長さ。
 * 戻り値 < len なら残りは pstart<<8 から読む。 */
u16  ne2k_ring_first_chunk(u8 pstop, u16 addr, u16 len);

#endif /* NE2000_RING_H */
