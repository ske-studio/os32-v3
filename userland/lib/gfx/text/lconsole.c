/*
 * lconsole.c - OS32 Logical Console (差分仮想VRAMレイヤー)
 *
 * テキストアプリ向けの汎用コンソールエンジン。
 * 論理VRAM (80x25セル) → KCGフォント描画による差分同期を提供。
 * C89 compatible
 */

#include "lconsole.h"
#include "libos32gfx.h"
#include "lib/utf8.h"

/* 1セル = 32bit: [31:16] 属性, [15:0] 文字コードまたはJIS */
static unsigned int logical_vram[LCONS_H][LCONS_W];
static unsigned int physical_vram[LCONS_H][LCONS_W];

/* ====== 属性→色変換テーブル (256エントリ) ====== */
static LConsAttrMap attr_map[256];

/* ====== カーソル状態 ====== */
static int cursor_x = 0;
static int cursor_y = 0;
static int cursor_visible = 0;

/* ============================================================ */
/*  初期化・クリア                                               */
/* ============================================================ */

void lcons_init(void) {
    lcons_reset_attr_map();
    lcons_clear();
}

void lcons_clear(void) {
    int i, j;
    for (i = 0; i < LCONS_H; i++) {
        for (j = 0; j < LCONS_W; j++) {
            /* 属性 0x07 (標準色)、文字 ' ' */
            logical_vram[i][j] = ((unsigned int)0x07 << 16) | ' ';
            physical_vram[i][j] = 0xFFFFFFFF; /* Force sync at next time */
        }
    }
}

/* ============================================================ */
/*  属性→色変換テーブル管理                                      */
/* ============================================================ */

/*
 * デフォルトの属性→色マッピングを設定
 * PC-98テキスト属性のビットレイアウトに基づく初期化
 */
void lcons_reset_attr_map(void) {
    int i;
    /* 全エントリをデフォルト (fg=0x07 グレー, bg=0x00 黒) に初期化 */
    for (i = 0; i < 256; i++) {
        attr_map[i].fg = 0x07;
        attr_map[i].bg = 0x00;
    }
    /* VZ互換マッピング */
    /* 0xE1 = 白文字, 通常表示 (ATR_NORMAL) */
    attr_map[0xE1].fg = 0x07;
    attr_map[0xE1].bg = 0x00;
    /* 0xE5 = 白文字, 反転表示 (ATR_STATUS / ATR_FUNCBAR) */
    attr_map[0xE5].fg = 0x0F;
    attr_map[0xE5].bg = 0x01;
    /* 0xA1 = シアン文字, 通常表示 (ATR_LINENUM / ATR_NEWLINE / ATR_EOF) */
    attr_map[0xA1].fg = 0x03;
    attr_map[0xA1].bg = 0x00;
    /* 0x07 = 標準色 (デフォルト) */
    attr_map[0x07].fg = 0x07;
    attr_map[0x07].bg = 0x00;
    /* 0x41 = 赤色 (kprintf属性) */
    attr_map[0x41].fg = 0x04;
    attr_map[0x41].bg = 0x00;
}

void lcons_set_attr_map(unsigned char attr, unsigned char fg, unsigned char bg) {
    attr_map[attr].fg = fg;
    attr_map[attr].bg = bg;
}

/* ============================================================ */
/*  セル操作                                                     */
/* ============================================================ */

void lcons_putc(int x, int y, char ch, unsigned char attr) {
    if (x >= 0 && x < LCONS_W && y >= 0 && y < LCONS_H) {
        logical_vram[y][x] = ((unsigned int)attr << 16) | (unsigned char)ch;
    }
}

void lcons_putkanji(int x, int y, unsigned short jis, unsigned char attr) {
    if (x >= 0 && x < LCONS_W - 1 && y >= 0 && y < LCONS_H) {
        logical_vram[y][x] = ((unsigned int)attr << 16) | jis;
        /* 漢字の右半分は 0xFFFF を入れて連結を示す */
        logical_vram[y][x+1] = ((unsigned int)attr << 16) | 0xFFFF;
    }
}

void lcons_fill_rect(int start_x, int start_y, int w, int h, char ch, unsigned char attr) {
    int x, y;
    for (y = start_y; y < start_y + h; y++) {
        for (x = start_x; x < start_x + w; x++) {
            lcons_putc(x, y, ch, attr);
        }
    }
}

void lcons_clear_line(int y, unsigned char attr) {
    lcons_fill_rect(0, y, LCONS_W, 1, ' ', attr);
}

/* ============================================================ */
/*  文字列描画                                                   */
/* ============================================================ */

