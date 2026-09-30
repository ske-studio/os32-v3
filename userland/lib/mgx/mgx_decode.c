/* ======================================================================== */
/*  MGX_DECODE.C -- MGX ヘッダ解析と展開                                    */
/*                                                                          */
/*  展開は zlib 本体の inflate (lib/zlib/) に委ねる。                        */
/*  ペイロードは標準の zlib ストリーム (RFC1950) なので、ホスト側は          */
/*  zlib.compress() の出力をそのまま入れるだけでよい。ラッパの検査も         */
/*  末尾 Adler-32 の照合も inflate() が行うので、こちら側には持たない。      */
/* ======================================================================== */

#include <string.h>
#include <stdlib.h>
#include "libos32mgx.h"
#include "zlib.h"

/* ------------------------------------------------------------------------
 *  zlib のアロケータ
 *
 *  lib/zlib は -DZ_SOLO でビルドしていて malloc を直接呼ばないので、
 *  z_stream に渡す形でこちらから供給する。inflate が確保するのは
 *  状態構造体 (約 7KB) と 32KB の窓の 2 つだけ。
 * ------------------------------------------------------------------------ */
static voidpf mgx_zalloc(voidpf opaque, uInt items, uInt size)
{
    (void)opaque;
    return (voidpf)malloc((size_t)items * (size_t)size);
}

static void mgx_zfree(voidpf opaque, voidpf address)
{
    (void)opaque;
    free(address);
}

/* ------------------------------------------------------------------------
 *  リトルエンディアン読み出し
 * ------------------------------------------------------------------------ */
#define LE16(p) ((u16)((p)[0] | ((u16)(p)[1] << 8)))
#define LE32(p) ((u32)((p)[0] | ((u32)(p)[1] << 8) \
                              | ((u32)(p)[2] << 16) | ((u32)(p)[3] << 24)))

/* ------------------------------------------------------------------------
 *  ヘッダ解析
 * ------------------------------------------------------------------------ */
int mgx_parse_header(const u8 *buf, u32 len, MgxHeader *h)
{
    u32 pitch;
    int i;

    if (buf == 0 || h == 0)
        return MGX_ERR_ARG;
    if (len < MGX_HDR_SIZE)
        return MGX_ERR_TRUNC;

    if (buf[0] != 'M' || buf[1] != 'G' || buf[2] != 'X' || buf[3] != '1')
        return MGX_ERR_MAGIC;

    h->version   = buf[4];
    h->flags     = buf[5];
    h->width     = LE16(buf + 6);
    h->height    = LE16(buf + 8);
    h->codec     = buf[10];
    h->dither    = buf[11];
    h->data_size = LE32(buf + 12);
    h->raw_size  = LE32(buf + 16);
    h->bpp       = buf[20];
    h->npal      = buf[21];
    for (i = 0; i < MGX_PAL_ENTRIES; i++)
        h->palette[i] = (u8)(buf[32 + i] & 0x0F);

    if (h->version != MGX_VERSION)
        return MGX_ERR_VERSION;
    if (h->width == 0 || h->width > MGX_MAX_WIDTH)
        return MGX_ERR_HEADER;
    if (h->height == 0 || h->height > MGX_MAX_HEIGHT)
        return MGX_ERR_HEADER;
    if (h->codec != MGX_CODEC_STORED && h->codec != MGX_CODEC_ZLIB)
        return MGX_ERR_CODEC;

    if (h->bpp < 1 || h->bpp > MGX_MAX_BPP)
        return MGX_ERR_HEADER;
    if (h->npal != (1 << h->bpp))
        return MGX_ERR_HEADER;

    /* raw_size がヘッダ内で自己矛盾していないか (壊れた値で確保させない) */
    pitch = (u32)((h->width + 7) / 8);
    if (h->raw_size != pitch * (u32)h->height * (u32)h->bpp)
        return MGX_ERR_HEADER;
    if (h->data_size == 0 || h->data_size > (u32)MGX_MAX_RAW * 2UL)
        return MGX_ERR_HEADER;

    return MGX_OK;
}

int mgx_pitch(const MgxHeader *h)
{
    if (h == 0)
        return MGX_ERR_ARG;
    return (h->width + 7) / 8;
}

int mgx_paper_index(const MgxHeader *h, int invert)
{
    int i, level, best_level = -1, best_idx = 0;

    if (h == 0)
        return 0;

    for (i = 0; i < h->npal && i < MGX_PAL_ENTRIES; i++) {
        level = h->palette[i] & 0x0F;
        if (invert)
            level = 15 - level;
        if (level > best_level) {
            best_level = level;
            best_idx = i;
        }
    }
    return best_idx;
}

u32 mgx_plane_size(const MgxHeader *h)
{
    if (h == 0)
        return 0;
    return (u32)((h->width + 7) / 8) * (u32)h->height;
}

/* ------------------------------------------------------------------------
 *  展開
 * ------------------------------------------------------------------------ */
int mgx_decode(const MgxHeader *h, const u8 *payload, u32 payload_len,
               u8 *dst, u32 dst_cap)
{
    z_stream zs;
    int rc;

    if (h == 0 || payload == 0 || dst == 0)
        return MGX_ERR_ARG;
    if (payload_len < h->data_size)
        return MGX_ERR_TRUNC;
    if (dst_cap < h->raw_size)
        return MGX_ERR_SIZE;

    if (h->codec == MGX_CODEC_STORED) {
        if (h->data_size != h->raw_size)
            return MGX_ERR_SIZE;
        memcpy(dst, payload, h->raw_size);
        return (int)h->raw_size;
    }

    /* --- codec 1: RFC1950 zlib ストリーム ---
     * ラッパ (CMF/FLG) の検査も末尾 Adler-32 の照合も inflate() が行う。
     * 出力バッファは raw_size ちょうどに絞ってあるので、
     * 壊れた入力が dst_cap を超えて書くことはない。 */
    memset(&zs, 0, sizeof(zs));
    zs.zalloc    = mgx_zalloc;
    zs.zfree     = mgx_zfree;
    zs.next_in   = (Bytef *)payload;
    zs.avail_in  = (uInt)h->data_size;
    zs.next_out  = (Bytef *)dst;
    zs.avail_out = (uInt)h->raw_size;

    if (inflateInit(&zs) != Z_OK)
        return MGX_ERR_STREAM;

    rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);

    if (rc != Z_STREAM_END) {
        /* Z_DATA_ERROR は Adler-32 不一致も含む */
        if (rc == Z_BUF_ERROR || rc == Z_OK)
            return MGX_ERR_SIZE;
        if (rc == Z_MEM_ERROR)
            return MGX_ERR_SIZE;
        return MGX_ERR_STREAM;
    }
    if (zs.total_out != (uLong)h->raw_size)
        return MGX_ERR_SIZE;

    return (int)h->raw_size;
}
