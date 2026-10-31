/* Kana accumulation, commitment and conversion key processing. */

/* ======================================================================== */
/*  かなバッファへのローマ字出力追記 (共通処理)                              */
/* ======================================================================== */

static void append_rk_output(void)
{
    int olen = (int)kstrlen(g_ime.rk.output);
    if (g_ime.kana_len + olen <= (int)sizeof(g_ime.kana_buf) - 1) {
        kmemcpy(g_ime.kana_buf + g_ime.kana_len, g_ime.rk.output, olen);
        g_ime.kana_len += olen;
        g_ime.kana_buf[g_ime.kana_len] = '\0';
    }
}

/* ======================================================================== */
/*  確定処理                                                                 */
/* ======================================================================== */

/* かなバッファの内容を確定出力バッファにコミット (無変換確定) */
static void commit_kana_direct(void)
{
    if (ime_rk_flush_n(&g_ime.rk)) {
        append_rk_output();
    }

    if (g_ime.kana_len > 0) {
        if (g_ime.mode == IME_MODE_KATAKANA) {
            ime_hira_to_kata(g_ime.kana_buf);
        }
        kmemcpy(g_ime.commit_buf, g_ime.kana_buf, g_ime.kana_len);
        g_ime.commit_len = g_ime.kana_len;
        g_ime.commit_pos = 0;
        g_ime.commit_buf[g_ime.commit_len] = '\0';
        g_ime.kana_buf[0] = '\0';
        g_ime.kana_len = 0;
    }
}

/* 変換候補を確定する (残りかなはバッファに保持) */
static void commit_candidate(void)
{
    if (g_ime.candidate_idx < g_ime.result_count) {
        int len = (int)kstrlen(g_ime.results[g_ime.candidate_idx].kanji);
        kmemcpy(g_ime.commit_buf, g_ime.results[g_ime.candidate_idx].kanji, len);
        g_ime.commit_len = len;
        g_ime.commit_pos = 0;
        g_ime.commit_buf[g_ime.commit_len] = '\0';

        /* 学習: ユーザーが選択した候補を記録 */
        ime_dict_learn(&g_ime.dict,
                       g_ime.results[g_ime.candidate_idx].yomi,
                       g_ime.results[g_ime.candidate_idx].kanji);
    }

    /* 最長一致: 変換対象部分だけ消費し、残りをバッファ先頭に移動 */
    if (g_ime.convert_len > 0 && g_ime.convert_len < g_ime.kana_len) {
        int remaining = g_ime.kana_len - g_ime.convert_len;
        int i;
        for (i = 0; i < remaining; i++) {
            g_ime.kana_buf[i] = g_ime.kana_buf[g_ime.convert_len + i];
        }
        g_ime.kana_len = remaining;
        g_ime.kana_buf[remaining] = '\0';
    } else {
        g_ime.kana_buf[0] = '\0';
        g_ime.kana_len = 0;
    }

    g_ime.converting = 0;
    g_ime.candidate_idx = 0;
    g_ime.result_count = 0;
    g_ime.convert_len = 0;
}

/* ======================================================================== */
/*  IME コアロジック: キー処理                                               */
/* ======================================================================== */

/* === 変換候補表示中 (IME_ST_CONVERT / IME_ST_CANDLIST) の操作 ===
 * 戻り値は ime_process_key と同じ。その他のキーは候補を確定して -1 (透過) を返し、
 * 通常入力の処理には回さない。印字可能文字は確定文字列の末尾へ積む。 */