/*
 * 数値を文字列に変換 (簡易sprintf代替)
 * 戻り値: 桁数
 */
int lcons_int_to_str(int val, char *buf) {
    int dc = 0;
    int tmp = val;
    int i;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    do { dc++; tmp /= 10; } while (tmp > 0);
    tmp = val;
    for (i = 0; i < dc; i++) {
        buf[dc - 1 - i] = '0' + (tmp % 10);
        tmp /= 10;
    }
    buf[dc] = '\0';
    return dc;
}

/*
 * フォーマット付き数値描画
 * right_align=1 の場合は指定widthの右寄せ、0の場合は左寄せ(後ろをpad_charで埋める)
 * 戻り値: 描画した文字幅
 */
int lcons_put_int(int x, int y, int val, int width, int right_align, char pad_char, unsigned char attr)
{
    char num_buf[16];
    int digit_count = lcons_int_to_str(val, num_buf);
    int i;
    char out_buf[32];
    int bp = 0;
    
    if (right_align) {
        for (i = 0; i < width - digit_count && bp < 31; i++) out_buf[bp++] = pad_char;
    }
    for (i = 0; i < digit_count && bp < 31; i++) out_buf[bp++] = num_buf[i];
    if (!right_align) {
        for (i = 0; i < width - digit_count && bp < 31; i++) out_buf[bp++] = pad_char;
    }
    out_buf[bp] = '\0';
    
    lcons_put_string(x, y, out_buf, attr);
    return bp;
}

/*
 * SJIS→JIS変換 (内部用)
 */
static unsigned short sjis_to_jis(unsigned char hi, unsigned char lo)
{
    unsigned short jhi, jlo;
    hi -= (hi <= 0x9F) ? 0x71 : 0xB1;
    hi = (hi << 1) + 1;
    if (lo > 0x7F) lo--;
    if (lo >= 0x9E) {
        lo -= 0x7D;
        hi++;
    } else {
        lo -= 0x1F;
    }
    jhi = hi + 0x20;
    jlo = lo + 0x20;
    return (jhi << 8) | jlo;
}

/*
 * 文字列表示 (SJIS/ASCII対応)
 * 戻り値: 描画後のX座標
 */
int lcons_put_string(int x, int y, const char *s, unsigned char attr)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p && x < LCONS_W) {
        if ((*p >= 0x81 && *p <= 0x9F) || (*p >= 0xE0 && *p <= 0xFC)) {
            if (*(p+1) != '\0') {
                u16 jis = sjis_to_jis(*p, *(p+1));
                if (x + 1 < LCONS_W) {
                    lcons_putkanji(x, y, jis, attr);
                    x += 2;
                } else {
                    lcons_putc(x, y, ' ', attr);
                    x += 1;
                }
                p += 2;
            } else break;
        } else {
            lcons_putc(x, y, (char)*p, attr);
            x++;
            p++;
        }
    }
    return x;
}

/*
 * 1文字分のUTF-8デコード・描画処理
 * dec は utf8_decode_t* (void*でinclude依存回避)
 * 戻り値: 消費したX座標の増分 (1 or 2)
 */
int lcons_put_utf8_char(int x, int y, void *dec_ptr, unsigned char attr)
{
    utf8_decode_t *dec = (utf8_decode_t *)dec_ptr;
    if (dec->codepoint >= 0x20 && dec->codepoint <= 0x7E) {
        lcons_putc(x, y, (char)dec->codepoint, attr);
        return 1;
    } else {
        /* ANKチェック(比較演算のみ)をJISテーブル(131KB)より先に判定 */
        u8 ank = unicode_to_ank(dec->codepoint);
        if (ank != 0) {
            lcons_putc(x, y, (char)ank, attr);
            return 1;
        } else {
            u16 jis = unicode_to_jis(dec->codepoint);
            if (jis != 0) {
                if (x + 1 < LCONS_W) {
                    lcons_putkanji(x, y, jis, attr);
                    return 2;
                } else {
                    lcons_putc(x, y, ' ', attr);
                    return 1;
                }
            }
            lcons_putc(x, y, '.', attr);
            return 1;
        }
    }
}

/*
 * UTF-8文字列描画 (戻り値は描画後のX座標)
 */
int lcons_put_utf8_string(int x, int y, const char *utf8_str, unsigned char attr)
{
    const unsigned char *p = (const unsigned char *)utf8_str;
    while (*p && x < LCONS_W) {
        utf8_decode_t dec = utf8_decode(p);
        x += lcons_put_utf8_char(x, y, &dec, attr);
        p += dec.bytes_used;
    }
    return x;
}

