/* ======================================================================== */
/*  LZ4.C -- LZ4 圧縮/展開コマンド (OS32 外部プログラム)                     */
/*                                                                          */
/*    lz4 c INPUT OUTPUT   ... INPUT を圧縮して OUTPUT に書き出す            */
/*    lz4 d INPUT OUTPUT   ... INPUT を展開して OUTPUT に書き出す            */
/*    lz4 t INPUT          ... INPUT の圧縮率をテスト表示                    */
/*                                                                          */
/*  ファイル形式: [4B orig_size LE] [LZ4ブロックデータ]                       */
/*  Python tools/lz4_compress.py と完全互換。                                */
/* ======================================================================== */

#include "os32api.h"
#include "lz4.h"

/* エンコーダ用定数 */
#define LZ4_MINMATCH     4
#define LZ4_HASHLOG      14           /* ハッシュテーブル: 2^14 = 16384 エントリ */
#define LZ4_HASHTABLESIZE (1 << LZ4_HASHLOG)
#define LZ4_MAX_OFFSET   65535

/* 静的バッファ (読み込み/書き込み用) */
static u8 src_buf[128 * 1024];   /* 最大128KBの入力 */
static u8 dst_buf[140 * 1024];   /* 圧縮/展開用出力バッファ */

/* ======================================================================== */
/*  LZ4 エンコーダ (外部プログラム用)                                        */
/*                                                                          */
/*  ハッシュテーブル (64KB) は呼び出し側で動的確保して渡す。                  */
/*  公式 LZ4 とワイヤ互換のブロックデータを生成する。                         */
/* ======================================================================== */

/* 4バイトからハッシュ値を計算 */
static u32 lz4_hash4(const u8 *p)
{
    u32 v = (u32)p[0] | ((u32)p[1] << 8) |
            ((u32)p[2] << 16) | ((u32)p[3] << 24);
    /* Knuth乗算ハッシュ */
    return (v * 2654435761UL) >> (32 - LZ4_HASHLOG);
}

/* リテラル長/マッチ長の拡張バイト書き込み */
static u8 *lz4_write_len(u8 *op, u8 *op_end, int length)
{
    while (length >= 255) {
        if (op >= op_end) return NULL;
        *op++ = 255;
        length -= 255;
    }
    if (op >= op_end) return NULL;
    *op++ = (u8)length;
    return op;
}

