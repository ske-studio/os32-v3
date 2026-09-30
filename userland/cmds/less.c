/* ======================================================================== */
/*  LESS.C — 双方向スクロール対応ページャー                                  */
/*                                                                          */
/*  Usage: less [FILE]                                                       */
/*         cmd | less                                                        */
/*                                                                          */
/*  キー操作:                                                                */
/*    Space/f: 次ページ    b: 前ページ                                      */
/*    ↓/Enter/j: 1行下     ↑/k: 1行上                                     */
/*    g/Home: 先頭          G/HELP: 末尾                                      */
/*    /: 前方検索           n: 次の検索結果   N: 前の検索結果                */
/*    q/ESC: 終了                                                            */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;

/* 入力バッファ */
static char buf[65536];

/* 行ポインタテーブル */
#define MAX_LINES 4096
static const char *lines[MAX_LINES];
static int line_count = 0;

/* 検索 */
static char search_pattern[64];
static int search_active = 0;

/* 表示状態 */
static int con_w, con_h, page_lines;
static int top_line = 0;            /* 表示中の先頭行 */
static const char *filename = NULL; /* 表示中のファイル名 */

/* PC-98 キーボード制御コード (scancode_to_ascii テーブルより) */
#define KEY_UP    0x1E  /* ↑ */
#define KEY_DOWN  0x1F  /* ↓ */
#define KEY_HOME  0x01  /* HOME */
#define KEY_HELP  0x05  /* HELP (ENDキーの代用) */

/* ======================================================================== */
/*  ユーティリティ                                                           */
/* ======================================================================== */

/* バッファを行に分割 */
static void split_lines(char *data, int len)
{
    int i;

    line_count = 0;
    if (len <= 0) return;

    lines[line_count++] = data;

    for (i = 0; i < len && line_count < MAX_LINES; i++) {
        if (data[i] == '\n') {
            data[i] = '\0';
            if (i + 1 < len) {
                lines[line_count++] = &data[i + 1];
            }
        } else if (data[i] == '\r') {
            data[i] = '\0';
        }
    }

    /* 末尾が空行の場合は除去 */
    if (line_count > 0 && lines[line_count - 1][0] == '\0') {
        line_count--;
    }
}

/* キー入力待ち (rshellタイムアウト回避: kbd_trygetchar ベース) */
static int wait_key(void)
{
    int ch;
    for (;;) {
        ch = api->kbd_trygetchar();
        if (ch >= 0) return ch;
        api->sys_halt();
    }
}