static inline int ime_candidate_key(u8 ascii, u8 scancode)
{
    int n;
    int target;
    int max_page;

    /* 1..9 キーのハンドリング (CANDLIST 中のみ有効) */
    if (g_ime.state == IME_ST_CANDLIST && ascii >= '1' && ascii <= '9') {
        n = ascii - '0';
        target = g_ime.page * g_ime.per_page + (n - 1);
        if (target < g_ime.result_count) {
            g_ime.candidate_idx = target;
            commit_candidate();
            g_ime.state = IME_ST_INPUT;
            tvram_set_scroll_reserve(1); /* インライン保護に戻す */
            candlist_clear(); /* リスト領域を消去 */
            if (g_ime.kana_len > 0) {
                preedit_draw();
            } else {
                preedit_clear();
            }
            return 1;
        }
        return 0;
    }

    /* Space キー (次候補送り) */
    if (scancode == KEY_SPACE) {
        g_ime.candidate_idx++;
        if (g_ime.candidate_idx >= g_ime.result_count) {
            g_ime.candidate_idx = 0;
        }
        /* 表示ページを同期 */
        g_ime.page = g_ime.candidate_idx / g_ime.per_page;
        preedit_draw();
        return 0;
    }

    /* Down または XFER キー (変換キー) */
    if (scancode == KEY_DOWN || scancode == KEY_XFER) {
        if (g_ime.state == IME_ST_CONVERT) {
            /* 候補リストウィンドウ展開 */
            candlist_save();
            g_ime.state = IME_ST_CANDLIST;
            g_ime.page = g_ime.candidate_idx / g_ime.per_page;
            /* 3行+インライン1行で計4行を保護 */
            tvram_set_scroll_reserve(4);
            preedit_draw();
        } else {
            /* CANDLIST 中は次候補 */
            g_ime.candidate_idx++;
            if (g_ime.candidate_idx >= g_ime.result_count) {
                g_ime.candidate_idx = 0;
            }
            g_ime.page = g_ime.candidate_idx / g_ime.per_page;
            preedit_draw();
        }
        return 0;
    }

    /* Up キー (前候補) */
    if (scancode == KEY_UP) {
        if (g_ime.state == IME_ST_CONVERT) {
            /* リスト展開して前候補 */
            candlist_save();
            g_ime.state = IME_ST_CANDLIST;
            g_ime.candidate_idx = (g_ime.candidate_idx == 0) ? (g_ime.result_count - 1) : (g_ime.candidate_idx - 1);
            g_ime.page = g_ime.candidate_idx / g_ime.per_page;
            tvram_set_scroll_reserve(4);
            preedit_draw();
        } else {
            /* CANDLIST 中は前候補 */
            g_ime.candidate_idx = (g_ime.candidate_idx == 0) ? (g_ime.result_count - 1) : (g_ime.candidate_idx - 1);
            g_ime.page = g_ime.candidate_idx / g_ime.per_page;
            preedit_draw();
        }
        return 0;
    }

    /* RollDown / Right (次ページ) */
    if (scancode == KEY_ROLLDOWN || scancode == KEY_RIGHT) {
        if (g_ime.state == IME_ST_CANDLIST) {
            max_page = (g_ime.result_count - 1) / g_ime.per_page;
            if (g_ime.page < max_page) {
                g_ime.page++;
                g_ime.candidate_idx = g_ime.page * g_ime.per_page;
                preedit_draw();
            }
        }
        return 0;
    }

    /* RollUp / Left (前ページ) */
    if (scancode == KEY_ROLLUP || scancode == KEY_LEFT) {
        if (g_ime.state == IME_ST_CANDLIST) {
            if (g_ime.page > 0) {
                g_ime.page--;
                g_ime.candidate_idx = g_ime.page * g_ime.per_page;
                preedit_draw();
            }
        }
        return 0;
    }

    /* Return (確定) */
    if (scancode == KEY_RETURN) {
        commit_candidate();
        g_ime.state = IME_ST_INPUT;
        tvram_set_scroll_reserve(1); /* インライン保護に戻す */
        candlist_clear(); /* リスト領域を消去 */
        if (g_ime.kana_len > 0) {
            preedit_draw();  /* 残りかなを表示 */
        } else {
            preedit_clear();
        }
        return 1;
    }

    /* ESC / BS (キャンセル / 畳む) */
    if (ascii == ASCII_ESC || scancode == KEY_BS) {
        if (g_ime.state == IME_ST_CANDLIST) {
            /* CANDLIST -> CONVERT に戻す (リストを畳む) */
            candlist_clear();
            g_ime.state = IME_ST_CONVERT;
            tvram_set_scroll_reserve(1); /* 1行保護に戻す */
            preedit_draw();
        } else {
            /* 変換自体をキャンセル */
            g_ime.state = IME_ST_INPUT;
            g_ime.converting = 0;
            g_ime.candidate_idx = 0;
            g_ime.result_count = 0;
            g_ime.convert_len = 0;
            preedit_draw();
        }
        return 0;
    }

    /* その他のキーは候補確定後にフォールスルー */
    commit_candidate();
    g_ime.state = IME_ST_INPUT;
    tvram_set_scroll_reserve(1); /* インライン保護に戻す */
    candlist_clear(); /* リスト領域を消去 */
    if (g_ime.kana_len > 0) {
        preedit_draw();
    } else {
        preedit_clear();
    }
    /* Deliver the confirmed candidate before the printable key, through the
     * same queue for CUI readers and the WM. Candidate storage leaves room
     * for one ASCII byte and the terminator. */
    STATIC_ASSERT(sizeof(g_ime.commit_buf) > sizeof(g_ime.results[0].kanji),
                  ime_candidate_ascii_capacity);
    if (ascii >= ASCII_PRINT_FIRST && ascii <= ASCII_PRINT_LAST) {
        g_ime.commit_buf[g_ime.commit_len++] = (char)ascii;
        g_ime.commit_buf[g_ime.commit_len] = '\0';
        return 1;
    }
    return -1;
}

