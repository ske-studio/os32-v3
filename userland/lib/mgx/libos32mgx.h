/* ======================================================================== */
/*  LIBOS32MGX.H -- MGX (漫画専用モノクロ画像形式) デコーダ 公開ヘッダ       */
/*                                                                          */
/*  MGX は 1bpp モノクロ専用の画像形式。エンコードはホスト側                 */
/*  (tools/img2mgx.py) のみ、ゲスト側は本ライブラリによるデコードのみ。      */
/*                                                                          */
/*  圧縮は PNG と同じ deflate をそのまま使い、展開には zlib 付属の参照       */
/*  実装 puff.c (lib/puff.c) を使う。PNG のスキャンラインフィルタ段は        */
/*  1bpp では逆効果と実測されたため採用していない。                          */
/*  詳細: docs/MGX_FORMAT.md                                                */
/*                                                                          */
/*  依存: lib/puff.c のみ。KernelAPI にも gfx にも依存しない。               */
/*        ファイル入出力とメモリ確保は呼び出し側の責務。                     */
/* ======================================================================== */

#ifndef LIBOS32MGX_H
#define LIBOS32MGX_H

#include "os32_kapi_shared.h"    /* u8, u16, u32 */

/* ====================================================================== */
/*  1. フォーマット定数 (tools/img2mgx.py と同期させること)                 */
/* ====================================================================== */

#define MGX_HDR_SIZE     48
#define MGX_VERSION      1
#define MGX_PAL_ENTRIES  16

#define MGX_MAX_WIDTH    640
#define MGX_MAX_HEIGHT   400
#define MGX_MAX_PITCH    ((MGX_MAX_WIDTH + 7) / 8)          /* 80 */
#define MGX_MAX_PLANE    (MGX_MAX_PITCH * MGX_MAX_HEIGHT)   /* 32000 */
#define MGX_MAX_BPP      4
#define MGX_MAX_RAW      (MGX_MAX_PLANE * MGX_MAX_BPP)      /* 128000 */

/* codec */
#define MGX_CODEC_STORED 0   /* 生 1bpp (無圧縮) */
#define MGX_CODEC_ZLIB   1   /* RFC1950 zlib ストリーム */

/* flags */
#define MGX_FLAG_INK_SET 0x01  /* セットされたビットが墨 (v1 は常に 1) */
#define MGX_FLAG_ROT90   0x02  /* エンコーダが 90 度回転した (情報用) */

/* palette -- プレーン値をどう色に写すか */
#define MGX_PAL_INK      0   /* bpp=1: 1 = 墨, 0 = 紙 */
#define MGX_PAL_GRAY16   1   /* bpp=4: 値 i = グレー階調 i (線形) */
#define MGX_PAL_GRAY16_G 2   /* bpp=4: 値 i はグレー階調 g の Gray 符号
                              *        (g ^ (g >> 1) == i)。
                              *        プレーン分離データの圧縮率が上がる。
                              *        パレット側で並べ替えるのでデコードは
                              *        素通しでよい。 */

/* dither (情報用。デコードには影響しない) */
#define MGX_DITHER_NONE    0
#define MGX_DITHER_CLUSTER 1
#define MGX_DITHER_BAYER4  2
#define MGX_DITHER_BAYER8  3
#define MGX_DITHER_FS      4

/* ====================================================================== */
/*  2. エラーコード                                                        */
/* ====================================================================== */

#define MGX_OK            0
#define MGX_ERR_MAGIC   (-1)   /* マジック不一致 */
#define MGX_ERR_VERSION (-2)   /* 未対応バージョン */
#define MGX_ERR_HEADER  (-3)   /* ヘッダの値が不正 (サイズ超過など) */
#define MGX_ERR_TRUNC   (-4)   /* データが足りない */
#define MGX_ERR_CODEC   (-5)   /* 未知の codec */
#define MGX_ERR_STREAM  (-6)   /* 圧縮ストリームが壊れている */
#define MGX_ERR_SIZE    (-7)   /* 展開後サイズが宣言と違う / 出力バッファ不足 */
#define MGX_ERR_CKSUM   (-8)   /* Adler-32 不一致 */
#define MGX_ERR_ARG     (-9)   /* 引数が不正 */