/* 簡易部分文字列検索 */
static int str_find(const char *haystack, const char *needle)
{
    int hlen, nlen, i, j;
    if (!needle[0]) return 0;
    hlen = strlen(haystack);
    nlen = strlen(needle);
    if (nlen > hlen) return 0;
    for (i = 0; i <= hlen - nlen; i++) {
        int ok = 1;
        for (j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

/* ======================================================================== */
/*  表示                                                                     */
/* ======================================================================== */

/* 1行を表示 (コンソール幅でクリップ) */
static void print_line(const char *line, int max_width)
{
    int len = strlen(line);
    if (len > max_width) len = max_width;
    if (len > 0) {
        api->sys_write(1, line, len);
    }
    api->sys_write(1, "\n", 1);
}

/* ステータスバーを表示 (最下行) */
static void show_status(void)
{
    int end_line;
    int pct;
    int at_end;

    end_line = top_line + page_lines;
    if (end_line > line_count) end_line = line_count;
    at_end = (end_line >= line_count);

    if (line_count > 0) {
        pct = (end_line * 100) / line_count;
        if (pct > 100) pct = 100;
    } else {
        pct = 100;
    }

    if (at_end) {
        if (search_active) {
            api->kprintf(ATTR_MAGENTA, "(END) [/%s]", search_pattern);
        } else {
            api->kprintf(ATTR_MAGENTA, "%s", "(END)");
        }
    } else if (filename) {
        if (search_active) {
            api->kprintf(ATTR_MAGENTA, "%s: %d-%d/%d (%d%%) [/%s]",
                         filename, top_line + 1, end_line, line_count,
                         pct, search_pattern);
        } else {
            api->kprintf(ATTR_MAGENTA, "%s: %d-%d/%d (%d%%)",
                         filename, top_line + 1, end_line, line_count, pct);
        }
    } else {
        if (search_active) {
            api->kprintf(ATTR_MAGENTA, "%d-%d/%d (%d%%) [/%s]",
                         top_line + 1, end_line, line_count,
                         pct, search_pattern);
        } else {
            api->kprintf(ATTR_MAGENTA, "%d-%d/%d (%d%%)",
                         top_line + 1, end_line, line_count, pct);
        }
    }
}

/* ステータスバーをクリア */
static void clear_status(void)
{
    api->sys_write(1, "\r", 1);
    api->sys_write(1,
        "                                                                              ",
        78);
    api->sys_write(1, "\r", 1);
}

/* 画面全体を再描画 (tvram_clear + page_lines 行 + ステータスバー) */
static void redraw(void)
{
    int i;
    api->tvram_clear();
    for (i = 0; i < page_lines && (top_line + i) < line_count; i++) {
        print_line(lines[top_line + i], con_w);
    }
    show_status();
}

/* ======================================================================== */
/*  検索                                                                     */
/* ======================================================================== */

/* 前方検索: from_line 以降でパターンにマッチする行を探す */
static int find_forward(int from_line, const char *pattern)
{
    int i;
    for (i = from_line; i < line_count; i++) {
        if (str_find(lines[i], pattern)) return i;
    }
    return -1;
}

/* 後方検索: from_line 以前でパターンにマッチする行を探す */
static int find_backward(int from_line, const char *pattern)
{
    int i;
    for (i = from_line; i >= 0; i--) {
        if (str_find(lines[i], pattern)) return i;
    }
    return -1;
}

/* 検索プロンプト */
static void read_search_pattern(void)
{
    int pos = 0;
    int ch;

    clear_status();
    api->kprintf(ATTR_MAGENTA, "%s", "/");

    while (pos < 62) {
        ch = wait_key();
        if (ch == '\r' || ch == '\n') break;
        if (ch == 0x1B) {
            search_pattern[0] = '\0';
            redraw();
            return;
        }
        if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                api->sys_write(1, "\b \b", 3);
            }
            continue;
        }
        if (ch >= 0x20 && ch < 0x7F) {
            char c = (char)ch;
            search_pattern[pos++] = c;
            api->sys_write(1, &c, 1);
        }
    }
    search_pattern[pos] = '\0';
    search_active = (pos > 0);
}

/* ======================================================================== */
/*  スクロール制御                                                           */
/* ======================================================================== */

/* top_line の安全な範囲クランプ */
static void clamp_top(void)
{
    int max_top;
    if (top_line < 0) top_line = 0;
    max_top = line_count - page_lines;
    if (max_top < 0) max_top = 0;
    if (top_line > max_top) top_line = max_top;
}

/* 1ページ下 */
static void page_down(void)
{
    top_line += page_lines;
    clamp_top();
    redraw();
}

/* 1ページ上 */
static void page_up(void)
{
    top_line -= page_lines;
    clamp_top();
    redraw();
}

/* 1行下 */
static void line_down(void)
{
    top_line++;
    clamp_top();
    redraw();
}

/* 1行上 */
static void line_up(void)
{
    top_line--;
    clamp_top();
    redraw();
}

/* 先頭へ */
static void goto_top(void)
{
    top_line = 0;
    redraw();
}

/* 末尾へ */
static void goto_bottom(void)
{
    int max_top = line_count - page_lines;
    if (max_top < 0) max_top = 0;
    top_line = max_top;
    redraw();
}

/* 検索結果の行が画面内に来るよう top_line を調整 */
static void scroll_to_line(int target)
{
    if (target < top_line || target >= top_line + page_lines) {
        top_line = target;
        clamp_top();
    }
    redraw();
}

/* ======================================================================== */
/*  メイン                                                                   */
/* ======================================================================== */

int main(int argc, char **argv, KernelAPI *kapi)
{
    int sz = 0;
    int ch;

    api = kapi;

    /* コンソールサイズ取得 */
    api->console_get_size(&con_w, &con_h);
    if (con_h < 5) con_h = 25;
    if (con_w < 20) con_w = 80;
    page_lines = con_h - 2; /* ステータス行 + 余白 */

    if (argc >= 2) {
        /* ファイルから読み込み */
        int fd = api->sys_open(argv[1], KAPI_O_RDONLY);
        if (fd < 0) {
            printf("less: %s: No such file\n", argv[1]);
            return 1;
        }
        filename = argv[1];
        /* ループで読み切り */
        while (sz < (int)(sizeof(buf) - 1)) {
            int r = api->sys_read(fd, buf + sz, sizeof(buf) - 1 - sz);
            if (r <= 0) break;
            sz += r;
        }
        api->sys_close(fd);
    } else {
        /* stdin から読み込み (パイプ入力) */
        if (api->sys_isatty(0)) {
            printf("Usage: less [FILE]\n");
            printf("       cmd | less\n");
            return 1;
        }
        filename = NULL;
        while (sz < (int)(sizeof(buf) - 1)) {
            int r = api->sys_read(0, buf + sz, sizeof(buf) - 1 - sz);
            if (r <= 0) break;
            sz += r;
        }
    }

    if (sz <= 0) return 0;
    buf[sz] = '\0';

    /* 行に分割 */
    split_lines(buf, sz);

    if (line_count == 0) return 0;

    /* stdout がパイプの場合はフィルタ動作 (全データ出力) */
    if (!api->sys_isatty(1)) {
        int i;
        for (i = 0; i < line_count; i++) {
            printf("%s\n", lines[i]);
        }
        return 0;
    }

    /* 初期化 */
    search_pattern[0] = '\0';
    search_active = 0;
    top_line = 0;

    /* 初期描画 */
    redraw();

    /* メインループ */
    for (;;) {
        ch = wait_key();

        if (ch == 'q' || ch == 'Q' || ch == 0x1B) {
            /* q / ESC: 終了 */
            clear_status();
            break;
        }

        if (ch == ' ' || ch == 'f') {
            /* Space / f: 次ページ */
            page_down();
        } else if (ch == 'b') {
            /* b: 前ページ */
            page_up();
        } else if (ch == KEY_DOWN || ch == '\r' || ch == '\n' || ch == 'j') {
            /* ↓ / Enter / j: 1行下 */
            line_down();
        } else if (ch == KEY_UP || ch == 'k') {
            /* ↑ / k: 1行上 */
            line_up();
        } else if (ch == 'g' || ch == KEY_HOME) {
            /* g / HOME: 先頭 */
            goto_top();
        } else if (ch == 'G' || ch == KEY_HELP) {
            /* G / HELP: 末尾 */
            goto_bottom();
        } else if (ch == '/') {
            /* /: 前方検索 */
            read_search_pattern();
            if (search_active) {
                int found = find_forward(top_line + 1, search_pattern);
                if (found >= 0) {
                    scroll_to_line(found);
                } else {
                    api->kprintf(ATTR_RED, "%s", " Pattern not found");
                    wait_key();
                    redraw();
                }
            } else {
                redraw();
            }
        } else if (ch == 'n') {
            /* n: 次の検索結果 (前方) */
            if (search_active) {
                int found = find_forward(top_line + 1, search_pattern);
                if (found >= 0) {
                    scroll_to_line(found);
                } else {
                    clear_status();
                    api->kprintf(ATTR_RED, "%s", "Pattern not found (bottom)");
                    wait_key();
                    redraw();
                }
            }
        } else if (ch == 'N') {
            /* N: 前の検索結果 (後方) */
            if (search_active) {
                int start = top_line - 1;
                int found;
                if (start < 0) start = 0;
                found = find_backward(start, search_pattern);
                if (found >= 0) {
                    scroll_to_line(found);
                } else {
                    clear_status();
                    api->kprintf(ATTR_RED, "%s", "Pattern not found (top)");
                    wait_key();
                    redraw();
                }
            }
        }
        /* その他のキーは無視 */
    }

    return 0;
}