/* ============================================================ */
/*  カーソル管理                                                 */
/* ============================================================ */

void lcons_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
}

void lcons_show_cursor(int visible) {
    cursor_visible = visible;
}

/* ============================================================ */
/*  ポップアップ描画                                             */
/* ============================================================ */

void lcons_draw_box(int x, int y, int w, int h, unsigned char attr) {
    int bx, by;
    for (by = 0; by < h; by++) {
        for (bx = 0; bx < w; bx++) {
            char ch = ' ';
            if (by == 0 || by == h - 1) ch = '-';
            else if (bx == 0 || bx == w - 1) ch = '|';
            lcons_putc(x + bx, y + by, ch, attr);
        }
    }
}

/* ============================================================ */
/*  差分同期 (物理VRAM)                                          */
/* ============================================================ */

void lcons_sync_vram(void) {
    int y, x;

    for (y = 0; y < LCONS_H; y++) {
        int is_diff = 0;
        /* 行単位で差分チェック */
        for (x = 0; x < LCONS_W; x++) {
            if (logical_vram[y][x] != physical_vram[y][x]) {
                is_diff = 1;
                break;
            }
        }
        if (!is_diff) {
            continue;
        }

        /* 差分セルのグループ化 (連続する変更セルの背景を一括で塗り、文字を描画する) */
        {
            int start_x = -1;
            for (x = 0; x <= LCONS_W; x++) {
                if (x < LCONS_W && logical_vram[y][x] != physical_vram[y][x]) {
                    /* 漢字のテイル (0xFFFF) で差分が検出された場合、ヘッド(x-1)も含める */
                    unsigned int code = logical_vram[y][x] & 0xFFFF;
                    if (code == 0xFFFF) {
                        if (start_x == -1 && x > 0) start_x = x - 1; /* Expand block to left */
                    } else {
                        if (start_x == -1) start_x = x;
                    }
                } else {
                    if (start_x != -1) {
                        /* [start_x, x-1] の範囲が連続して変更されている */
                        int end_x = x;
                        int bx;
                        int run_start = start_x;
                        unsigned char cur_attr = (logical_vram[y][start_x] >> 16) & 0xFF;

                        /* 1. まずこの差分ブロックの背景を属性テーブルで塗る */
                        for (bx = start_x; bx <= end_x; bx++) {
                            unsigned char a = (bx < end_x) ? ((logical_vram[y][bx] >> 16) & 0xFF) : 0;
                            if (bx == end_x || a != cur_attr) {
                                unsigned char bg_color = attr_map[cur_attr].bg;
                                gfx_fill_rect(run_start * 8, y * 16, (bx - run_start) * 8, 16, bg_color);
                                if (bx < end_x) {
                                    cur_attr = a;
                                    run_start = bx;
                                }
                            }
                        }

                        /* 2. 背景を透過にして文字を描画 */
                        for (bx = start_x; bx < end_x; bx++) {
                            unsigned int val = logical_vram[y][bx];
                            unsigned char a = (val >> 16) & 0xFF;
                            unsigned int code = val & 0xFFFF;
                            unsigned char fg_color = attr_map[a].fg;

                            if (code == 0xFFFF) continue; /* 漢字の右側セルはスキップ */
                            if (code == 0x0020) continue; /* スペースは背景塗りのみで描画なし */

                            if (code > 0xFF) {
                                kcg_draw_kanji(bx * 8, y * 16, code, fg_color, 0xFF); /* bg_color=0xFF は透過の意 */
                                bx++; /* 漢字は2セル占有 */
                            } else if (code > 0) {
                                kcg_draw_ank(bx * 8, y * 16, code, fg_color, 0xFF);
                            }
                        }

                        /* 3. 状態の同期 */
                        for (bx = start_x; bx < end_x; bx++) {
                            physical_vram[y][bx] = logical_vram[y][bx];
                        }

                        start_x = -1;
                    }
                }
            }
        }
    }

    /* カーソル描画 (差分同期後) */
    if (cursor_visible &&
        cursor_x >= 0 && cursor_x < LCONS_W &&
        cursor_y >= 0 && cursor_y < LCONS_H) {
        /* カーソル位置に下線を描画 */
        unsigned int val = logical_vram[cursor_y][cursor_x];
        unsigned char a = (val >> 16) & 0xFF;
        unsigned char fg = attr_map[a].fg;
        gfx_hline(cursor_x * 8, cursor_y * 16 + 15, 8, fg);
    }
}
