/* ======================================================================== */
/*  FILER_CORE.C - GFXファイラー コアロジック + GFX描画                      */
/*                                                                          */
/*  ディレクトリ走査、カーソル操作、GFXオーバーレイ描画を一体で実装。        */
/*  libos32gfxに依存。                                                      */
/* ======================================================================== */

#include <string.h>
#include <stdio.h>
#include "libos32filer.h"
#include "libos32gfx.h"

/* ======================================================================== */
/*  内部状態                                                                */
/* ======================================================================== */

static KernelAPI *f_api;
static FilerState filer;

/* 色定数 (呼び出し元のパレットに依存) */
#define FC_BG         0   /* 背景 */
#define FC_TITLE      2   /* タイトル (スカイブルー) */
#define FC_TEXT       4   /* テキスト (白) */
#define FC_DIR_NAME   3   /* ディレクトリ名 (エメラルド) */
#define FC_FILE_NAME  4   /* ファイル名 (白) */
#define FC_SIZE       7   /* サイズ表示 (グレー) */
#define FC_CURSOR_BG  12  /* カーソル行背景 */
#define FC_BORDER     7   /* 枠線 */
#define FC_HINT       13  /* ヒント文字 */
#define FC_PANEL_BG   6   /* パネル背景 */

/* レイアウト定数 */
#define FP_X          32   /* パネル左 */
#define FP_Y          16   /* パネル上 */
#define FP_W          576  /* パネル幅 */
#define FP_H          360  /* パネル高さ */
#define FP_LINE_H     18   /* 1エントリの高さ */
#define FP_HEADER_H   24   /* ヘッダ行高さ */
#define FP_FOOTER_H   20   /* フッタ高さ */
#define FP_PAD        8    /* 内側パディング */

/* 表示可能エントリ数 */
#define FP_VISIBLE    ((FP_H - FP_HEADER_H - FP_FOOTER_H) / FP_LINE_H)

/* キーコード */
#define FK_UP       0x3A
#define FK_DOWN     0x3D
#define FK_HOME     0x3E

#define FK_KEYCODE(k)  (((k) >> 8) & 0x7F)
#define FK_KEYDATA(k)  ((k) & 0xFF)

/* ======================================================================== */
/*  拡張子フィルタ                                                          */
/* ======================================================================== */