/* ====================================================================== */
/*  3. ヘッダ                                                              */
/* ====================================================================== */

typedef struct {
    u16 width;       /* 1..640 */
    u16 height;      /* 1..400 */
    u8  version;
    u8  flags;       /* MGX_FLAG_* */
    u8  codec;       /* MGX_CODEC_* */
    u8  dither;      /* MGX_DITHER_* (情報用) */
    u32 data_size;   /* ヘッダ直後のペイロード長 */
    u32 raw_size;    /* 展開後のサイズ = height * pitch * bpp */
    u8  bpp;         /* 1..MGX_MAX_BPP (=4) = 格納プレーン数 */
    u8  npal;        /* 使用パレット数 (1 << bpp) */

    /* パレット表: プレーン値 i を表示するグレー階調 (0..15)。
     * 並べ替え (Gray 符号など) はエンコーダが表に畳み込んで書くので、
     * デコード側は gfx_set_palette() へ流し込むだけでよい。 */
    u8  palette[MGX_PAL_ENTRIES];
} MgxHeader;

/* ====================================================================== */
/*  4. API -- デコード (mgx_decode.c)                                      */
/* ====================================================================== */

/* 先頭 MGX_HDR_SIZE バイトを解析する。
 * buf/len には少なくとも MGX_HDR_SIZE バイトが必要。
 * 戻り値: MGX_OK または MGX_ERR_*
 */
int mgx_parse_header(const u8 *buf, u32 len, MgxHeader *h);

/* パレット表から「余白に使う番号」を選ぶ。
 * invert が非 0 なら階調を反転したうえで判定する。
 * 表示上いちばん明るい番号 (同値なら小さい番号) を返す。
 */
int mgx_paper_index(const MgxHeader *h, int invert);

/* 1 プレーンの 1 行あたりバイト数 = (width + 7) / 8 */
int mgx_pitch(const MgxHeader *h);

/* 1 プレーンのバイト数 = pitch * height */
u32 mgx_plane_size(const MgxHeader *h);

/* ペイロードを生 1bpp ビットマップへ展開する。
 *
 *   payload : ヘッダ直後の h->data_size バイト
 *   dst     : 展開先 (h->raw_size バイト以上)
 *
 * 戻り値: 展開したバイト数 (> 0)、失敗時は MGX_ERR_*
 *
 * 破損データに対して dst_cap を超えて書き込むことはない。
 */
int mgx_decode(const MgxHeader *h, const u8 *payload, u32 payload_len,
               u8 *dst, u32 dst_cap);

/* ====================================================================== */
/*  5. API -- 描画 (mgx_blit.c)                                            */
/* ====================================================================== */

/* 格納されている nplanes 枚のプレーンを転送先のプレーンへ写す。
 * bpp=1..4 のいずれでも同じ経路でよい (階調はパレット表が決める)。
 * gfx には依存せず、プレーンのポインタだけを受け取る。
 *
 *   planes[4] : 転送先プレーン (0:B 1:R 2:G 3:I)
 *   dst_pitch : 転送先の 1 行あたりバイト数 (640 なら 80)
 *   dst_w/h   : 転送先の画素数 (クリッピングに使う)
 *   dx, dy    : 転送先の左上座標 (dx は任意。8 の倍数なら高速)
 *   bitmap    : プレーン 0..nplanes-1 が plane_size バイトずつ連続したデータ
 *   plane_size: mgx_plane_size() の値
 *   nplanes   : 1..4 (= MgxHeader.bpp)
 *   width/h   : bitmap の画素数
 *
 * 転送先のプレーン nplanes..3 には触らない。画素値は 2^nplanes 未満なので
 * 上位プレーンは 0 であるべきで、呼び出し側の画面クリアで既にそうなっている。
 *
 * 戻り値: MGX_OK または MGX_ERR_ARG
 */
int mgx_blit_planes(u8 *const planes[4], int dst_pitch, int dst_w, int dst_h,
                    int dx, int dy,
                    const u8 *bitmap, u32 plane_size, int nplanes,
                    int width, int height);

#endif /* LIBOS32MGX_H */
