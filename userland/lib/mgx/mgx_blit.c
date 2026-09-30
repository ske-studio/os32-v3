/* ======================================================================== */
/*  MGX_BLIT.C -- 1bpp ビットマップ → PC-98 4 プレーン展開                  */
/*                                                                          */
/*  gfx には依存せず、プレーンのポインタだけを受け取る。                     */
/*  1bpp → 4 プレーンの合成の考え方は                                       */
/*  programs/libos32gfx/asm/asm_draw.asm の asm_kcg_draw_font と同じで、     */
/*  ビットが立っている画素に ink、立っていない画素に paper の                */
/*  パレット番号を割り当てる。                                              */
/* ======================================================================== */

#include <string.h>
#include "libos32mgx.h"

/* 転送先 1 行分の作業バッファ (640/8 = 80 バイト) */
#define MGX_LINE_MAX MGX_MAX_PITCH

/* ------------------------------------------------------------------------
 *  転送元行の範囲外を 0 として読む
 * ------------------------------------------------------------------------ */
static u8 mgx_fetch(const u8 *row, int pitch, int i)
{
    if (i < 0 || i >= pitch)
        return 0;
    return row[i];
}

/* ------------------------------------------------------------------------
 *  転送元行をビット位置 dx へ置いたときの、転送先バイト bx の 8 ビット
 *
 *  転送先ビット (bx*8 + k) は転送元ビット (j*8 + k - s) に対応する。
 *  ここで s = dx & 7, j = bx - (dx >> 3)。s > 0 のときは転送元の
 *  2 バイトにまたがるので、隣接バイトから継ぎ足す。
 * ------------------------------------------------------------------------ */
static u8 mgx_gather(const u8 *row, int pitch, int j, int s)
{
    if (s == 0)
        return mgx_fetch(row, pitch, j);
    return (u8)(((u32)mgx_fetch(row, pitch, j - 1) << (8 - s))
                | ((u32)mgx_fetch(row, pitch, j) >> s));
}

/* ------------------------------------------------------------------------
 *  転送範囲の算出 (bpp=1 と bpp=4 で共通)
 *
 *  戻り値: 0 = 転送すべき領域あり, 1 = 完全に画面外
 * ------------------------------------------------------------------------ */
typedef struct {
    int bx0, bx1;       /* 触れる転送先バイト範囲 */
    int sy0, sy1;       /* 転送元の行範囲 */
    int s, byteoff;     /* ビットシフト量とバイトオフセット */
    int src_pitch;
    u8  mask[MGX_LINE_MAX];
} MgxBlitCtx;

static int mgx_setup(MgxBlitCtx *c, int dst_pitch, int dst_w, int dst_h,
                     int dx, int dy, int width, int height)
{
    u8 srcmask[MGX_LINE_MAX];
    int bx, dwb;

    c->src_pitch = (width + 7) / 8;
    c->s         = dx & 7;
    c->byteoff   = dx >> 3;

    if (dst_w > dst_pitch * 8)
        dst_w = dst_pitch * 8;
    dwb = (dst_w + 7) / 8;

    /* 転送元の有効ビット範囲 [0, width) を表すマスク行 */
    memset(srcmask, 0xFF, (size_t)c->src_pitch);
    if (width & 7)
        srcmask[c->src_pitch - 1] = (u8)(0xFFu << (8 - (width & 7)));

    c->bx0 = c->byteoff;
    c->bx1 = (dx + width + 7) / 8;
    if (c->bx0 < 0)
        c->bx0 = 0;
    if (c->bx1 > dwb)
        c->bx1 = dwb;                  /* dst_w を超える桁には触れない */
    if (c->bx1 > dst_pitch)
        c->bx1 = dst_pitch;
    if (c->bx0 >= c->bx1)
        return 1;

    c->sy0 = 0;
    c->sy1 = height;
    if (dy < 0)
        c->sy0 = -dy;
    if (dy + c->sy1 > dst_h)
        c->sy1 = dst_h - dy;
    if (c->sy0 >= c->sy1)
        return 1;

    /* マスクは全行で共通なので一度だけ作る */
    for (bx = c->bx0; bx < c->bx1; bx++)
        c->mask[bx] = mgx_gather(srcmask, c->src_pitch, bx - c->byteoff, c->s);

    if ((dst_w & 7) != 0 && c->bx1 == dwb)
        c->mask[c->bx1 - 1] &= (u8)(0xFFu << (8 - (dst_w & 7)));

    return 0;
}

/* ------------------------------------------------------------------------
 *  格納されている nplanes 枚をそのまま転送先のプレーンへ写す
 *
 *  bpp=1..4 のいずれでも同じ経路でよい。画素値がどのグレー階調で表示されるかは
 *  ヘッダのパレット表が決めるので、ここでは変換をしない。
 *  転送先のプレーン nplanes..3 には触らない — 画素値は 2^nplanes 未満なので
 *  上位プレーンは 0 であるべきで、呼び出し側の画面クリアで既にそうなっている
 *  (余白の色 mgx_paper_index() も 2^nplanes 未満)。
 * ------------------------------------------------------------------------ */
int mgx_blit_planes(u8 *const planes[4], int dst_pitch, int dst_w, int dst_h,
                    int dx, int dy,
                    const u8 *bitmap, u32 plane_size, int nplanes,
                    int width, int height)
{
    MgxBlitCtx c;
    u8 line[MGX_LINE_MAX];
    int y, bx, p;

    if (planes == 0 || bitmap == 0)
        return MGX_ERR_ARG;
    if (dst_pitch <= 0 || dst_pitch > MGX_LINE_MAX)
        return MGX_ERR_ARG;
    if (width <= 0 || height <= 0 || width > MGX_MAX_WIDTH)
        return MGX_ERR_ARG;
    if (dx < 0 || dst_w <= 0 || dst_h <= 0)
        return MGX_ERR_ARG;
    if (plane_size != (u32)((width + 7) / 8) * (u32)height)
        return MGX_ERR_ARG;
    if (nplanes < 1 || nplanes > 4)
        return MGX_ERR_ARG;

    if (mgx_setup(&c, dst_pitch, dst_w, dst_h, dx, dy, width, height))
        return MGX_OK;

    for (p = 0; p < nplanes; p++) {
        const u8 *splane = bitmap + (u32)p * plane_size;

        if (planes[p] == 0)
            continue;

        for (y = c.sy0; y < c.sy1; y++) {
            const u8 *srow = splane + (u32)y * (u32)c.src_pitch;
            u8 *drow = planes[p] + (u32)(dy + y) * (u32)dst_pitch;

            for (bx = c.bx0; bx < c.bx1; bx++)
                line[bx] = mgx_gather(srow, c.src_pitch, bx - c.byteoff, c.s);

            for (bx = c.bx0; bx < c.bx1; bx++)
                drow[bx] = (u8)((drow[bx] & ~c.mask[bx])
                                | (line[bx] & c.mask[bx]));
        }
    }

    return MGX_OK;
}
