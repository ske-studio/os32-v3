/* ======================================================================== */
/*  IME.C — OS32 カーネルFEP メインロジック                                  */
/*                                                                          */
/*  IME状態管理・キー処理・プリエディット表示・公開API                        */
/*  ローマ字変換は ime_romkana.c、辞書検索は ime_dict.c に分離               */
/* ======================================================================== */

#include "ime.h"
#include "kbd.h"
#include "tvram.h"
#include "console.h"
#include "utf8.h"
#include "kprintf.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "kmalloc.h"

/* ======================================================================== */
/*  グローバル IME 状態                                                      */
/* ======================================================================== */

static IME_State g_ime;

/* 辞書ファイルのパス */
#define IME_DICT_PATH  "/db/fep.db"

/* ======================================================================== */
/*  プリエディット表示 (TVRAM 25行目 = y=24)                                 */
/* ======================================================================== */

#define IME_PREEDIT_ROW  24   /* TVRAM最終行 (0始まり) */

static int draw_utf8(int x, int y, const char *str, u8 col)
{
    const u8 *p = (const u8 *)str;
    int cur_x = x;
    utf8_decode_t dec;

    if (!g_ime.render) {
        return 0;
    }

    while (*p && cur_x < TVRAM_COLS) {
        dec = utf8_decode(p);
        int w = g_ime.render->putw(cur_x, y, dec.codepoint, col);
        if (w <= 0) {
            break;
        }
        cur_x += w;
        p += dec.bytes_used;
    }
    return cur_x - x;
}

static int page_count(const IME_State *s)
{
    int base = s->page * s->per_page;
    int rem  = s->result_count - base;
    return (rem > s->per_page) ? s->per_page : rem;
}

#define CANDLIST_SAVE_ROWS  3
#define CANDLIST_SAVE_COLS  80

static u16 *g_cand_save_text = NULL;
static u8  *g_cand_save_attr = NULL;
static int g_cand_has_saved = 0;

static void candlist_save(void)
{
    int rows;
    int top;
    int y, x;

    rows = (g_ime.per_page + 2) / 3;
    top = IME_PREEDIT_ROW - rows;

    if (g_cand_has_saved) return;
    if (!g_cand_save_text || !g_cand_save_attr) return;

    for (y = 0; y < rows; y++) {
        for (x = 0; x < TVRAM_COLS; x++) {
            tvram_readchar_at(x, top + y,
                              &g_cand_save_text[y * CANDLIST_SAVE_COLS + x],
                              &g_cand_save_attr[y * CANDLIST_SAVE_COLS + x]);
        }
    }
    g_cand_has_saved = 1;
}

static void candlist_restore(void)
{
    int rows;
    int top;
    int y, x;

    if (!g_cand_has_saved) return;
    if (!g_cand_save_text || !g_cand_save_attr) return;

    rows = (g_ime.per_page + 2) / 3;
    top = IME_PREEDIT_ROW - rows;

    for (y = 0; y < rows; y++) {
        for (x = 0; x < TVRAM_COLS; x++) {
            u32 offset = (u32)(top + y) * TVRAM_BPR + (u32)x * 2;
            *(volatile u16 *)(TVRAM_BASE + offset) = g_cand_save_text[y * CANDLIST_SAVE_COLS + x];
            *(volatile u8 *)(TVRAM_ATTR + offset) = g_cand_save_attr[y * CANDLIST_SAVE_COLS + x];
        }
    }
    g_cand_has_saved = 0;
}

static void candlist_clear(void)
{
    if (g_cand_has_saved) {
        candlist_restore();
    }
}