static int lz4_encode(const u8 *src, int src_size,
                      u8 *dst, int dst_capacity,
                      u16 *hash_table)
{
    const u8 *ip;
    const u8 *ip_end;
    const u8 *ip_limit;  /* マッチ検索の終端 (末尾5バイトは検索しない) */
    const u8 *anchor;    /* 直近のリテラル開始位置 */
    u8 *op;
    u8 *op_end;
    int i;

    if (src_size < 1) {
        /* 空入力 → 空出力 */
        return 0;
    }

    /* ハッシュテーブル初期化 */
    for (i = 0; i < LZ4_HASHTABLESIZE; i++) {
        hash_table[i] = 0;
    }

    ip       = src;
    ip_end   = src + src_size;
    ip_limit = ip_end - 5;  /* 末尾5バイトはリテラル保証 */
    anchor   = ip;
    op       = dst;
    op_end   = dst + dst_capacity;

    /* 最初のバイトをハッシュ登録 */
    hash_table[lz4_hash4(ip)] = 0;
    ip++;

    /* メイン圧縮ループ */
    while (ip < ip_limit) {
        u32 h;
        const u8 *ref;
        int offset;
        int match_len;
        int lit_len;
        u8 *token_ptr;
        int ml;
        const u8 *mp;
        const u8 *mr;

        /* ハッシュで候補を検索 */
        h = lz4_hash4(ip);
        ref = src + hash_table[h];
        hash_table[h] = (u16)(ip - src);

        offset = (int)(ip - ref);

        /* マッチ判定: 4バイト一致 + 範囲内 */
        if (offset < 1 || offset > LZ4_MAX_OFFSET ||
            ref < src ||
            ip[0] != ref[0] || ip[1] != ref[1] ||
            ip[2] != ref[2] || ip[3] != ref[3]) {
            ip++;
            continue;
        }

        /* マッチ長を計算 */
        match_len = LZ4_MINMATCH;
        mp = ip + LZ4_MINMATCH;
        mr = ref + LZ4_MINMATCH;
        while (mp < ip_end && *mp == *mr) {
            mp++;
            mr++;
            match_len++;
        }

        /* シーケンス出力 */
        lit_len = (int)(ip - anchor);

        /* トークンバイト */
        if (op >= op_end) return 0;
        token_ptr = op++;

        /* トークン値を構築 */
        if (lit_len >= 15) {
            *token_ptr = 0xF0;
            op = lz4_write_len(op, op_end, lit_len - 15);
            if (!op) return 0;
        } else {
            *token_ptr = (u8)(lit_len << 4);
        }

        /* リテラルバイト列 */
        if (op + lit_len > op_end) return 0;
        for (i = 0; i < lit_len; i++) {
            *op++ = anchor[i];
        }

        /* Offset (2バイト LE) */
        if (op + 2 > op_end) return 0;
        *op++ = (u8)(offset & 0xFF);
        *op++ = (u8)((offset >> 8) & 0xFF);

        /* マッチ長 */
        ml = match_len - LZ4_MINMATCH;
        if (ml >= 15) {
            *token_ptr |= 0x0F;
            op = lz4_write_len(op, op_end, ml - 15);
            if (!op) return 0;
        } else {
            *token_ptr |= (u8)ml;
        }

        /* ポインタ前進 */
        ip += match_len;
        anchor = ip;
    }

    /* 最終リテラル (末尾のデータ) */
    {
        int last_lit = (int)(ip_end - anchor);
        u8 *token_ptr2;

        if (op >= op_end) return 0;
        token_ptr2 = op++;

        if (last_lit >= 15) {
            *token_ptr2 = 0xF0;
            op = lz4_write_len(op, op_end, last_lit - 15);
            if (!op) return 0;
        } else {
            *token_ptr2 = (u8)(last_lit << 4);
        }

        if (op + last_lit > op_end) return 0;
        for (i = 0; i < last_lit; i++) {
            *op++ = anchor[i];
        }
    }

    return (int)(op - dst);
}

/* ======================================================================== */
/*  メインエントリ                                                           */
/* ======================================================================== */

