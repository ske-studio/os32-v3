/* ======================================================================== */
/*  HELP.C - libos32 ヘルプシステム実装                                      */
/*                                                                          */
/*  /usr/man/<cmd>.1 からMarkdown形式のマニュアルを読み込み、                */
/*  色付きで stdout に表示する。Markdownサブセットのみ対応。                 */
/*                                                                          */
/*  対応するMarkdown要素:                                                   */
/*    # 見出し          → ATTR_CYAN                                         */
/*    ## 小見出し        → ATTR_YELLOW                                      */
/*    ``` コードブロック → ATTR_GREEN (フェンス内全行)                       */
/*    `インラインコード` → ATTR_GREEN                                       */
/*    - リスト          → ATTR_WHITE (先頭マーカー付き)                     */
/*    通常テキスト       → ATTR_WHITE                                       */
/* ======================================================================== */
#include "os32/help.h"
#include <string.h>

/* crt0_c.c で定義されるグローバルKAPIポインタ */
extern KernelAPI *kapi;

#define HELP_BUF_SIZE 32768

/* ------------------------------------------------------------------------ */
/*  ユーティリティ: コマンド名抽出                                          */
/*                                                                          */
/*  argv[0] から純粋なコマンド名を取り出す                                  */
/*  例: "/bin/grep.bin" → "grep"                                            */
/*      "grep.bin" → "grep"                                                 */
/*      "grep" → "grep"                                                     */
/* ------------------------------------------------------------------------ */
static void extract_cmd_name(const char *raw, char *out, int out_size)
{
    const char *p;
    const char *base;
    int len, i;

    /* 最後の '/' を探してベース名を取得 */
    base = raw;
    p = raw;
    while (*p) {
        if (*p == '/') base = p + 1;
        p++;
    }

    /* ベース名をコピー */
    len = 0;
    p = base;
    while (*p && len < out_size - 1) {
        out[len++] = *p++;
    }
    out[len] = '\0';

    /* ".bin" 拡張子を除去 */
    if (len >= 4) {
        if (out[len-4] == '.' && out[len-3] == 'b' &&
            out[len-2] == 'i' && out[len-1] == 'n') {
            out[len-4] = '\0';
        }
    }

    /* 大文字を小文字に変換 (念のため) */
    for (i = 0; out[i]; i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] = out[i] + ('a' - 'A');
        }
    }
}

/* ------------------------------------------------------------------------ */
/*  マニュアルファイルパスの構築                                             */
/*  /usr/man/<cmd>.1                                                        */
/* ------------------------------------------------------------------------ */
static void build_man_path(const char *cmd_name, char *path, int path_size)
{
    int i = 0;
    const char *s;

    /* OS32_MAN_DIR をコピー */
    s = OS32_MAN_DIR;
    while (*s && i < path_size - 10) path[i++] = *s++;

    /* '/' を追加 */
    path[i++] = '/';

    /* コマンド名をコピー */
    s = cmd_name;
    while (*s && i < path_size - 5) path[i++] = *s++;

    /* OS32_MAN_EXT を追加 */
    s = OS32_MAN_EXT;
    while (*s && i < path_size - 1) path[i++] = *s++;

    path[i] = '\0';
}

/* ------------------------------------------------------------------------ */
/*  行が "```" で始まるかチェック (コードフェンス)                           */
/* ------------------------------------------------------------------------ */
static int is_code_fence(const char *line)
{
    return (line[0] == '`' && line[1] == '`' && line[2] == '`');
}