static void candlist_draw(void)
{
    int rows = (g_ime.per_page + 2) / 3;
    int top  = IME_PREEDIT_ROW - rows;
    int base = g_ime.page * g_ime.per_page;
    int n    = page_count(&g_ime);
    int i;
    int y, x;
    int max_page;
    char pbuf[16];

    if (!g_ime.render) {
        return;
    }

    /* 描画前に一旦リスト表示領域をクリア */
    for (y = top; y < IME_PREEDIT_ROW; y++) {
        g_ime.render->clear_row(y, ATTR_WHITE);
    }

    for (i = 0; i < n; i++) {
        int gi = base + i;
        u8  col = (gi == g_ime.candidate_idx) ? ATTR_YELLOW : ATTR_WHITE;
        y = top + (i / 3);
        x = (i % 3) * 26;

        /* 番号 "1 " */
        g_ime.render->putc(x, y, '1' + i, ATTR_CYAN);
        g_ime.render->putc(x + 1, y, ' ', col);
        /* 候補文字列 */
        draw_utf8(x + 2, y, g_ime.results[gi].kanji, col);
    }

    /* ページ位置表示 "1/2" などを末尾（右端付近）に表示 */
    max_page = (g_ime.result_count - 1) / g_ime.per_page + 1;
    pbuf[0] = '0' + (g_ime.page + 1);
    pbuf[1] = '/';
    pbuf[2] = '0' + max_page;
    pbuf[3] = '\0';
    draw_utf8(74, IME_PREEDIT_ROW - 1, pbuf, ATTR_CYAN);
}

static void preedit_clear(void)
{
    if (g_ime.render) {
        g_ime.render->clear_row(IME_PREEDIT_ROW, ATTR_WHITE);
    }
}

static void preedit_draw(void)
{
    int x = 0;
    const u8 *p;

    if (!g_ime.render) {
        return;
    }

    preedit_clear();

    /* モード表示 */
    if (g_ime.mode == IME_MODE_HIRAGANA) {
        g_ime.render->putc(x++, IME_PREEDIT_ROW, '[', ATTR_CYAN);
        x += g_ime.render->putw(x, IME_PREEDIT_ROW, 0x3042, ATTR_CYAN); /* あ */
        g_ime.render->putc(x++, IME_PREEDIT_ROW, ']', ATTR_CYAN);
    } else if (g_ime.mode == IME_MODE_KATAKANA) {
        g_ime.render->putc(x++, IME_PREEDIT_ROW, '[', ATTR_CYAN);
        x += g_ime.render->putw(x, IME_PREEDIT_ROW, 0x30A2, ATTR_CYAN); /* ア */
        g_ime.render->putc(x++, IME_PREEDIT_ROW, ']', ATTR_CYAN);
    }

    if (g_ime.converting) {
        /* 変換候補表示 */
        x += g_ime.render->putw(x, IME_PREEDIT_ROW, 0x25BC, ATTR_YELLOW); /* ▼ */

        if (g_ime.candidate_idx < g_ime.result_count) {
            x += draw_utf8(x, IME_PREEDIT_ROW, g_ime.results[g_ime.candidate_idx].kanji, ATTR_WHITE);
        }

        /* 候補番号表示 */
        if (g_ime.result_count > 1 && x < TVRAM_COLS - 6) {
            char nbuf[8];
            int ni;
            g_ime.render->putc(x++, IME_PREEDIT_ROW, '(', ATTR_CYAN);
            nbuf[0] = '0' + ((g_ime.candidate_idx + 1) / 10);
            nbuf[1] = '0' + ((g_ime.candidate_idx + 1) % 10);
            nbuf[2] = '/';
            nbuf[3] = '0' + (g_ime.result_count / 10);
            nbuf[4] = '0' + (g_ime.result_count % 10);
            nbuf[5] = ')';
            nbuf[6] = '\0';
            for (ni = 0; nbuf[ni] && x < TVRAM_COLS; ni++) {
                g_ime.render->putc(x++, IME_PREEDIT_ROW, nbuf[ni], ATTR_CYAN);
            }
        }

        /* 候補リストがONなら展開 */
        if (g_ime.state == IME_ST_CANDLIST) {
            candlist_draw();
        }
    } else if (g_ime.kana_len > 0 || g_ime.rk.preedit[0] != '\0') {
        /* かなバッファ + 未確定ローマ字 */
        x += draw_utf8(x, IME_PREEDIT_ROW, g_ime.kana_buf, ATTR_GREEN);
        
        /* 未確定ローマ字 */
        p = (const u8 *)g_ime.rk.preedit;
        while (*p && x < TVRAM_COLS) {
            g_ime.render->putc(x++, IME_PREEDIT_ROW, (char)*p, ATTR_YELLOW);
            p++;
        }
    }
}

/* ======================================================================== */
/*  かなバッファへのローマ字出力追記 (共通処理)                              */
/* ======================================================================== */