static int str_ends_with(const char *s, const char *suffix)
{
    int slen, sflen, i;

    slen = 0;
    while (s[slen]) slen++;
    sflen = 0;
    while (suffix[sflen]) sflen++;

    if (sflen > slen) return 0;
    for (i = 0; i < sflen; i++) {
        char a = s[slen - sflen + i];
        char b = suffix[i];
        /* 大文字小文字無視 */
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

/* ======================================================================== */
/*  ディレクトリ走査                                                        */
/* ======================================================================== */

static void dir_callback(const DirEntry_Ext *entry, void *ctx)
{
    FilerEntry *e;
    int i;
    (void)ctx;

    /* フィルタチェック (ディレクトリは常に表示) */
    if (filer.filter_ext && entry->type != OS32_FILE_TYPE_DIR) {
        if (!str_ends_with(entry->name, filer.filter_ext)) return;
    }

    /* T23: 63 バイトで切った名前を載せると filer_get_selected_path() が
     * **別のファイルのパス**を返し、ディレクトリ移動も別の場所へ行く。
     * 載せずに件数だけ数える (cmd_filer.c の fl_ls_callback と同じ)。 */
    for (i = 0; entry->name[i]; i++) {}
    if (i > FILER_NAME_LEN - 1) { filer.dropped_count++; return; }

    if (filer.entry_count >= FILER_MAX_ENTRIES) { filer.dropped_count++; return; }

    e = &filer.entries[filer.entry_count];
    for (i = 0; entry->name[i]; i++) {
        e->name[i] = entry->name[i];
    }
    e->name[i] = '\0';
    e->size = entry->size;
    e->is_dir = (entry->type == OS32_FILE_TYPE_DIR);
    filer.entry_count++;
}

static void scan_directory(const char *dir)
{
    filer.entry_count = 0;
    filer.cursor_idx = 0;
    filer.scroll_top = 0;
    filer.dropped_count = 0;

    /* ".." エントリを手動追加 (ルートでなければ) */
    if (dir[0] != '\0' && !(dir[0] == '/' && dir[1] == '\0')) {
        FilerEntry *e = &filer.entries[0];
        e->name[0] = '.'; e->name[1] = '.'; e->name[2] = '\0';
        e->size = 0;
        e->is_dir = 1;
        filer.entry_count = 1;
    }

    f_api->sys_ls(dir, (DirCallback)dir_callback, NULL);
}

/* パスを結合。戻り値: 長さ / out に収まらなければ -1 (票 T23)。
 * 以前は戻り値が無く、溢れても切った綴りを返していた。 */
static int build_full_path(char *out, int out_sz,
                           const char *dir, const char *name)
{
    int i = 0, j = 0;
    while (dir[j]) {
        if (i >= out_sz - 2) return -1;
        out[i++] = dir[j++];
    }
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    j = 0;
    while (name[j]) {
        if (i >= out_sz - 1) return -1;
        out[i++] = name[j++];
    }
    out[i] = '\0';
    return i;
}

/* ======================================================================== */
/*  GFX描画                                                                 */
/* ======================================================================== */

/* 数値を右寄せ文字列に変換 */
static void format_size(char *buf, u32 size)
{
    if (size >= 1048576) {
        sprintf(buf, "%dMB", (int)(size / 1048576));
    } else if (size >= 1024) {
        sprintf(buf, "%dKB", (int)(size / 1024));
    } else {
        sprintf(buf, "%d", (int)size);
    }
}

static void draw_filer_panel(void)
{
    int i, idx;
    int ly;
    char buf[80];

    /* パネル背景 */
    gfx_fill_rect(FP_X, FP_Y, FP_W, FP_H, FC_PANEL_BG);
    gfx_rect(FP_X, FP_Y, FP_W, FP_H, FC_BORDER);
    gfx_rect(FP_X + 1, FP_Y + 1, FP_W - 2, FP_H - 2, FC_BORDER);

    /* ヘッダ: カレントディレクトリ */
    gfx_fill_rect(FP_X + 2, FP_Y + 2, FP_W - 4, FP_HEADER_H, FC_CURSOR_BG);
    sprintf(buf, " %s", filer.current_dir);
    kcg_draw_utf8(FP_X + FP_PAD, FP_Y + 4, buf, FC_TITLE, 0xFF);

    /* エントリ件数 (載せられなかった分は数だけ出す — 票 T23) */
    if (filer.dropped_count > 0)
        sprintf(buf, " %d items (+%d hidden)",
                filer.entry_count, filer.dropped_count);
    else
        sprintf(buf, " %d items", filer.entry_count);
    kcg_draw_utf8(FP_X + FP_W - 100, FP_Y + 4, buf, FC_HINT, 0xFF);

    /* エントリ一覧 */
    for (i = 0; i < FP_VISIBLE; i++) {
        idx = filer.scroll_top + i;
        if (idx >= filer.entry_count) break;

        ly = FP_Y + FP_HEADER_H + i * FP_LINE_H;

        /* カーソル行ハイライト */
        if (idx == filer.cursor_idx) {
            gfx_fill_rect(FP_X + 2, ly, FP_W - 4, FP_LINE_H, FC_CURSOR_BG);
        }

        {
            FilerEntry *e = &filer.entries[idx];
            u8 name_color;

            if (e->is_dir) {
                /* ディレクトリ: [DIR] + 名前 */
                kcg_draw_utf8(FP_X + FP_PAD, ly + 1,
                              "<DIR>", FC_DIR_NAME, 0xFF);
                name_color = FC_DIR_NAME;
            } else {
                /* ファイル: サイズ + 名前 */
                char sz[16];
                format_size(sz, e->size);
                kcg_draw_utf8(FP_X + FP_PAD, ly + 1,
                              sz, FC_SIZE, 0xFF);
                name_color = FC_FILE_NAME;
            }

            kcg_draw_utf8(FP_X + FP_PAD + 64, ly + 1,
                          e->name, name_color, 0xFF);
        }
    }

    /* スクロールバー表示 (簡易) */
    if (filer.entry_count > FP_VISIBLE) {
        int bar_h = FP_H - FP_HEADER_H - FP_FOOTER_H;
        int thumb_h = (FP_VISIBLE * bar_h) / filer.entry_count;
        int thumb_y;

        if (thumb_h < 4) thumb_h = 4;
        thumb_y = FP_Y + FP_HEADER_H +
                  (filer.scroll_top * bar_h) / filer.entry_count;

        gfx_fill_rect(FP_X + FP_W - 6, thumb_y, 4, thumb_h, FC_BORDER);
    }

    /* フッタ: キーヒント */
    gfx_fill_rect(FP_X + 2, FP_Y + FP_H - FP_FOOTER_H,
                  FP_W - 4, FP_FOOTER_H, FC_CURSOR_BG);
    kcg_draw_utf8(FP_X + FP_PAD,
                  FP_Y + FP_H - FP_FOOTER_H + 2,
                  "Up/Down:Select  Enter:Open  ESC:Cancel",
                  FC_HINT, 0xFF);
}

/* ======================================================================== */
/*  API実装                                                                  */
/* ======================================================================== */

void filer_init(KernelAPI *api)
{
    f_api = api;
    memset(&filer, 0, sizeof(filer));
}

int filer_open(const char *dir, const char *filter)
{
    /* 状態初期化 */
    filer.active = 1;
    filer.filter_ext = filter;
    strncpy(filer.current_dir, dir, FILER_MAX_PATH - 1);
    filer.current_dir[FILER_MAX_PATH - 1] = '\0';
    filer.selected_path[0] = '\0';

    /* ディレクトリ走査 */
    scan_directory(dir);

    /* モーダルイベントループ */
    for (;;) {
        int key, kd, kc;

        /* 描画 */
        draw_filer_panel();
        gfx_present();
        f_api->gfx_present_dirty();

        /* キー入力 */
        key = f_api->kbd_getkey();
        kd = FK_KEYDATA(key);
        kc = FK_KEYCODE(key);

        if (kd == 0x1B) {
            /* ESC: キャンセル */
            filer.active = 0;
            return 0;

        } else if (kc == FK_UP) {
            if (filer.cursor_idx > 0) {
                filer.cursor_idx--;
                if (filer.cursor_idx < filer.scroll_top) {
                    filer.scroll_top = filer.cursor_idx;
                }
            }

        } else if (kc == FK_DOWN) {
            if (filer.cursor_idx < filer.entry_count - 1) {
                filer.cursor_idx++;
                if (filer.cursor_idx >= filer.scroll_top + FP_VISIBLE) {
                    filer.scroll_top = filer.cursor_idx - FP_VISIBLE + 1;
                }
            }

        } else if (kc == FK_HOME) {
            filer.cursor_idx = 0;
            filer.scroll_top = 0;

        } else if (kd == 0x0D || kd == 0x0A) {
            /* Enter */
            if (filer.entry_count > 0) {
                FilerEntry *e = &filer.entries[filer.cursor_idx];

                if (e->is_dir) {
                    /* ディレクトリに移動 */
                    char new_dir[FILER_MAX_PATH];

                    if (e->name[0] == '.' && e->name[1] == '.' && e->name[2] == '\0') {
                        /* ".." → 親ディレクトリ */
                        int len, last_slash;
                        len = 0;
                        while (filer.current_dir[len]) len++;
                        /* 末尾の / を除去 */
                        if (len > 1 && filer.current_dir[len - 1] == '/') len--;
                        /* 最後の / を探す */
                        last_slash = 0;
                        {
                            int k;
                            for (k = 0; k < len; k++) {
                                if (filer.current_dir[k] == '/') last_slash = k;
                            }
                        }
                        if (last_slash == 0) {
                            new_dir[0] = '/'; new_dir[1] = '\0';
                        } else {
                            memcpy(new_dir, filer.current_dir, last_slash);
                            new_dir[last_slash] = '\0';
                        }
                    } else {
                        /* 収まらなければ移動しない (切れた別の場所へ行かない) */
                        if (build_full_path(new_dir, FILER_MAX_PATH,
                                            filer.current_dir, e->name) < 0)
                            continue;
                    }

                    strncpy(filer.current_dir, new_dir, FILER_MAX_PATH - 1);
                    filer.current_dir[FILER_MAX_PATH - 1] = '\0';
                    scan_directory(filer.current_dir);

                } else {
                    /* ファイル選択 → 確定。収まらなければ**確定しない**
                     * (切れた別のファイルのパスを返さない)。 */
                    if (build_full_path(filer.selected_path, FILER_MAX_PATH,
                                        filer.current_dir, e->name) < 0) {
                        filer.selected_path[0] = '\0';
                        continue;
                    }
                    filer.active = 0;
                    return 1;
                }
            }
        }
    }
}

const char *filer_get_selected_path(void)
{
    return filer.selected_path;
}