/* ------------------------------------------------------------------------ */
/*  1行をMarkdownサブセット解釈して色付き出力                               */
/*                                                                          */
/*  is_tty: 端末出力か (0ならプレーンテキスト)                              */
/*  in_code: コードフェンス内フラグへのポインタ                             */
/* ------------------------------------------------------------------------ */
static void render_line(const char *line, int is_tty, int *in_code)
{
    /* コードフェンス切り替え */
    if (is_code_fence(line)) {
        *in_code = !(*in_code);
        return; /* フェンス行自体は表示しない */
    }

    /* コードブロック内 → 全行グリーン */
    if (*in_code) {
        if (is_tty) {
            kapi->kprintf(ATTR_GREEN, "    %s\n", line);
        } else {
            kapi->sys_write(1, "    ", 4);
            kapi->sys_write(1, line, strlen(line));
            kapi->sys_write(1, "\n", 1);
        }
        return;
    }

    /* 空行 */
    if (line[0] == '\0') {
        kapi->sys_write(1, "\n", 1);
        return;
    }

    /* # 見出し (h1) */
    if (line[0] == '#' && line[1] == ' ') {
        if (is_tty) {
            kapi->kprintf(ATTR_CYAN, "%s\n", line + 2);
        } else {
            kapi->sys_write(1, line + 2, strlen(line + 2));
            kapi->sys_write(1, "\n", 1);
        }
        return;
    }

    /* ## 見出し (h2) */
    if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
        if (is_tty) {
            kapi->kprintf(ATTR_YELLOW, "\n%s\n", line + 3);
        } else {
            kapi->sys_write(1, "\n", 1);
            kapi->sys_write(1, line + 3, strlen(line + 3));
            kapi->sys_write(1, "\n", 1);
        }
        return;
    }

    /* ### 見出し (h3) */
    if (line[0] == '#' && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
        if (is_tty) {
            kapi->kprintf(ATTR_YELLOW, "  %s\n", line + 4);
        } else {
            kapi->sys_write(1, "  ", 2);
            kapi->sys_write(1, line + 4, strlen(line + 4));
            kapi->sys_write(1, "\n", 1);
        }
        return;
    }

    /* - リスト項目 */
    if (line[0] == '-' && line[1] == ' ') {
        if (is_tty) {
            /* インラインコードを含む可能性があるのでrender_inline使用 */
            kapi->kprintf(ATTR_WHITE, "%s", "  * ");
            /* line+2 以降をインライン解析 */
            {
                const char *p = line + 2;
                int in_tick = 0;
                char seg[512];
                int si = 0;

                while (*p) {
                    if (*p == '`') {
                        /* 溜まったセグメントを出力 */
                        if (si > 0) {
                            seg[si] = '\0';
                            kapi->kprintf(in_tick ? ATTR_GREEN : ATTR_WHITE,
                                          "%s", seg);
                            si = 0;
                        }
                        in_tick = !in_tick;
                        p++;
                        continue;
                    }
                    if (si < 510) seg[si++] = *p;
                    p++;
                }
                if (si > 0) {
                    seg[si] = '\0';
                    kapi->kprintf(in_tick ? ATTR_GREEN : ATTR_WHITE,
                                  "%s", seg);
                }
            }
            kapi->sys_write(1, "\n", 1);
        } else {
            kapi->sys_write(1, "  * ", 4);
            kapi->sys_write(1, line + 2, strlen(line + 2));
            kapi->sys_write(1, "\n", 1);
        }
        return;
    }

    /* 通常行 — インラインコード対応 */
    if (is_tty) {
        const char *p = line;
        int in_tick = 0;
        char seg[512];
        int si = 0;

        kapi->kprintf(ATTR_WHITE, "%s", "  ");
        while (*p) {
            if (*p == '`') {
                if (si > 0) {
                    seg[si] = '\0';
                    kapi->kprintf(in_tick ? ATTR_GREEN : ATTR_WHITE,
                                  "%s", seg);
                    si = 0;
                }
                in_tick = !in_tick;
                p++;
                continue;
            }
            if (si < 510) seg[si++] = *p;
            p++;
        }
        if (si > 0) {
            seg[si] = '\0';
            kapi->kprintf(in_tick ? ATTR_GREEN : ATTR_WHITE, "%s", seg);
        }
        kapi->sys_write(1, "\n", 1);
    } else {
        kapi->sys_write(1, "  ", 2);
        kapi->sys_write(1, line, strlen(line));
        kapi->sys_write(1, "\n", 1);
    }
}

/* ======================================================================== */
/*  公開API: os32_help_show                                                  */
/* ======================================================================== */
int os32_help_show(const char *name)
{
    char cmd_name[64];
    char man_path[OS32_MAX_PATH];
    char *help_buf;
    int fd, sz;
    int is_tty;
    int in_code;
    char *p, *line_start;

    if (!kapi || !name) return -1;

    extract_cmd_name(name, cmd_name, sizeof(cmd_name));
    build_man_path(cmd_name, man_path, sizeof(man_path));

    /* ファイルを開く */
    fd = kapi->sys_open(man_path, KAPI_O_RDONLY);
    if (fd < 0) return -1;

    /* バッファを動的確保 */
    help_buf = (char *)kapi->mem_alloc(HELP_BUF_SIZE);
    if (!help_buf) { kapi->sys_close(fd); return -1; }

    /* 読み込み */
    sz = kapi->sys_read(fd, help_buf, HELP_BUF_SIZE - 1);
    kapi->sys_close(fd);

    if (sz <= 0) { kapi->mem_free(help_buf); return -1; }
    help_buf[sz] = '\0';

    /* 端末チェック */
    is_tty = kapi->sys_isatty(1);

    /* 先頭に空行を挿入 */
    kapi->sys_write(1, "\n", 1);

    /* 行ごとにパースして出力 */
    in_code = 0;
    p = help_buf;
    line_start = p;

    while (*p) {
        if (*p == '\n') {
            *p = '\0';
            render_line(line_start, is_tty, &in_code);
            line_start = p + 1;
        } else if (*p == '\r') {
            /* CR を無視 (CR+LF対応) */
            *p = '\0';
        }
        p++;
    }
    /* 最終行 (改行なしで終端の場合) */
    if (line_start < p && *line_start) {
        render_line(line_start, is_tty, &in_code);
    }

    /* 末尾に空行 */
    kapi->sys_write(1, "\n", 1);

    kapi->mem_free(help_buf);
    return 0;
}

/* ======================================================================== */
/*  公開API: os32_help_exists                                                */
/* ======================================================================== */
int os32_help_exists(const char *name)
{
    char cmd_name[64];
    char man_path[OS32_MAX_PATH];
    int fd;

    if (!kapi || !name) return 0;

    extract_cmd_name(name, cmd_name, sizeof(cmd_name));
    build_man_path(cmd_name, man_path, sizeof(man_path));

    fd = kapi->sys_open(man_path, KAPI_O_RDONLY);
    if (fd < 0) return 0;
    kapi->sys_close(fd);
    return 1;
}