static void append_rk_output(void)
{
    int olen = (int)kstrlen(g_ime.rk.output);
    if (g_ime.kana_len + olen < 127) {
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
        if (len > 255) len = 255;
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

/*
 * 戻り値: 1=commit_bufに確定データあり, 0=未確定, -1=キーを消費せず透過
 */
static int ime_process_key(int keydata)
{
    u8 scancode = (u8)((keydata >> 8) & 0x7F);
    u8 ascii = (u8)(keydata & 0xFF);
    int count;
    int n;
    int target;
    int max_page;

    /* === 変換候補表示中の操作 === */
    if (g_ime.state != IME_ST_INPUT) {
        /* 1..9 キーのハンドリング (CANDLIST 中のみ有効) */
        if (g_ime.state == IME_ST_CANDLIST && ascii >= '1' && ascii <= '9') {
            n = ascii - '0';
            target = g_ime.page * g_ime.per_page + (n - 1);
            if (target < g_ime.result_count) {
                g_ime.candidate_idx = target;
                commit_candidate();
                g_ime.state = IME_ST_INPUT;
                g_ime.converting = 0;
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
            g_ime.converting = 0;
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
        if (ascii == 0x1B || scancode == KEY_BS) {
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
        g_ime.converting = 0;
        tvram_set_scroll_reserve(1); /* インライン保護に戻す */
        candlist_clear(); /* リスト領域を消去 */
        if (g_ime.kana_len > 0) {
            preedit_draw();
        } else {
            preedit_clear();
        }
        return -1;
    }

    /* === 通常入力中 === */

    /* スペースキー: 変換開始 (最長一致法) */
    if (scancode == KEY_SPACE && g_ime.kana_len > 0) {
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
            while (try_len > 0 && ((u8)g_ime.kana_buf[try_len] & 0xC0) == 0x80) {
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
            preedit_clear();
            return 1;
        }
        return -1;
    }

    /* ESC: かなバッファをクリア */
    if (ascii == 0x1B) {
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
                if (plen - 1 == 0) g_ime.rk.n_wait = 0;
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
    if (ascii >= 0x20 && ascii <= 0x7E && scancode != KEY_SPACE) {
        if (ime_rk_append(&g_ime.rk, (char)ascii)) {
            append_rk_output();
        }
        preedit_draw();
        return 0;
    }

    /* その他のキー: 透過 */
    return -1;
}

/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

void ime_init(void)
{
    kmemset(&g_ime, 0, sizeof(IME_State));
    g_ime.mode = IME_MODE_OFF;
    g_ime.dict_loaded = 0;
    g_ime.state = IME_ST_INPUT;
    g_ime.per_page = 9;
    g_ime.render = &g_ime_render_tvram;
    ime_rk_init(&g_ime.rk);

    if (!g_cand_save_text) {
        g_cand_save_text = (u16 *)kmalloc(CANDLIST_SAVE_ROWS * CANDLIST_SAVE_COLS * sizeof(u16));
    }
    if (!g_cand_save_attr) {
        g_cand_save_attr = (u8 *)kmalloc(CANDLIST_SAVE_ROWS * CANDLIST_SAVE_COLS * sizeof(u8));
    }
}

void ime_toggle(void)
{
    if (g_ime.mode == IME_MODE_OFF) {
        if (!g_ime.dict_loaded) {
            if (ime_dict_open(&g_ime.dict, IME_DICT_PATH) == 0) {
                g_ime.dict_loaded = 1;
            } else {
                kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
                return;
            }
        }
        g_ime.mode = IME_MODE_HIRAGANA;
        g_ime.state = IME_ST_INPUT;
        g_ime.kana_buf[0] = '\0';
        g_ime.kana_len = 0;
        g_ime.converting = 0;
        ime_rk_init(&g_ime.rk);
        tvram_set_scroll_reserve(1);   /* 25 行目を保護 */
        preedit_draw();
    } else {
        /* かなバッファに残りがあれば確定 */
        if (g_ime.kana_len > 0 || g_ime.rk.preedit[0] != '\0') {
            commit_kana_direct();
        }
        g_ime.mode = IME_MODE_OFF;
        g_ime.converting = 0;
        g_ime.state = IME_ST_INPUT;
        tvram_set_scroll_reserve(0);   /* 保護解除 */
        candlist_clear();              /* 候補リスト領域を消去 */
        preedit_clear();
    }
}

int ime_is_active(void)
{
    return g_ime.mode != IME_MODE_OFF;
}

void ime_set_mode(int mode)
{
    /* FEP ON 時は辞書を遅延オープンする (ime_toggle と同じ)。
     * これを欠くと dict->db が NULL のまま ime_dict_search が常に
     * 0 件を返し、変換がかな確定にフォールバックする。 */
    if (mode != IME_MODE_OFF && !g_ime.dict_loaded) {
        if (ime_dict_open(&g_ime.dict, IME_DICT_PATH) == 0) {
            g_ime.dict_loaded = 1;
        } else {
            kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
            return;
        }
    }
    g_ime.mode = mode;
    if (mode != IME_MODE_OFF) {
        g_ime.state = IME_ST_INPUT;
        g_ime.converting = 0;
        tvram_set_scroll_reserve(1);
        preedit_draw();
    } else {
        tvram_set_scroll_reserve(0);
        candlist_clear();              /* 候補リスト領域を消去 */
        preedit_clear();
    }
}

int ime_get_mode(void)
{
    return g_ime.mode;
}

int ime_getchar(void)
{
    int keydata;
    int result;

    /* バッファに確定済みがあれば返す */
    if (g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }

    for (;;) {
        keydata = kbd_getkey();

        /* Shift+Space: ON/OFF問わず常に検出 */
        if (((keydata >> 8) & 0x7F) == KEY_SPACE &&
            (kbd_shift_state & SHIFT_SHIFT)) {
            ime_toggle();
            if (g_ime.commit_pos < g_ime.commit_len) {
                return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
            }
            continue;
        }

        /* FEP OFF: ASCII値をそのまま返す */
        if (g_ime.mode == IME_MODE_OFF) {
            u8 a = (u8)(keydata & 0xFF);
            if (a != 0) return (int)a;
            continue;
        }

        /* FEP ON: IME処理 */
        result = ime_process_key(keydata);
        if (result == 1 && g_ime.commit_pos < g_ime.commit_len) {
            return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
        }
        if (result == -1) {
            u8 a = (u8)(keydata & 0xFF);
            if (a != 0) return (int)a;
        }
    }
}

int ime_trygetchar(void)
{
    int keydata;
    int result;

    if (g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }

    keydata = kbd_trygetkey();
    if (keydata < 0) return -1;

    if (((keydata >> 8) & 0x7F) == KEY_SPACE &&
        (kbd_shift_state & SHIFT_SHIFT)) {
        ime_toggle();
        if (g_ime.commit_pos < g_ime.commit_len) {
            return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
        }
        return -1;
    }

    if (g_ime.mode == IME_MODE_OFF) {
        u8 a = (u8)(keydata & 0xFF);
        if (a != 0) return (int)a;
        return -1;
    }

    result = ime_process_key(keydata);
    if (result == 1 && g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }
    if (result == -1) {
        u8 a = (u8)(keydata & 0xFF);
        if (a != 0) return (int)a;
    }
    return -1;
}

/* ======================================================================== */
/*  ime_feed_key — WM (gshell) が打鍵を 1 件 FEP に通す (GUI v1.1 W2、K 依頼 1)  */
/*                                                                          */
/*  GUI 中はカーネルが cooked キューに積まない (K2) ので、ime_trygetkey 系は  */
/*  キーを引けない。WM が raw から作った keydata = (scancode << 8) | ascii を  */
/*  ここへ渡す。keydata < 0 は「新しいキーは無い、確定文字列の続きだけ」。      */
/*  戻り値:                                                                 */
/*    < 0            FEP が消費した (WM はアプリへ Key を配送しない)          */
/*    >= 0x100       素通り (keydata そのもの)                               */
/*    == 0x1B        素通りの ESC (scancode 0 は ESC だけで ascii は 0x1B)     */
/*    1..0xFF        確定文字列の 1 バイト (UTF-8)。続きは keydata < 0 で引く  */
/*  分類が一意なのは、確定文字列 (UTF-8) に 0x1B が現れないため。            */
/*  SHIFT+SPACE の on/off は WM が ime_toggle で行うので、ここでは見ない。      */
/* ======================================================================== */
int ime_feed_key(int keydata)
{
    int result;

    /* 確定済みの続きがあれば先にそれを返す */
    if (g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }
    if (keydata < 0) {
        return -1;
    }

    /* FEP OFF: 素通り */
    if (g_ime.mode == IME_MODE_OFF) {
        return keydata;
    }

    result = ime_process_key(keydata);
    if (result == 1 && g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }
    if (result == -1) {
        return keydata;      /* FEP が関与しないキー: 素通り */
    }
    return -1;               /* 消費 (未確定の更新など) */
}

/* ======================================================================== */
/*  ime_set_render — 描画バックエンドの差し替え (GUI v1.1 W2、K 依頼 2)       */
/*                                                                          */
/*  WM (gshell、CPL=0) が GFX 版の IME_Render 関数表を渡す。NULL で TVRAM 版  */
/*  に戻す。表の実体は呼び手が常駐している間だけ有効なので、gshell は終了時    */
/*  (CUI へ戻る前) に NULL を渡すこと。                                      */
/*  KAPI からは kernel/gui.c の gui_ime_set_render を経由する (owner 1 の     */
/*  CPL=0 の直呼びだけを通す門、2026-09-26)。ここはカーネル内の直呼び用で、   */
/*  呼び手を検査しない — gui_owner_exit(1) の NULL 戻しもここへ来る。         */
/* ======================================================================== */
void ime_set_render(void *table)
{
    g_ime.render = table ? (const IME_Render *)table : &g_ime_render_tvram;
}

int ime_getkey(void)
{
    int keydata;
    int result;

    /* バッファに確定済みがあれば 1バイトずつ返す (scancode=0) */
    if (g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }

    for (;;) {
        keydata = kbd_getkey();

        /* Shift+Space: ON/OFF問わず常に検出 */
        if (((keydata >> 8) & 0x7F) == KEY_SPACE &&
            (kbd_shift_state & SHIFT_SHIFT)) {
            ime_toggle();
            if (g_ime.commit_pos < g_ime.commit_len) {
                return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
            }
            continue;
        }

        /* FEP OFF: キーをそのまま返す */
        if (g_ime.mode == IME_MODE_OFF) {
            return keydata;
        }

        /* FEP ON: IME処理 */
        result = ime_process_key(keydata);
        if (result == 1 && g_ime.commit_pos < g_ime.commit_len) {
            return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
        }
        if (result == -1) {
            return keydata;
        }
        /* result == 0: 未確定、次のキーを待つ */
    }
}

int ime_trygetkey(void)
{
    int keydata;
    int result;

    /* バッファに確定済みがあれば 1バイトずつ返す (scancode=0) */
    if (g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }

    keydata = kbd_trygetkey();
    if (keydata < 0) return -1;

    /* Shift+Space: ON/OFF問わず常に検出 */
    if (((keydata >> 8) & 0x7F) == KEY_SPACE &&
        (kbd_shift_state & SHIFT_SHIFT)) {
        ime_toggle();
        if (g_ime.commit_pos < g_ime.commit_len) {
            return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
        }
        return -1;
    }

    /* FEP OFF: キーをそのまま返す */
    if (g_ime.mode == IME_MODE_OFF) {
        return keydata;
    }

    /* FEP ON: IME処理 */
    result = ime_process_key(keydata);
    if (result == 1 && g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }
    if (result == -1) {
        return keydata;
    }
    /* result == 0: 未確定、キーを消費したのでキーなし (-1) を返す */
    return -1;
}

/* 辞書バリアント切り替え */
int ime_switch_dict(int variant)
{
    const char *path;
    if (variant == 0) {
        path = "/db/fep_s.db";
    } else if (variant == 1) {
        path = "/db/fep.db";
    } else if (variant == 2) {
        path = "/db/fep_l.db";
    } else {
        return -1;
    }
    return ime_dict_reopen(&g_ime.dict, path);
}

/* ユーザー学習辞書操作のカーネル内ファサード */
int ime_user_list_facade(const char *yomi_prefix, void *out, int max)
{
    return ime_user_list(&g_ime.dict, yomi_prefix, (IME_UserEntry *)out, max);
}

int ime_user_delete_facade(const char *yomi, const char *kanji)
{
    return ime_user_delete(&g_ime.dict, yomi, kanji);
}

int ime_user_export_facade(const char *path)
{
    return ime_user_export(&g_ime.dict, path);
}

int ime_user_clear_facade(void)
{
    return ime_user_clear(&g_ime.dict);
}
