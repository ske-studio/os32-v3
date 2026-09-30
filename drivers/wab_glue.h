/* ======================================================================== */
/*  WAB_GLUE.H — ウィンドウアクセラレータ「ボードグルー」の共通インタフェース */
/*                                                                          */
/*  DESIGN §6 の二層構造の境界そのもの:                                      */
/*                                                                          */
/*      HAL バックエンド (gfx/backend_cirrus.c)                              */
/*            │                                                            */
/*            ├─ チップドライバ (drivers/wab_cirrus.c)                      */
/*            │     VGA の**論理**レジスタ番号 (3C0h 系) だけを知る。        */
/*            │     PC-98 のポート番号を 1 つも持たない。                    */
/*            │                                                            */
/*            └─ ボードグルー (drivers/wab_glue_xe10.c ほか)                */
/*                  ポート翻訳表と制御レジスタ (ID / リレー / 窓 / MMIO)。    */
/*                  チップのレジスタ体系を知らない。                          */
/*                                                                          */
/*  チップドライバはこの 1 枚の表だけを通してハードウェアに触る。新しい       */
/*  ボード (PC-9801-96、メルコ WAB-S、…) を足す人は WabGlue をもう 1 枚      */
/*  書けばよく、チップドライバは無変更で載る (DESIGN §7-3)。                  */
/*                                                                          */
/*  NP21/W の `vga_convert_ioport()` と同じ役割 (DESIGN §6)。               */
/* ======================================================================== */

#ifndef __WAB_GLUE_H
#define __WAB_GLUE_H

#include "types.h"

/* VGA の論理レジスタ番号 (チップドライバが使う唯一の「アドレス」)。
 * グルーが機種ごとの PC-98 ポートへ翻訳する。値は IBM PC/AT の VGA と同じ
 * なので、チップドライバは市販のチップ資料そのままで書ける。 */
#define VGA_ATTR_IDX      0x03C0  /* Attribute Controller Index/Data (write) */
#define VGA_ATTR_DATA_R   0x03C1  /* Attribute Controller Data (read) */
#define VGA_MISC_W        0x03C2  /* Miscellaneous Output (write) / Status0 (read) */
#define VGA_SLEEP         0x03C3  /* Sleep Address (Video Subsystem Enable) */
#define VGA_SEQ_IDX       0x03C4  /* Sequencer Index */
#define VGA_SEQ_DATA      0x03C5  /* Sequencer Data */
#define VGA_PIXMASK       0x03C6  /* Pixel Mask / Hidden DAC */
#define VGA_DAC_RIDX      0x03C7  /* DAC Read Index (write) / DAC State (read) */
#define VGA_DAC_WIDX      0x03C8  /* DAC Write Index */
#define VGA_DAC_DATA      0x03C9  /* DAC Data */
#define VGA_FEAT_R        0x03CA  /* Feature Control (read) */
#define VGA_MISC_R        0x03CC  /* Miscellaneous Output (read) */
#define VGA_GRC_IDX       0x03CE  /* Graphics Controller Index */
#define VGA_GRC_DATA      0x03CF  /* Graphics Controller Data */
#define VGA_CRTC_IDX      0x03D4  /* CRTC Index */
#define VGA_CRTC_DATA     0x03D5  /* CRTC Data */
#define VGA_STATUS1       0x03DA  /* Input Status 1 (read) / Feature Control (write) */

/* ボードグルーの関数表。 */
typedef struct WabGlue {
    const char *name;

    /* 機種検出。1 = このボードが居る。副作用を残さないこと。 */
    int  (*probe)(void);

    /* ボードを使える状態にする (Video Subsystem Enable など)。
     * 映像リレーはここでは触らない — 画面を奪うのは enter/leave の仕事。 */
    void (*init)(void);

    /* VGA 論理レジスタの読み書き。翻訳表を持つのはグルーだけ。 */
    void (*out)(u16 reg, u8 val);
    u8   (*in)(u16 reg);

    /* 映像出力リレー。1 = アクセラレータ出力 / 0 = 本体 (98 グラフィック)。 */
    void (*relay)(int on);

    /* メモリマップト I/O 窓の有効/無効。窓を持たないボードは無視してよい。 */
    void (*mmio_enable)(int on);

    /* CPU から見える VRAM 窓 (物理アドレス / バイト数)。窓のどこが VRAM の
     * どこに当たるかはチップのバンクレジスタが決める (層の分担)。 */
    u32  win_base;
    u32  win_size;

    /* CPU から見える **リニア窓** (物理アドレス / バイト数)。バンク
     * レジスタを通らず VRAM オフセット 0 から連続して見える窓で、
     * ボードが持っていなければ lin_size = 0。
     * 窓の物理番地はボードが決める (バックエンドは番地を知らない)。
     * init() が開く。閉じるのは linear_enable(0) — バックエンドの
     * leave()/shutdown() から呼ぶ。 */
    u32  lin_base;
    u32  lin_size;

    /* リニア窓の有効/無効。窓を持たないボードは NULL でよい。 */
    void (*linear_enable)(int on);

    /* MMIO 窓の先頭 (無いボードは NULL)。BLT レジスタを OUT ではなく
     * メモリ書き込みで設定するための入口 (DESIGN §8)。 */
    volatile u8 *mmio;

    /* このグルーが発行した I/O アクセスの累計。契約 G7 の io_accesses は
     * バックエンドがこの値の差分を拾って加算する (層をまたいで gfx の
     * カウンタ変数を触らないため)。 */
    u32 io_count;
} WabGlue;

#endif /* __WAB_GLUE_H */
