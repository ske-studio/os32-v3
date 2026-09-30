/* ======================================================================== */
/*  MORE.C — ページャー (画面単位のテキスト表示)                              */
/*                                                                          */
/*  Usage: more [FILE]                                                       */
/*         cmd | more                                                        */
/*                                                                          */
/*  キー操作:                                                                */
/*    スペース: 次ページ    Enter: 次の1行    b: 前ページ                    */
/*    /: 検索 (前方)        n: 次の検索結果   q/ESC: 終了                    */
/* ======================================================================== */
#include "os32api.h"
#include <string.h>
#include <stdio.h>

static KernelAPI *api;

/* 入力バッファ */
static char buf[65536];

/* 行ポインタテーブル (各行の先頭位置) */
#define MAX_LINES 4096
static const char *lines[MAX_LINES];
static int line_count = 0;

/* 検索パターン */
static char search_pattern[64];
static int search_active = 0;

/* コンソールサイズ */
static int con_w, con_h, page_lines;

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

/* ステータス行を表示 */
static void show_prompt(int current_line, int total_lines)
{
    int pct;
    if (total_lines > 0) {
        pct = (current_line * 100) / total_lines;
        if (pct > 100) pct = 100;
    } else {
        pct = 100;
    }
    if (search_active) {
        api->kprintf(ATTR_MAGENTA, "--More-- (%d%%) [/%s]", pct, search_pattern);
    } else {
        api->kprintf(ATTR_MAGENTA, "--More-- (%d%%)", pct);
    }
}

/* ステータス行をクリア */
static void clear_prompt(void)
{
    api->sys_write(1, "\r", 1);
    api->sys_write(1, "                                                ", 48);
    api->sys_write(1, "\r", 1);
}

/* キー入力待ち (rshellタイムアウト回避: kbd_trygetchar ベース) */
static int wait_key(void)
{
    int ch;
    for (;;) {
        ch = api->kbd_trygetchar();
        if (ch >= 0) return ch;
        /* CPU をアイドルにして次の割り込みを待つ */
        api->sys_halt();
    }
}

/* 画面をクリアして指定位置から1ページ表示 */
static void display_page(int start_line)
{
    int i;
    api->tvram_clear();
    for (i = 0; i < page_lines && (start_line + i) < line_count; i++) {
        print_line(lines[start_line + i], con_w);
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

/* 検索: 指定行以降でパターンにマッチする行を探す */
static int find_next(int from_line, const char *pattern)
{
    int i;
    for (i = from_line; i < line_count; i++) {
        if (str_find(lines[i], pattern)) return i;
    }
    return -1; /* 見つからない */
}

/* 検索プロンプトを表示してパターンを入力 */
static void read_search_pattern(void)
{
    int pos = 0;
    int ch;

    api->kprintf(ATTR_MAGENTA, "%s", "/");

    while (pos < 62) {
        ch = wait_key();
        if (ch == '\r' || ch == '\n') break;
        if (ch == 0x1B) {
            /* ESCでキャンセル */
            search_pattern[0] = '\0';
            clear_prompt();
            return;
        }
        if (ch == 0x08 || ch == 0x7F) {
            /* バックスペース */
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
    clear_prompt();
}

int main(int argc, char **argv, KernelAPI *kapi)
{
    int sz = 0;
    int current_line;
    int ch;

    api = kapi;

    /* コンソールサイズ取得 */
    api->console_get_size(&con_w, &con_h);
    if (con_h < 5) con_h = 25;
    if (con_w < 20) con_w = 80;
    page_lines = con_h - 2; /* ステータス行 + 余白分 */

    if (argc >= 2) {
        /* ファイルから読み込み */
        int fd = api->sys_open(argv[1], KAPI_O_RDONLY);
        if (fd < 0) {
            printf("more: %s: No such file\n", argv[1]);
            return 1;
        }
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
            printf("Usage: more [FILE]\n");
            printf("       cmd | more\n");
            return 1;
        }
        /* ループで読み切り */
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

    /* stdoutが端末でない場合はそのまま全部出力 (more | grep 等) */
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

    /* 最初のページを表示 */
    current_line = 0;
    display_page(current_line);
    current_line = page_lines;
    if (current_line > line_count) current_line = line_count;

    /* メインループ */
    while (current_line < line_count) {
        /* --More-- プロンプト表示 */
        show_prompt(current_line, line_count);

        /* キー入力待ち */
        ch = wait_key();
        clear_prompt();

        if (ch == 'q' || ch == 'Q' || ch == 0x1B) {
            /* q または ESC で終了 */
            break;
        } else if (ch == ' ') {
            /* スペース: 次ページ */
            display_page(current_line);
            current_line += page_lines;
            if (current_line > line_count) current_line = line_count;
        } else if (ch == '\r' || ch == '\n') {
            /* Enter: 1行進む */
            print_line(lines[current_line], con_w);
            current_line++;
        } else if (ch == 'b' || ch == 'B') {
            /* b: 前ページ */
            current_line -= page_lines * 2;
            if (current_line < 0) current_line = 0;
            display_page(current_line);
            current_line += page_lines;
            if (current_line > line_count) current_line = line_count;
        } else if (ch == '/') {
            /* /: 検索パターン入力 */
            read_search_pattern();
            if (search_active) {
                int found = find_next(current_line, search_pattern);
                if (found >= 0) {
                    current_line = found;
                    display_page(current_line);
                    current_line += page_lines;
                    if (current_line > line_count) current_line = line_count;
                } else {
                    api->kprintf(ATTR_RED, "%s", "Pattern not found");
                    ch = wait_key();
                    clear_prompt();
                }
            }
        } else if (ch == 'n' || ch == 'N') {
            /* n: 次の検索結果 */
            if (search_active) {
                int found = find_next(current_line, search_pattern);
                if (found >= 0) {
                    current_line = found;
                    display_page(current_line);
                    current_line += page_lines;
                    if (current_line > line_count) current_line = line_count;
                } else {
                    api->kprintf(ATTR_RED, "%s", "Pattern not found");
                    ch = wait_key();
                    clear_prompt();
                }
            }
        }
        /* その他のキーは無視 */
    }

    /* ファイル末尾: (END) を表示して q を待つ */
    api->kprintf(ATTR_MAGENTA, "%s", "(END)");
    while (1) {
        ch = wait_key();
        if (ch == 'q' || ch == 'Q' || ch == 0x1B) break;
    }
    clear_prompt();

    return 0;
}