int main(int argc, char **argv, KernelAPI *api)
{
    const char *mode;
    const char *input_path;
    const char *output_path;
    int fd;
    int sz;
    OS32_Stat st;

    if (argc < 3) {
        api->kprintf(0xE1, "%s", "Usage: lz4 c|d|t INPUT [OUTPUT]\n");
        return 1;
    }

    mode = argv[1];
    input_path = argv[2];

    /* 入力ファイル読み込み */
    fd = api->sys_open((char *)input_path, KAPI_O_RDONLY);
    if (fd < 0) {
        api->kprintf(0xE1 | 0x40, "lz4: %s not found\n", input_path);
        return 1;
    }
    if (api->sys_fstat(fd, &st) != 0 || st.st_size <= 0 ||
        (u32)st.st_size > sizeof(src_buf)) {
        api->kprintf(0xE1 | 0x40, "%s",
                     "lz4: file too large or stat error\n");
        api->sys_close(fd);
        return 1;
    }
    sz = api->sys_read(fd, src_buf, st.st_size);
    api->sys_close(fd);
    if (sz != st.st_size) {
        api->kprintf(0xE1 | 0x40, "%s", "lz4: read error\n");
        return 1;
    }

    if (mode[0] == 'c') {
        /* === 圧縮 === */
        int enc_sz;
        u16 *hash_table;
        int wfd;

        if (argc < 4) {
            api->kprintf(0xE1 | 0x40, "%s",
                         "lz4: output path required for compress\n");
            return 1;
        }
        output_path = argv[3];

        /* ハッシュテーブル動的確保 (16384 x 2 = 32KB) */
        hash_table = (u16 *)api->mem_alloc(
            LZ4_HASHTABLESIZE * sizeof(u16));
        if (!hash_table) {
            api->kprintf(0xE1 | 0x40, "%s",
                         "lz4: memory allocation failed\n");
            return 1;
        }

        /* ヘッダ: 先頭4バイトにオリジナルサイズ (LE) */
        dst_buf[0] = (u8)(sz & 0xFF);
        dst_buf[1] = (u8)((sz >> 8) & 0xFF);
        dst_buf[2] = (u8)((sz >> 16) & 0xFF);
        dst_buf[3] = (u8)((sz >> 24) & 0xFF);

        enc_sz = lz4_encode(src_buf, sz,
                            dst_buf + 4, (int)sizeof(dst_buf) - 4,
                            hash_table);
        api->mem_free(hash_table);

        if (enc_sz <= 0) {
            api->kprintf(0xE1 | 0x40, "%s", "lz4: compression failed\n");
            return 1;
        }

        /* 出力ファイル書き込み */
        wfd = api->sys_open((char *)output_path,
                            KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
        if (wfd < 0) {
            api->kprintf(0xE1 | 0x40,
                         "lz4: cannot create %s\n", output_path);
            return 1;
        }
        if (api->sys_write(wfd, dst_buf, enc_sz + 4) != enc_sz + 4) {
            api->kprintf(0xE1 | 0x40, "%s", "lz4: write error\n");
            api->sys_close(wfd);
            return 1;
        }
        api->sys_close(wfd);

        api->kprintf(0xE1, "%s: %d -> %d (%d%%)\n",
                     input_path, sz, enc_sz + 4,
                     sz > 0 ? (enc_sz * 100 / sz) : 0);

    } else if (mode[0] == 'd') {
        /* === 展開 === */
        u32 orig_size;
        int dec_sz;
        int wfd;

        if (argc < 4) {
            api->kprintf(0xE1 | 0x40, "%s",
                         "lz4: output path required for decompress\n");
            return 1;
        }
        output_path = argv[3];

        /* ヘッダ: 先頭4バイトからオリジナルサイズ読み取り */
        if (sz < 4) {
            api->kprintf(0xE1 | 0x40, "%s",
                         "lz4: file too small (no header)\n");
            return 1;
        }
        orig_size = (u32)src_buf[0] | ((u32)src_buf[1] << 8) |
                    ((u32)src_buf[2] << 16) | ((u32)src_buf[3] << 24);

        if (orig_size > sizeof(dst_buf)) {
            api->kprintf(0xE1 | 0x40, "%s",
                         "lz4: decompressed size too large\n");
            return 1;
        }

        /* LZ4ブロック展開 */
        dec_sz = lz4_decode(src_buf + 4, sz - 4,
                            dst_buf, (int)orig_size);
        if (dec_sz < 0) {
            api->kprintf(0xE1 | 0x40,
                         "lz4: decompression failed (err=%d)\n", dec_sz);
            return 1;
        }

        /* 出力ファイル書き込み */
        wfd = api->sys_open((char *)output_path,
                            KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
        if (wfd < 0) {
            api->kprintf(0xE1 | 0x40,
                         "lz4: cannot create %s\n", output_path);
            return 1;
        }
        if (api->sys_write(wfd, dst_buf, dec_sz) != dec_sz) {
            api->kprintf(0xE1 | 0x40, "%s", "lz4: write error\n");
            api->sys_close(wfd);
            return 1;
        }
        api->sys_close(wfd);

        api->kprintf(0xE1, "%s: %d -> %d (decompressed)\n",
                     input_path, sz, dec_sz);

    } else if (mode[0] == 't') {
        /* === テスト (圧縮率表示のみ) === */
        int enc_sz;
        u16 *hash_table;

        hash_table = (u16 *)api->mem_alloc(
            LZ4_HASHTABLESIZE * sizeof(u16));
        if (!hash_table) {
            api->kprintf(0xE1 | 0x40, "%s",
                         "lz4: memory allocation failed\n");
            return 1;
        }

        enc_sz = lz4_encode(src_buf, sz,
                            dst_buf, (int)sizeof(dst_buf),
                            hash_table);
        api->mem_free(hash_table);

        if (enc_sz <= 0) {
            api->kprintf(0xE1 | 0x40, "%s", "lz4: compression failed\n");
            return 1;
        }

        api->kprintf(0xE1, "Original:   %d bytes\n", sz);
        api->kprintf(0xE1, "Compressed: %d bytes (+4B header)\n", enc_sz);
        api->kprintf(0xE1, "Ratio:      %d%%\n",
                     sz > 0 ? (enc_sz * 100 / sz) : 0);

    } else {
        api->kprintf(0xE1 | 0x40, "lz4: unknown mode '%s'\n", mode);
        return 1;
    }

    return 0;
}
