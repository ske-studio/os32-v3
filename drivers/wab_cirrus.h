/* ======================================================================== */
/*  WAB_CIRRUS.H — Cirrus Logic CL-GD54xx チップドライバ (票 H3)             */
/*                                                                          */
/*  ボードを知らない層。すべての操作は WabGlue 経由で行い、PC-98 のポート    */
/*  番号を 1 つも持たない (DESIGN §6, §7-3)。                                */
/* ======================================================================== */

#ifndef __WAB_CIRRUS_H
#define __WAB_CIRRUS_H

#include "types.h"
#include "wab_glue.h"

/* CPU から VRAM を覗く窓のバンク粒度 (GR0B bit5 = 0 のとき 4KB)。
 * バックエンドが「この矩形は窓 1 枚に収まるか」を判断するのに使う。 */
#define WAB_CIRRUS_BANK_GRAN   4096UL

/* BLT の最大寸法 (GR20/21・GR22/23 は 13bit + 1)。 */
#define WAB_CIRRUS_BLT_MAX     8192

/* エンジンのビジー待ちの上限。NP21/W の video-to-video / パターン塗りは
 * 同期実行なので即座に抜ける (cirrus_bitblt_videotovideo が
 * cirrus_bitblt_reset を呼んでから返る)。実機で万一ビジーが落ちなくても
 * ここで諦めて先へ進む — 無限ループでカーネルを止めない。 */
#define WAB_CIRRUS_BUSY_LIMIT  100000L

/* 機種検出。SR6 の解錠キー (12h を書くと 12h、他だと 0Fh が読める) と
 * CR27 (チップ ID) が Cirrus らしいかを見る。1 = 使える。 */
int  wab_cirrus_probe(WabGlue *g);

/* CR27 (Chip ID) の読み値。probe が 1 を返したあとだけ意味がある。 */
u8   wab_cirrus_chip_id(WabGlue *g);

/* 640x480 / 8bpp パックドピクセルを立ち上げる。pitch は表示面の 1 ライン
 * バイト数 (= 640)、start は表示面の VRAM 先頭オフセット (バイト)。
 * 戻り値 0 = 成功。 */
int  wab_cirrus_setup_8bpp(WabGlue *g, int width, int height,
                           u32 pitch, u32 start);

/* 表示を止めて標準 VGA 相当へ戻す (リレーはグルーの仕事)。 */
void wab_cirrus_shutdown(WabGlue *g);

/* CPU 窓のバンクを vram_off が見える位置へ動かし、窓内オフセットを返す。
 * 戻り値 + glue->win_base がその画素の線形アドレス。
 * 同じバンクなら I/O を出さない (小さい矩形の連続書き込み対策)。 */
u32  wab_cirrus_set_bank(WabGlue *g, u32 vram_off);

/* DAC パレット 1 項目 (輝度は VGA DAC の 6bit = 0〜63)。 */
void wab_cirrus_dac_set(WabGlue *g, int idx, u8 r6, u8 g6, u8 b6);

/* 8x8 モノクロパターン (8 バイト) の VRAM 上の位置を教える。
 * 塗りつぶしはこのパターン (全ビット 1) の色展開で行うので、
 * バックエンドが init で全 FFh を書き込んでからでないと使えない。 */
void wab_cirrus_set_fill_pattern(WabGlue *g, u32 vram_off);

/* 矩形塗りつぶし。dst_off は VRAM バイトオフセット、w はピクセル数
 * (8bpp なのでバイト数と同じ)。戻り値 0 = 発行した。 */
int  wab_cirrus_fill(WabGlue *g, u32 dst_off, u32 pitch,
                     int w, int h, u8 color);

/* 矩形転送 (VRAM → VRAM)。重なりは上下方向だけ後方転送で避ける。 */
int  wab_cirrus_copy(WabGlue *g, u32 dst_off, u32 src_off,
                     u32 dpitch, u32 spitch, int w, int h);

/* エンジンの完了待ち。戻り値 0 = idle になった / -1 = 上限まで待った。 */
int  wab_cirrus_wait_idle(WabGlue *g);

#endif /* __WAB_CIRRUS_H */