/* === 通常入力中 (IME_ST_INPUT) の操作 ===
 * 戻り値は ime_process_key と同じ。 */
static inline int ime_input_key(u8 ascii, u8 scancode)
{
    int count;

    /* スペースキー: 変換開始 (最長一致法) */
    if (scancode == KEY_SPACE && (g_ime.kana_len > 0 || g_ime.rk.n_wait)) {
        int try_len;
        char saved_char;

        if (ime_rk_flush_n(&g_ime.rk)) {
            append_rk_output();
        }

        /* 完全一致 → 最長前方一致の順で検索 */
        try_len = g_ime.kana_len;
        while (try_len > 0) {
            saved_char = g_ime.kana_buf[try_len];
            g_ime.kana_buf[try_len] = '\0';

            count = ime_dict_search(&g_ime.dict, g_ime.kana_buf,
                                    g_ime.results, IME_MAX_RESULTS);

            g_ime.kana_buf[try_len] = saved_char;

            if (count > 0) {
                g_ime.result_count = count;
                g_ime.candidate_idx = 0;
                g_ime.converting = 1;
                g_ime.state = IME_ST_CONVERT;
                g_ime.convert_len = try_len;
                preedit_draw();
                return 0;
            }

            /* 1文字分戻る (UTF-8後続バイトをスキップ) */
            try_len--;
            while (try_len > 0 && UTF8_IS_CONT((u8)g_ime.kana_buf[try_len])) {
                try_len--;
            }
        }

        /* 全く一致なし: かなのまま確定 */
        commit_kana_direct();
        preedit_clear();
        return 1;
    }

    /* Enter: かなバッファを直接確定 */
    if (scancode == KEY_RETURN) {
        if (g_ime.kana_len > 0 || g_ime.rk.preedit[0] != '\0') {
            commit_kana_direct();
            ime_rk_init(&g_ime.rk); /* Discard incomplete romaji after flushing n. */
            preedit_draw(); /* Keep the ON indicator even when nothing was committed. */
            return 1;
        }
        return -1;
    }

    /* ESC: かなバッファをクリア */
    if (ascii == ASCII_ESC) {
        if (g_ime.kana_len > 0 || g_ime.rk.preedit[0] != '\0') {
            g_ime.kana_buf[0] = '\0';
            g_ime.kana_len = 0;
            ime_rk_init(&g_ime.rk);
            preedit_draw();
            return 0;
        }
        return -1;
    }

    /* Backspace: かなバッファ末尾削除 */
    if (scancode == KEY_BS) {
        if (g_ime.rk.preedit[0] != '\0') {
            int plen = (int)kstrlen(g_ime.rk.preedit);
            if (plen > 0) {
                g_ime.rk.preedit[plen - 1] = '\0';
                g_ime.rk.n_wait = (kstrcmp(g_ime.rk.preedit, "n") == 0);
            }
            preedit_draw();
            return 0;
        }
        if (g_ime.kana_len > 0) {
            utf8_delete_last((u8 *)g_ime.kana_buf);
            g_ime.kana_len = (int)kstrlen(g_ime.kana_buf);
            preedit_draw();
            return 0;
        }
        return -1;
    }

    /* 英字/記号: ローマ字変換 */
    if (ascii >= ASCII_PRINT_FIRST && ascii <= ASCII_PRINT_LAST && scancode != KEY_SPACE) {
        if (ime_rk_append(&g_ime.rk, (char)ascii)) {
            append_rk_output();
        }
        preedit_draw();
        return 0;
    }

    /* その他のキー: 透過 */
    return -1;
}

/*
 * 戻り値: 1=commit_bufに確定データあり, 0=未確定, -1=キーを消費せず透過
 */
static int ime_process_key(int keydata)
{
    u8 scancode = (u8)((keydata >> 8) & SCANCODE_KEY);
    u8 ascii = (u8)(keydata & 0xFF);

    if (g_ime.state != IME_ST_INPUT)
        return ime_candidate_key(ascii, scancode);
    return ime_input_key(ascii, scancode);
}

