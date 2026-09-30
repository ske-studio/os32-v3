/* ======================================================================== */
/*  SYSCONFIG.C — 最小の起動設定パーサ (K レーン K4)                         */
/*                                                                          */
/*  行指向 KEY=VALUE 設定 (/etc/system.cfg) を VFS から読み、整数値を返す。   */
/*  カーネルのシェル起動ループ (kernel.c) が GUI=0/1 を分岐するのに使う。     */
/*  票 H2b で文字列値の取り出し (GFX=pc98|pegc|auto) を追加した。            */
/*  設定ファイルは小さい前提 (数キー) なので固定バッファに一括で読む。        */
/*  仕様: docs/archive/gui_v11/TASK_K4_gui_boot.md、契約 T9。                 */
/* ======================================================================== */

#include "types.h"
#include "vfs.h"
#include "kstring.h"
#include "sysconfig.h"

/* 設定ファイルの読み込み上限 (十分に小さい前提)。 */
#define SYSCONFIG_MAX  1024

/* 起動ループ専用・単一スレッド。get_int / get_str が交互に使う。 */
static char sc_buf[SYSCONFIG_MAX];

/* 符号付き十進を最低限だけ解釈する (先頭の +/- と連続する数字)。 */
static int sc_atoi(const char *s)
{
    int v = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (int)(*s - '0');
        s++;
    }
    return neg ? -v : v;
}

/* path を sc_buf へ読む。読めたバイト数 (NUL 終端済み)、失敗で 0。 */
static int sc_load(const char *path)
{
    int n = vfs_read(path, sc_buf, (u32)(SYSCONFIG_MAX - 1));
    if (n <= 0) {
        sc_buf[0] = '\0';
        return 0;
    }
    sc_buf[n] = '\0';
    return n;
}

/* sc_buf[0, n) から key を探し、'=' の右側 (先行空白を落とした位置) の
 * オフセットを返す。見つからなければ -1。 */
static int sc_find_value(int n, const char *key)
{
    int klen = (int)kstrlen(key);
    int i = 0;

    while (i < n) {
        int j;

        /* 行頭の空白を飛ばす */
        while (i < n && (sc_buf[i] == ' ' || sc_buf[i] == '\t')) {
            i++;
        }

        /* コメント行・空行はスキップ */
        if (i >= n || sc_buf[i] == '#' || sc_buf[i] == ';' ||
            sc_buf[i] == '\n' || sc_buf[i] == '\r') {
            while (i < n && sc_buf[i] != '\n') {
                i++;
            }
            if (i < n) {
                i++;
            }
            continue;
        }

        /* KEY を先頭一致で照合 */
        if (kstrncmp(&sc_buf[i], key, (u32)klen) == 0) {
            j = i + klen;
            /* KEY の直後は空白 or '=' のみ許可 (前方一致の誤検出を防ぐ) */
            while (j < n && (sc_buf[j] == ' ' || sc_buf[j] == '\t')) {
                j++;
            }
            if (j < n && sc_buf[j] == '=') {
                j++;
                while (j < n && (sc_buf[j] == ' ' || sc_buf[j] == '\t')) {
                    j++;
                }
                return j;
            }
        }

        /* 次の行へ */
        while (i < n && sc_buf[i] != '\n') {
            i++;
        }
        if (i < n) {
            i++;
        }
    }

    return -1;
}

int sysconfig_get_int(const char *path, const char *key, int def)
{
    int n = sc_load(path);
    int v;

    if (n <= 0) {
        return def;   /* ファイル無し / 空 / 読み取り失敗 */
    }
    v = sc_find_value(n, key);
    if (v < 0) {
        return def;
    }
    return sc_atoi(&sc_buf[v]);
}

int sysconfig_get_str(const char *path, const char *key, char *out, int outsz)
{
    int n, v, len;

    if (!out || outsz <= 0) return -1;
    out[0] = '\0';

    n = sc_load(path);
    if (n <= 0) return -1;

    v = sc_find_value(n, key);
    if (v < 0) return -1;

    /* 行末 / 空白 / コメント開始までを値とする */
    len = 0;
    while (v + len < n) {
        char c = sc_buf[v + len];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t' ||
            c == '#' || c == ';') {
            break;
        }
        len++;
    }
    if (len > outsz - 1) len = outsz - 1;
    kstrncpy(out, &sc_buf[v], (u32)(len + 1));
    out[len] = '\0';
    return len;
}
