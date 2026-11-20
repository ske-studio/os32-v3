    /* メインデコードループ */
    for (;;) {
        int token;
        int lit_len;
        int match_len;
        int offset;
        const u8 *match_src;
        int i;

        /* トークン読み込み */
        if (ip >= ip_end)
            break;
        token = *ip++;

        /* --- リテラル --- */
        lit_len = (token >> LZ4_TOKEN_BITS) & LZ4_TOKEN_MASK;
        if (lit_len == LZ4_LEN_EXT) {
            /* 拡張リテラル長 */
            int s;
            do {
                if (ip >= ip_end) return -1;
                s = *ip++;
                lit_len += s;
                /* 壊れた入力での長さ累積 (int オーバーフロー) を止める */
                if (lit_len > dst_capacity) return -1;
            } while (s == 255);
        }

        /* リテラルバイト列をコピー。
         * ip + lit_len 形式はポインタラップですり抜けるので差分比較 */
        if (lit_len > (int)(ip_end - ip)) return -1;
        if (lit_len > (int)(op_end - op)) return LZ4_ERR_OUTPUT;
        for (i = 0; i < lit_len; i++) {
            *op++ = *ip++;
        }

        /* 最終シーケンス判定: 入力を全て消費したらここで終了 */
        if (ip >= ip_end)
            break;

        /* --- Offset (2バイト LE) --- */
        if (ip + LZ4_OFFSET_BYTES > ip_end) return -1;
        offset = (int)ip[0] | ((int)ip[1] << 8);
        ip += LZ4_OFFSET_BYTES;

        if (offset == 0) return -1;        /* offset 0 は不正 */
        if (op - dst < offset) return -1;   /* 出力バッファ先頭を超える参照 */

        /* --- マッチ --- */
        match_len = (token & LZ4_TOKEN_MASK) + LZ4_MINMATCH;
        if ((token & LZ4_TOKEN_MASK) == LZ4_LEN_EXT) {
            /* 拡張マッチ長 */
            int s;
            do {
                if (ip >= ip_end) return -1;
                s = *ip++;
                match_len += s;
                if (match_len > dst_capacity) return -1;
            } while (s == 255);
        }

        /* マッチコピー (オーバーラップ対応: 1バイトずつコピー) */
        if (match_len > (int)(op_end - op)) return LZ4_ERR_OUTPUT;
        match_src = op - offset;
        for (i = 0; i < match_len; i++) {
            *op++ = match_src[i];
        }
    }

    return (int)(op - dst);
