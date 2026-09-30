/* ======================================================================== */
/*  CFG.C — 設定レジストリの CUI (/usr/bin/cfg.bin)、票 S2 §2               */
/*                                                                          */
/*    cfg get <scope> <key> [default]                                       */
/*    cfg set <scope> <key> int|text|blob <value>                           */
/*    cfg del <scope> <key>                                                 */
/*    cfg list [<scope> [<prefix>]]                                         */
/*    cfg status                                                            */
/*    cfg init [--tsv <path>]                                               */
/*    cfg export <file>                                                     */
/*                                                                          */
/*  GUI 配下 (端末) でも動く — KAPI しか使わない。コンソールへの出力は DB を  */
/*  **閉じた後**に 1KB ごと sys_yield を挟んで書く: open 〜 close の間に      */
/*  yield しないという直列化の契約 (票 §7) を崩さないため。                  */
/*  引数解釈と整形は純関数に切り出してあり、tools/tests/cfg_host.c が同じ     */
/*  ハーネスで直接呼ぶ。                                                     */
/* ======================================================================== */

#include "os32api.h"
#include "cfg/libos32cfg.h"

/* ---- 副指令 ---- */
#define CFG_CMD_NONE    0
#define CFG_CMD_GET     1
#define CFG_CMD_SET     2
#define CFG_CMD_DEL     3
#define CFG_CMD_LIST    4
#define CFG_CMD_STATUS  5
#define CFG_CMD_INIT    6
#define CFG_CMD_EXPORT  7
#define CFG_CMD_IMPORT  8

/* 出力の作業領域 ([C4]) */
/* 4096B blob の hex は 8192 文字。その 1 行と案内文がまとめて収まる大きさ。 */
#define CFG_OUT_MAX     9216      /* 溜める上限 (コンソール / export 共用) */
#define CFG_OUT_CHUNK   1024      /* 1 回の sys_write */
#define CFG_LINE_MAX    6400      /* export の 1 行 (base64 4096B = 5464 文字) */
#define CFG_ROW_MAX     512       /* list の 1 行 */

typedef struct {
    int cmd;
    const char *scope;
    const char *key;
    const char *type;
    const char *value;
    const char *def;      /* get の既定値 (NULL = 省略) */
    const char *prefix;
    const char *path;     /* init --tsv / export <file> / import <file> */
    int merge;            /* import --merge (0 = 置換) */
} CfgArgs;

static KernelAPI *api;
/* 出力の取りこぼし (溜め場の溢れ / short write)。終了コードへ持ち上げる。 */
static int out_err;

/* --- 純関数 (ホスト TDD が直接呼ぶ) --- */
static int cfg_cmd_parse(int argc, char **argv, CfgArgs *out);
static int fmt_int(char *buf, int cap, int v);
static int fmt_u32(char *buf, int cap, u32 v);
static int fmt_hex(char *buf, int cap, const unsigned char *p, int n);
static int parse_hex(const char *s, unsigned char *out, int cap);
static int fmt_b64(char *buf, int cap, const unsigned char *p, int n);
static int fmt_json_str(char *buf, int cap, const char *s, int n);
static const char *status_name(int st);
static const char *type_name(int t);
static int parse_int(const char *s, int *out);

/* --- 出力 --- */
static void out_reset(void);
static int  out_put(const char *p, int n);
static int  out_str(const char *s);
static int  out_unum(u32 v);
static int  out_flush_console(void);
static int  norm_path(const char *in, char *out, int cap);

/* --- 副指令の実装 --- */
static int do_status(void);
static int do_get(const CfgArgs *a);
static int do_set(const CfgArgs *a, int del);
static int do_list(const CfgArgs *a);
static int do_init(const CfgArgs *a);
static int do_export(const CfgArgs *a);
static int do_import(const CfgArgs *a);
static void usage(void);

int main(int argc, char **argv, KernelAPI *kapi_in)
{
    CfgArgs a;
    int rc;

    api = kapi_in;
    out_reset();
    if (cfg_cmd_parse(argc, argv, &a) != 0) {
        usage();
        out_flush_console();
        return 1;
    }
    switch (a.cmd) {
    case CFG_CMD_STATUS: rc = do_status();      break;
    case CFG_CMD_GET:    rc = do_get(&a);       break;
    case CFG_CMD_SET:    rc = do_set(&a, 0);    break;
    case CFG_CMD_DEL:    rc = do_set(&a, 1);    break;
    case CFG_CMD_LIST:   rc = do_list(&a);      break;
    case CFG_CMD_INIT:   rc = do_init(&a);      break;
    case CFG_CMD_EXPORT: rc = do_export(&a);    break;
    case CFG_CMD_IMPORT: rc = do_import(&a);    break;
    default:             usage(); rc = 1;       break;
    }
    /* 出力の取りこぼし (溢れ / short write) は黙って捨てず終了コードへ。 */
    if (out_flush_console() != 0) rc = 1;
    if (out_err) rc = 1;
    return rc;
}

/* ======================================================================== */
/*  小物                                                                     */
/* ======================================================================== */

static int s_len(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

static int s_eq(const char *a, const char *b)
{
    int i = 0;
    if (!a || !b) return 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == '\0' && b[i] == '\0';
}

static int s_cmp(const char *a, const char *b)
{
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static void s_cpy(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* 1..max バイトの C 文字列か。切り詰めてから使わないための門番。 */
static int s_len_ok(const char *s, int max)
{
    int n;
    if (!s) return 0;
    for (n = 0; s[n]; n++) {
        if (n >= max) return 0;
    }
    return n > 0;
}

/* ======================================================================== */
/*  引数解釈 (純関数)                                                        */
/* ======================================================================== */

static int cfg_cmd_parse(int argc, char **argv, CfgArgs *out)
{
    const char *c;

    out->cmd = CFG_CMD_NONE;
    out->scope = (const char *)0;
    out->key = (const char *)0;
    out->type = (const char *)0;
    out->value = (const char *)0;
    out->def = (const char *)0;
    out->prefix = (const char *)0;
    out->path = (const char *)0;
    out->merge = 0;
    if (argc < 2 || !argv) return -1;
    c = argv[1];

    if (s_eq(c, "status")) {
        if (argc != 2) return -1;
        out->cmd = CFG_CMD_STATUS;
        return 0;
    }
    if (s_eq(c, "get")) {
        if (argc < 4 || argc > 5) return -1;
        out->cmd = CFG_CMD_GET;
        out->scope = argv[2];
        out->key = argv[3];
        if (argc == 5) out->def = argv[4];
        return 0;
    }
    if (s_eq(c, "set")) {
        if (argc != 6) return -1;
        if (!s_eq(argv[4], "int") && !s_eq(argv[4], "text") &&
            !s_eq(argv[4], "blob")) return -1;
        out->cmd = CFG_CMD_SET;
        out->scope = argv[2];
        out->key = argv[3];
        out->type = argv[4];
        out->value = argv[5];
        return 0;
    }
    if (s_eq(c, "del")) {
        if (argc != 4) return -1;
        out->cmd = CFG_CMD_DEL;
        out->scope = argv[2];
        out->key = argv[3];
        return 0;
    }
    if (s_eq(c, "list")) {
        if (argc > 4) return -1;
        out->cmd = CFG_CMD_LIST;
        if (argc >= 3) out->scope = argv[2];
        if (argc >= 4) out->prefix = argv[3];
        return 0;
    }
    if (s_eq(c, "init")) {
        out->cmd = CFG_CMD_INIT;
        if (argc == 2) return 0;
        if (argc == 4 && s_eq(argv[2], "--tsv")) { out->path = argv[3]; return 0; }
        return -1;
    }
    if (s_eq(c, "export")) {
        if (argc != 3) return -1;
        out->cmd = CFG_CMD_EXPORT;
        out->path = argv[2];
        return 0;
    }
    if (s_eq(c, "import")) {
        int i;
        if (argc < 3 || !argv[2] || argv[2][0] == '-') return -1;
        out->cmd = CFG_CMD_IMPORT;
        out->path = argv[2];
        for (i = 3; i < argc; i++) {
            if (s_eq(argv[i], "--merge")) {
                if (out->merge) return -1;
                out->merge = 1;
            } else if (s_eq(argv[i], "--scope")) {
                if (out->scope || i + 1 >= argc) return -1;
                i++;
                out->scope = argv[i];
            } else {
                return -1;
            }
        }
        return 0;
    }
    return -1;
}

/* ======================================================================== */
/*  整形 (純関数)                                                            */
/* ======================================================================== */

static int fmt_int(char *buf, int cap, int v)
{
    char tmp[12];
    int n = 0, i = 0, neg = 0;
    u32 u;

    if (cap < 2) return -1;
    if (v < 0) { neg = 1; u = (u32)(-(v + 1)) + 1UL; } else { u = (u32)v; }
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = (char)('0' + (int)(u % 10UL)); u /= 10UL; }
    if (n + neg >= cap) return -1;
    if (neg) buf[i++] = '-';
    while (n) buf[i++] = tmp[--n];
    buf[i] = '\0';
    return i;
}

/* 符号なし 10 進 (KAPI の u32 をそのまま出す。fmt_int に通すと 2GB 超が
 * 負に化ける)。戻り: 長さ / -1 = 溜め場が足りない。 */
static int fmt_u32(char *buf, int cap, u32 v)
{
    char tmp[12];
    int n = 0, i = 0;

    if (cap < 2) return -1;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + (int)(v % 10UL)); v /= 10UL; }
    if (n >= cap) return -1;
    while (n) buf[i++] = tmp[--n];
    buf[i] = '\0';
    return i;
}

static int parse_int(const char *s, int *out)
{
    int neg = 0, i = 0, seen = 0;
    u32 acc = 0, limit;

    if (!s) return -1;
    if (s[0] == '-') { neg = 1; i = 1; }
    limit = neg ? 2147483648UL : 2147483647UL;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        seen = 1;
        if (acc > (limit - (u32)(s[i] - '0')) / 10UL) return -1;
        acc = acc * 10UL + (u32)(s[i] - '0');
    }
    if (!seen) return -1;
    /* INT_MIN の単項マイナスは signed overflow (UB)。表現できる値だけで組む。*/
    *out = neg ? -(int)(acc - 1UL) - 1 : (int)acc;
    return 0;
}

static int fmt_hex(char *buf, int cap, const unsigned char *p, int n)
{
    static const char digits[] = "0123456789abcdef";
    int i;
    if (n < 0 || n * 2 + 1 > cap) return -1;
    for (i = 0; i < n; i++) {
        buf[i * 2] = digits[(p[i] >> 4) & 0x0F];
        buf[i * 2 + 1] = digits[p[i] & 0x0F];
    }
    buf[n * 2] = '\0';
    return n * 2;
}

static int hex_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex(const char *s, unsigned char *out, int cap)
{
    int n = 0, hi, lo, i = 0;
    if (!s) return -1;
    while (s[i]) {
        hi = hex_val((unsigned char)s[i]);
        if (hi < 0 || !s[i + 1]) return -1;
        lo = hex_val((unsigned char)s[i + 1]);
        if (lo < 0) return -1;
        if (n >= cap) return -1;
        out[n++] = (unsigned char)((hi << 4) | lo);
        i += 2;
    }
    return n;
}

static int fmt_b64(char *buf, int cap, const unsigned char *p, int n)
{
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i = 0, o = 0;
    u32 v;
    if (n < 0 || ((n + 2) / 3) * 4 + 1 > cap) return -1;
    while (i + 2 < n) {
        v = ((u32)p[i] << 16) | ((u32)p[i + 1] << 8) | (u32)p[i + 2];
        buf[o++] = t[(v >> 18) & 63];
        buf[o++] = t[(v >> 12) & 63];
        buf[o++] = t[(v >> 6) & 63];
        buf[o++] = t[v & 63];
        i += 3;
    }
    if (n - i == 1) {
        v = (u32)p[i] << 16;
        buf[o++] = t[(v >> 18) & 63];
        buf[o++] = t[(v >> 12) & 63];
        buf[o++] = '=';
        buf[o++] = '=';
    } else if (n - i == 2) {
        v = ((u32)p[i] << 16) | ((u32)p[i + 1] << 8);
        buf[o++] = t[(v >> 18) & 63];
        buf[o++] = t[(v >> 12) & 63];
        buf[o++] = t[(v >> 6) & 63];
        buf[o++] = '=';
    }
    buf[o] = '\0';
    return o;
}

/* JSON の文字列本体 (両端の " は呼び手が書く)。UTF-8 はそのまま通す。 */
static int fmt_json_str(char *buf, int cap, const char *s, int n)
{
    static const char digits[] = "0123456789abcdef";
    int i, o = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            if (o + 2 >= cap) return -1;
            buf[o++] = '\\';
            buf[o++] = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t' || c < 0x20) {
            if (o + 6 >= cap) return -1;
            buf[o++] = '\\';
            if (c == '\n') { buf[o++] = 'n'; }
            else if (c == '\r') { buf[o++] = 'r'; }
            else if (c == '\t') { buf[o++] = 't'; }
            else {
                buf[o++] = 'u';
                buf[o++] = '0';
                buf[o++] = '0';
                buf[o++] = digits[(c >> 4) & 0x0F];
                buf[o++] = digits[c & 0x0F];
            }
        } else {
            if (o + 1 >= cap) return -1;
            buf[o++] = (char)c;
        }
    }
    if (o >= cap) return -1;
    buf[o] = '\0';
    return o;
}

static const char *status_name(int st)
{
    switch (st) {
    case CFG_OK:      return "OK";
    case CFG_MISSING: return "MISSING";
    case CFG_CORRUPT: return "CORRUPT";
    case CFG_VERSION: return "VERSION";
    default:          return "ERROR";
    }
}

static const char *type_name(int t)
{
    switch (t) {
    case CFG_TYPE_INT:  return "int";
    case CFG_TYPE_TEXT: return "text";
    case CFG_TYPE_BLOB: return "blob";
    default:            return "?";
    }
}

/* ======================================================================== */
/*  出力                                                                     */
/* ======================================================================== */

static char out_buf[CFG_OUT_MAX];
static int  out_len;

static void out_reset(void)
{
    out_len = 0;
    out_err = 0;
}

/* n バイト足せるか (溢れを「予定どおり」に扱う list / export 用)。 */
static int out_room(int n)
{
    return n >= 0 && out_len + n <= CFG_OUT_MAX;
}

static int out_put(const char *p, int n)
{
    int i;
    if (n < 0 || out_len + n > CFG_OUT_MAX) { out_err = 1; return -1; }
    for (i = 0; i < n; i++) out_buf[out_len + i] = p[i];
    out_len += n;
    return 0;
}

static int out_str(const char *s)
{
    return out_put(s, s_len(s));
}

static int out_num(int v)
{
    char tmp[16];
    int n = fmt_int(tmp, (int)sizeof(tmp), v);
    if (n < 0) { out_err = 1; return -1; }
    return out_put(tmp, n);
}

static int out_unum(u32 v)
{
    char tmp[16];
    int n = fmt_u32(tmp, (int)sizeof(tmp), v);
    if (n < 0) { out_err = 1; return -1; }
    return out_put(tmp, n);
}

/* fd へ全部書き切る。short write を「書けた」ことにしない (往復 1 の ⑬)。 */
static int write_all(int fd, const char *p, int n)
{
    int done = 0, rc;
    while (done < n) {
        rc = api->sys_write(fd, p + done, (u32)(n - done));
        if (rc <= 0) return -1;
        done += rc;
    }
    return 0;
}

/* DB を閉じた後に呼ぶ。1KB ごとに sys_yield を挟み、端末に読み出す間を作る。
 * 0 = 全部書けた / -1 = 途中で落ちた。 */
static int out_flush_console(void)
{
    int off = 0, n, bad = 0;
    while (off < out_len) {
        n = out_len - off;
        if (n > CFG_OUT_CHUNK) n = CFG_OUT_CHUNK;
        if (write_all(1, out_buf + off, n) != 0) { bad = 1; break; }
        off += n;
        if (off < out_len) api->sys_yield();
    }
    out_len = 0;
    if (bad) out_err = 1;
    return bad ? -1 : 0;
}

/* 溜めた分をファイルへ流す (export)。コンソールではないので yield は要らない。*/
static int out_flush_fd(int fd)
{
    int rc = (out_len > 0) ? write_all(fd, out_buf, out_len) : 0;
    out_len = 0;
    if (rc != 0) out_err = 1;
    return rc;
}

static void usage(void)
{
    out_str("usage: cfg get <scope> <key> [default]\n");
    out_str("       cfg set <scope> <key> int|text|blob <value>\n");
    out_str("       cfg del <scope> <key>\n");
    out_str("       cfg list [<scope> [<prefix>]]\n");
    out_str("       cfg status\n");
    out_str("       cfg init [--tsv <path>]\n");
    out_str("       cfg export <file>\n");
    out_str("       cfg import <file> [--scope <scope>] [--merge]\n");
}

/* MISSING のときの案内 (DESIGN §2 の文言は S3 のリカバリ用に残す)。 */
static void note_status(CfgDb *db)
{
    if (cfg_status(db) == CFG_MISSING)
        out_str("settings.db missing: run 'cfg init'\n");
}

static int close_note(CfgDb *db, int rc)
{
    if (cfg_close(db) != 0) {
        out_str("close failed (");
        out_num(cfg_last_close_error());
        out_str(")\n");
        return 1;
    }
    return rc;
}

/* ======================================================================== */
/*  出力先の保護 (票 §2 / 往復 1 の ③)                                       */
/*                                                                          */
/*  `cfg export /etc/settings.db` は設定 DB 自身を O_TRUNC で潰す。          */
/*  `-journal` に書けば次の open が hot journal = CORRUPT になる。           */
/*  相対名や `..` も同じ場所を指せるので、**正規化してから**突き合わせる。    */
/* ======================================================================== */

static const char *const PROTECTED_PATHS[] = {
    CFG_DB_PATH, CFG_DB_JOURNAL_PATH,
    CFG_DB_NEW_PATH, CFG_DB_NEW_JOURNAL_PATH,
    (const char *)0
};

/* 絶対名へ直し `.` / `..` / 連続 `/` を畳む。0 / -1 (長すぎ・空)。 */
static int norm_path(const char *in, char *out, int cap)
{
    char tmp[OS32_MAX_PATH * 2];
    const char *parts[OS32_MAX_PATH / 2];
    int lens[OS32_MAX_PATH / 2];
    int n = 0, np = 0, i, p, o;
    const char *cwd;

    if (!in || !in[0] || cap < 2) return -1;
    if (in[0] != '/') {
        cwd = api->sys_getcwd ? api->sys_getcwd() : (const char *)0;
        if (!cwd || !cwd[0]) cwd = "/";
        for (i = 0; cwd[i]; i++) {
            if (n >= (int)sizeof(tmp) - 2) return -1;
            tmp[n++] = cwd[i];
        }
        if (n == 0 || tmp[n - 1] != '/') tmp[n++] = '/';
    }
    for (i = 0; in[i]; i++) {
        if (n >= (int)sizeof(tmp) - 1) return -1;
        tmp[n++] = in[i];
    }
    tmp[n] = '\0';

    p = 0;
    while (tmp[p]) {
        int start, len;
        if (tmp[p] == '/') { p++; continue; }
        start = p;
        while (tmp[p] && tmp[p] != '/') p++;
        len = p - start;
        if (len == 1 && tmp[start] == '.') continue;
        if (len == 2 && tmp[start] == '.' && tmp[start + 1] == '.') {
            if (np > 0) np--;
            continue;
        }
        if (np >= (int)(sizeof(parts) / sizeof(parts[0]))) return -1;
        parts[np] = &tmp[start];
        lens[np] = len;
        np++;
    }
    o = 0;
    out[o++] = '/';
    for (i = 0; i < np; i++) {
        int j;
        if (i > 0) {
            if (o >= cap - 1) return -1;
            out[o++] = '/';
        }
        for (j = 0; j < lens[i]; j++) {
            if (o >= cap - 1) return -1;
            out[o++] = parts[i][j];
        }
    }
    out[o] = '\0';
    return 0;
}

/* 既に在るファイル同士の同一性 (ハードリンクや別名でも同じ inode を指す)。 */
/* 1 = 同じ / 0 = 違う / -1 = 判定不能。stat の I/O 障害を「別ファイル」に
 * 丸めると、DB を指す別名を O_TRUNC で潰せる (往復 2 の 7)。 */
static int same_file(const char *a, const char *b)
{
    OS32_Stat sa, sb;
    int ra = api->sys_stat(a, &sa);
    int rb;
    if (ra == OS32_ERR_NOTFOUND) return 0;       /* まだ無い = 別物 */
    if (ra != 0) return -1;
    rb = api->sys_stat(b, &sb);
    if (rb == OS32_ERR_NOTFOUND) return 0;
    if (rb != 0) return -1;
    if (sa.st_dev != sb.st_dev) return 0;             /* 別デバイス = 別物 */
    /* inode を供給しない FS (FAT は `fs/fatfs_vfs.c` が st_ino を 0 のまま
     * 返す) では同一性を確かめられない。「別物」に丸めると `/ETC/SETTINGS.DB`
     * のような別名で DB 自身を O_TRUNC で潰せる (往復 3 の B1)。
     * 判定できないなら **書かない**。 */
    if (sa.st_ino == 0 || sb.st_ino == 0) return -1;
    return sa.st_ino == sb.st_ino ? 1 : 0;
}

/* ASCII の大小を無視した一致。FAT は大文字小文字を区別しないので、
 * 正規化した名前の完全一致だけでは別名を捕まえられない。 */
static int s_ieq(const char *a, const char *b)
{
    int i = 0, ca, cb;
    if (!a || !b) return 0;
    for (;;) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        if (ca == 0) return 1;
        i++;
    }
}

/* 0 = 書いてよい / -1 = 設定 DB 側のファイル */
static int output_allowed(const char *path, char *norm, int cap)
{
    int i;
    if (norm_path(path, norm, cap) != 0) return -1;
    for (i = 0; PROTECTED_PATHS[i]; i++) {
        if (s_eq(norm, PROTECTED_PATHS[i])) return -1;
        if (s_ieq(norm, PROTECTED_PATHS[i])) return -1;   /* FAT の別名 (B1) */
        if (same_file(norm, PROTECTED_PATHS[i]) != 0) return -1;  /* 不明も拒否 */
    }
    return 0;
}

/* ======================================================================== */
/*  列挙の控え (list / export / get の型引き)                                */
/* ======================================================================== */

static char l_keys[CFG_ENUM_MAX][CFG_KEY_MAX + 1];
static int  l_types[CFG_ENUM_MAX];
static int  l_nkeys;
static char l_scopes[CFG_SCOPES_MAX][CFG_SCOPE_MAX + 1];
static int  l_nscopes;
/* 出力バッファが一杯になったら DB を閉じて吐き出し、続きから開き直す。
 * 列挙は (scope, key) 順なので、最後に出した組より後だけを出せば再開できる。*/
static char r_scope[CFG_SCOPE_MAX + 1];
static char r_key[CFG_KEY_MAX + 1];
static int  r_active;

static int collect_key(const char *key, int type, void *ctx)
{
    (void)ctx;
    if (l_nkeys >= CFG_ENUM_MAX) return 1;
    s_cpy(l_keys[l_nkeys], key, CFG_KEY_MAX + 1);
    l_types[l_nkeys] = type;
    l_nkeys++;
    return 0;
}

static int collect_scope(const char *scope, void *ctx)
{
    (void)ctx;
    if (l_nscopes >= CFG_SCOPES_MAX) return 1;
    s_cpy(l_scopes[l_nscopes], scope, CFG_SCOPE_MAX + 1);
    l_nscopes++;
    return 0;
}

/* ======================================================================== */
/*  status                                                                   */
/* ======================================================================== */

static int do_status(void)
{
    CfgDb *db;
    int st, rc;

    if (cfg_open(&db, 0) != 0) { out_str("cfg: cannot open\n"); return 1; }
    st = cfg_status(db);
    out_str(status_name(st));
    if (st == CFG_OK || st == CFG_VERSION) {
        out_str(" schema_version ");
        out_num(cfg_schema_version(db));
    } else if (st == CFG_ERROR) {
        out_str(" sqlite=");
        out_num(cfg_last_sqlite(db));
    } else if (st == CFG_CORRUPT) {
        out_str(" sqlite=");
        out_num(cfg_last_sqlite(db));
    }
    /* SQLite の MEMSYS5 プールの使用量 (票 S5-C(2)、DESIGN §6 のプール)。
     * これは**プール全体**の使用量 — FEP 辞書など他の常駐接続の分も含む。
     * 設定 DB 単体の取り分ではない (レビュー往復 1 の non-blocker)。
     * 採るのは**接続を持っている間**なので、`cfg status` を DB の無い機械と
     * 有る機械で比べれば差分が読める。close の後だと素の値に戻ってしまう。
     * 状態に関わらず必ず末尾に付ける — MISSING でも常駐分が読めるように
     * するため (受入 C2 の文言はこの追記を許す)。 */
    out_str(" pool ");
    out_unum(api->db_mem_used());
    out_str(" B");
    out_str("\n");
    rc = (st == CFG_OK) ? 0 : 1;
    return close_note(db, rc);
}

/* ======================================================================== */
/*  get                                                                      */
/* ======================================================================== */

static unsigned char v_blob[CFG_BLOB_MAX];
static char v_text[CFG_TEXT_MAX + 1];
static char v_hex[CFG_BLOB_MAX * 2 + 1];

static int do_get(const CfgArgs *a)
{
    CfgDb *db;
    int st, rc = 0, have = 0, bad = 0, iv = 0, blen = 0, tlen = 0;
    int kind = -1, info;

    if (cfg_open(&db, 0) != 0) { out_str("cfg: cannot open\n"); return 1; }
    st = cfg_status(db);
    if (st == CFG_OK || st == CFG_VERSION) {
        /* 完全一致の 1 行照会で型を引く。前方一致の列挙だと同じ scope に
         * 257 件あるだけで NOSPC になり、実値があるのに既定値を出す (⑪)。 */
        info = cfg_get_info(db, a->scope, a->key);
        if (info == OS32_ERR_NOTFOUND) {
            /* 未設定 */
        } else if (info < 0) {
            bad = 1;                       /* 障害 / 契約外の保存値 */
        } else if (CFG_INFO_IS_NULL(info)) {
            /* 行はあるが値が NULL — 未設定扱い (⑥) */
        } else {
            kind = CFG_INFO_TYPE(info);
            if (kind == CFG_TYPE_INT) {
                /* 値の取得だけが落ちることもある。0 を成功にしない (往復 2 の 1)。*/
                if (cfg_read_int(db, a->scope, a->key, &iv) == 0) have = 1;
                else bad = 1;
            } else if (kind == CFG_TYPE_TEXT) {
                tlen = cfg_get_text(db, a->scope, a->key, v_text,
                                    (int)sizeof(v_text));
                if (tlen >= 0) have = 1; else bad = 1;
            } else {
                blen = cfg_get_blob(db, a->scope, a->key, v_blob,
                                    (int)sizeof(v_blob));
                if (blen >= 0) have = 1; else bad = 1;
            }
        }
    }
    if (bad || cfg_status(db) == CFG_ERROR) rc = 1;
    note_status(db);
    rc = close_note(db, rc);

    if (bad) {
        out_str("get failed\n");
    } else if (have && kind == CFG_TYPE_INT) {
        out_num(iv);
        out_str("\n");
    } else if (have && kind == CFG_TYPE_TEXT) {
        out_put(v_text, tlen);
        out_str("\n");
    } else if (have && kind == CFG_TYPE_BLOB) {
        if (fmt_hex(v_hex, (int)sizeof(v_hex), v_blob, blen) < 0) {
            out_str("(blob too large)\n");
            rc = 1;
        } else {
            out_str(v_hex);
            out_str("\n");
        }
    } else if (a->def) {
        out_str(a->def);
        out_str("\n");
    } else {
        out_str("(not set)\n");
    }
    return rc;
}

/* ======================================================================== */
/*  set / del                                                                */
/* ======================================================================== */

static int do_set(const CfgArgs *a, int del)
{
    CfgDb *db;
    int st, rc = 0, iv = 0, n = 0;

    if (cfg_open(&db, 1) != 0) { out_str("cfg: cannot open\n"); return 1; }
    st = cfg_status(db);
    if (st != CFG_OK) {
        out_str("cannot write: ");
        out_str(status_name(st));
        out_str("\n");
        note_status(db);
        return close_note(db, 1);
    }
    if (!del && s_eq(a->type, "int") && parse_int(a->value, &iv) != 0) {
        out_str("bad int value\n");
        return close_note(db, 1);
    }
    if (!del && s_eq(a->type, "blob")) {
        n = parse_hex(a->value, v_blob, (int)sizeof(v_blob));
        if (n < 0) { out_str("bad hex value\n"); return close_note(db, 1); }
    }
    if (cfg_begin(db) != 0) {
        out_str("begin failed\n");
        return close_note(db, 1);
    }
    if (del) {
        rc = cfg_delete(db, a->scope, a->key);
    } else if (s_eq(a->type, "int")) {
        rc = cfg_set_int(db, a->scope, a->key, iv);
    } else if (s_eq(a->type, "text")) {
        rc = cfg_set_text(db, a->scope, a->key, a->value);
    } else {
        rc = cfg_set_blob(db, a->scope, a->key, v_blob, n);
    }
    if (rc != 0) {
        out_str(del ? "delete rejected\n" : "set rejected\n");
        cfg_rollback(db);
        return close_note(db, 1);
    }
    if (cfg_commit(db) != 0) {
        out_str("commit failed\n");
        return close_note(db, 1);
    }
    return close_note(db, 0);
}

/* ======================================================================== */
/*  list                                                                     */
/* ======================================================================== */

/* 1 行 = "<scope>\t<key>\t<type>\t<value>"。blob は長さだけ (hex は get で)。
 * 戻り: 0 = 溜めた / 1 = 溜め場が足りない (閉じて吐いてから出し直す) /
 *      -1 = 値の取得に失敗 (⑤: 0 や空値で埋めない)。 */
static int emit_row(CfgDb *db, const char *scope, const char *key, int *kind_io)
{
    char row[CFG_ROW_MAX];
    int o = 0, n, i, kind, iv, info;

    info = cfg_get_info(db, scope, key);
    *kind_io = info;
    if (info < 0) return -1;                 /* 障害 / 契約外の保存値 */
    kind = CFG_INFO_TYPE(info);

    for (i = 0; scope[i] && o < CFG_ROW_MAX - 8; i++) row[o++] = scope[i];
    row[o++] = '\t';
    for (i = 0; key[i] && o < CFG_ROW_MAX - 8; i++) row[o++] = key[i];
    row[o++] = '\t';
    for (i = 0; type_name(kind)[i] && o < CFG_ROW_MAX - 8; i++)
        row[o++] = type_name(kind)[i];
    row[o++] = '\t';
    if (CFG_INFO_IS_NULL(info)) {
        for (i = 0; "(unset)"[i] && o < CFG_ROW_MAX - 2; i++)
            row[o++] = "(unset)"[i];
    } else {
        if (kind == CFG_TYPE_INT) {
            /* 値の取得だけが落ちることもある (往復 2 の 1)。0 で埋めない。 */
            if (cfg_read_int(db, scope, key, &iv) != 0) return -1;
            n = fmt_int(row + o, CFG_ROW_MAX - o - 2, iv);
            if (n < 0) return -1;
            o += n;
        } else if (kind == CFG_TYPE_TEXT) {
            n = cfg_get_text(db, scope, key, v_text, (int)sizeof(v_text));
            if (n < 0) return -1;
            for (i = 0; i < n && o < CFG_ROW_MAX - 2; i++) row[o++] = v_text[i];
        } else {
            n = cfg_get_blob(db, scope, key, v_blob, (int)sizeof(v_blob));
            if (n < 0) return -1;
            row[o++] = 'b';
            row[o++] = 'l';
            row[o++] = 'o';
            row[o++] = 'b';
            row[o++] = ':';
            n = fmt_int(row + o, CFG_ROW_MAX - o - 2, n);
            if (n < 0) return -1;
            o += n;
        }
    }
    row[o++] = '\n';
    if (!out_room(o)) return 1;
    return out_put(row, o) == 0 ? 0 : -1;
}

static int do_list(const CfgArgs *a)
{
    CfgDb *db;
    int st, si, ki, rc = 0, more = 1, n, kind, emitted;

    if (a->scope && !s_len_ok(a->scope, CFG_SCOPE_MAX)) {
        /* 切り詰めると **別の scope** の値を出してしまう (⑭)。 */
        out_str("bad scope\n");
        return 1;
    }
    r_active = 0;
    while (more) {
        more = 0;
        if (cfg_open(&db, 0) != 0) { out_str("cfg: cannot open\n"); return 1; }
        st = cfg_status(db);
        if (st != CFG_OK && st != CFG_VERSION) {
            note_status(db);
            if (st != CFG_MISSING) {
                out_str(status_name(st));
                out_str("\n");
            }
            return close_note(db, st == CFG_MISSING ? 0 : 1);
        }
        l_nscopes = 0;
        if (a->scope) {
            s_cpy(l_scopes[0], a->scope, CFG_SCOPE_MAX + 1);
            l_nscopes = 1;
        } else {
            n = cfg_enum_scopes(db, collect_scope, (void *)0);
            if (n == OS32_ERR_NOSPC) {
                out_str("too many scopes\n");
                return close_note(db, 1);
            }
            if (n < 0) {
                out_str("list failed\n");
                return close_note(db, 1);
            }
        }
        for (si = 0; si < l_nscopes && !more; si++) {
            if (r_active && s_cmp(l_scopes[si], r_scope) < 0) continue;
            l_nkeys = 0;
            n = cfg_enum(db, l_scopes[si], a->prefix, collect_key, (void *)0);
            if (n < 0) {
                out_str("list failed\n");
                return close_note(db, 1);
            }
            for (ki = 0; ki < l_nkeys; ki++) {
                if (r_active && s_cmp(l_scopes[si], r_scope) == 0 &&
                    s_cmp(l_keys[ki], r_key) <= 0) continue;
                emitted = emit_row(db, l_scopes[si], l_keys[ki], &kind);
                if (emitted < 0) {
                    out_str("list failed (");
                    out_str(l_scopes[si]);
                    out_str(" ");
                    out_str(l_keys[ki]);
                    out_str(")\n");
                    return close_note(db, 1);
                }
                if (emitted == 1) {
                    more = 1;              /* 溢れた — 閉じて吐いてから続き */
                    break;
                }
                s_cpy(r_scope, l_scopes[si], CFG_SCOPE_MAX + 1);
                s_cpy(r_key, l_keys[ki], CFG_KEY_MAX + 1);
                r_active = 1;
            }
        }
        rc = close_note(db, rc);
        if (rc != 0) return rc;
        if (more && out_flush_console() != 0) return 1;
    }
    return rc;
}

/* ======================================================================== */
/*  init                                                                     */
/* ======================================================================== */

static int do_init(const CfgArgs *a)
{
    int rc = cfg_init(a->path);
    int why = cfg_last_init_reason();

    if (rc == 0) {
        out_str("cfg: created ");
        out_str(CFG_DB_PATH);
        out_str("\n");
        return 0;
    }
    switch (why) {
    case CFG_INIT_EXISTS:    out_str("already exists\n"); break;
    case CFG_INIT_JOURNAL:   out_str("needs recovery: journal present\n"); break;
    case CFG_INIT_STALE_NEW: out_str("needs recovery: stale .new\n"); break;
    case CFG_INIT_TSV:       out_str("cannot read the tsv\n"); break;
    case CFG_INIT_CLOSE:     out_str("init failed: close\n"); break;
    case CFG_INIT_AMBIGUOUS: out_str("needs recovery: rename left both names\n"); break;
    case CFG_INIT_RENAME:    out_str("init failed: rename\n"); break;
    case CFG_INIT_VERIFY:    out_str("init failed: verify\n"); break;
    case CFG_INIT_STAT:      out_str("needs recovery: cannot stat\n"); break;
    default:                 out_str("init failed\n"); break;
    }
    return 1;
}

/* ======================================================================== */
/*  export — DESIGN §6b の JSON (1 行 1 レコード)                            */
/* ======================================================================== */


static char x_line[CFG_LINE_MAX];
static int  x_fd;

/* JSON の骨 (構造の文字) をそのまま足す。値は fmt_json_str / fmt_* を通す。*/
static int x_lit(int *o, const char *s)
{
    int i;
    for (i = 0; s[i]; i++) {
        if (*o + 2 >= CFG_LINE_MAX) return -1;
        x_line[(*o)++] = s[i];
    }
    return 0;
}

/* 1 レコードを x_line に組む。
 * 戻り: 長さ / -1 = 値の取得か整形に失敗 (⑤⑥: 0 や空値で埋めない)。 */
static int export_row(CfgDb *db, const char *scope, const char *key)
{
    int o = 0, n, m, kind, iv, info;

    info = cfg_get_info(db, scope, key);
    if (info < 0) return -1;
    /* 型は **宣言型 (0/1/2) のまま**。NULL は値の側で表す (往復 2 の 4) —
     * バックアップから元の型を復元できなくなるため。 */
    kind = CFG_INFO_TYPE(info);

    if (x_lit(&o, "{\"scope\":\"") != 0) return -1;
    n = fmt_json_str(x_line + o, CFG_LINE_MAX - o, scope, s_len(scope));
    if (n < 0) return -1;
    o += n;
    if (x_lit(&o, "\",\"key\":\"") != 0) return -1;
    n = fmt_json_str(x_line + o, CFG_LINE_MAX - o, key, s_len(key));
    if (n < 0) return -1;
    o += n;
    if (x_lit(&o, "\",\"type\":") != 0) return -1;
    n = fmt_int(x_line + o, CFG_LINE_MAX - o, kind);
    if (n < 0) return -1;
    o += n;
    if (x_lit(&o, ",\"v\":") != 0) return -1;
    if (CFG_INFO_IS_NULL(info)) {
        /* 行はあるが値の列が NULL。空値と取り違えられない形で書く (⑥)。 */
        if (x_lit(&o, "null") != 0) return -1;
    } else if (kind == CFG_TYPE_INT) {
        if (cfg_read_int(db, scope, key, &iv) != 0) return -1;
        n = fmt_int(x_line + o, CFG_LINE_MAX - o, iv);
        if (n < 0) return -1;
        o += n;
    } else if (kind == CFG_TYPE_TEXT) {
        m = cfg_get_text(db, scope, key, v_text, (int)sizeof(v_text));
        if (m < 0) return -1;
        if (x_lit(&o, "\"") != 0) return -1;
        n = fmt_json_str(x_line + o, CFG_LINE_MAX - o, v_text, m);
        if (n < 0) return -1;
        o += n;
        if (x_lit(&o, "\"") != 0) return -1;
    } else {
        m = cfg_get_blob(db, scope, key, v_blob, (int)sizeof(v_blob));
        if (m < 0) return -1;
        if (x_lit(&o, "\"") != 0) return -1;
        n = fmt_b64(x_line + o, CFG_LINE_MAX - o, v_blob, m);
        if (n < 0) return -1;
        o += n;
        if (x_lit(&o, "\"") != 0) return -1;
    }
    if (x_lit(&o, "}\n") != 0) return -1;
    return o;
}

/* 途中で落ちたときは中途半端なバックアップを残さない。 */
static int export_abort(CfgDb *db, const char *norm, const char *msg)
{
    if (x_fd >= 0) { api->sys_close(x_fd); x_fd = -1; }
    if (norm) api->sys_unlink(norm);
    out_str(msg);
    out_str("\n");
    return db ? close_note(db, 1) : 1;
}

static int do_export(const CfgArgs *a)
{
    static char norm[OS32_MAX_PATH];
    CfgDb *db;
    int st, si, ki, n, o, rows = 0, more = 1, first = 1;

    x_fd = -1;
    r_active = 0;               /* 再開位置は list と共有。必ず先頭から */
    /* 出力先が設定 DB / journal / `.new*` なら拒否 (③)。DB を開く前に見る。*/
    if (output_allowed(a->path, norm, (int)sizeof(norm)) != 0) {
        out_str("refusing to write the settings database itself\n");
        return 1;
    }

    while (more) {
        more = 0;
        if (cfg_open(&db, 0) != 0) {
            return export_abort((CfgDb *)0, first ? (const char *)0 : norm,
                                "cfg: cannot open");
        }
        st = cfg_status(db);
        if (st != CFG_OK && st != CFG_VERSION) {
            out_str("cannot export: ");
            out_str(status_name(st));
            out_str("\n");
            note_status(db);
            if (!first) { api->sys_unlink(norm); }
            return close_note(db, 1);
        }
        if (first) {
            x_fd = api->sys_open(norm, KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
            if (x_fd < 0) {
                out_str("cannot create the export file\n");
                return close_note(db, 1);
            }
            /* ヘッダの版は **実値** (VERSION なら認識版より大きい値のまま)。*/
            o = 0;
            if (x_lit(&o, "{\"schema_version\":") != 0)
                return export_abort(db, norm, "export failed");
            n = fmt_int(x_line + o, CFG_LINE_MAX - o, cfg_schema_version(db));
            if (n < 0) return export_abort(db, norm, "export failed");
            o += n;
            if (x_lit(&o, ",\"exported\":\"") != 0)
                return export_abort(db, norm, "export failed");
            n = fmt_int(x_line + o, CFG_LINE_MAX - o, (int)api->get_tick());
            if (n < 0) return export_abort(db, norm, "export failed");
            o += n;
            if (x_lit(&o, "\"}\n") != 0)
                return export_abort(db, norm, "export failed");
            if (out_put(x_line, o) != 0)
                return export_abort(db, norm, "export failed");
            first = 0;
        }

        l_nscopes = 0;
        n = cfg_enum_scopes(db, collect_scope, (void *)0);
        if (n == OS32_ERR_NOSPC) return export_abort(db, norm, "too many scopes");
        if (n < 0) return export_abort(db, norm, "export failed");

        for (si = 0; si < l_nscopes && !more; si++) {
            if (r_active && s_cmp(l_scopes[si], r_scope) < 0) continue;
            l_nkeys = 0;
            if (cfg_enum(db, l_scopes[si], (const char *)0,
                         collect_key, (void *)0) < 0)
                return export_abort(db, norm, "export failed");
            for (ki = 0; ki < l_nkeys; ki++) {
                if (r_active && s_cmp(l_scopes[si], r_scope) == 0 &&
                    s_cmp(l_keys[ki], r_key) <= 0) continue;
                n = export_row(db, l_scopes[si], l_keys[ki]);
                if (n < 0) return export_abort(db, norm, "export failed");
                if (!out_room(n)) { more = 1; break; }   /* 閉じてから書く */
                if (out_put(x_line, n) != 0)
                    return export_abort(db, norm, "export failed");
                rows++;
                s_cpy(r_scope, l_scopes[si], CFG_SCOPE_MAX + 1);
                s_cpy(r_key, l_keys[ki], CFG_KEY_MAX + 1);
                r_active = 1;
            }
        }
        /* 票 §2 の「出力は DB を閉じた後」をファイル側にも通す。溜め場が
         * 一杯になったら閉じて書き、開き直して続きから (list と同じ再開)。 */
        if (cfg_close(db) != 0) {
            out_str("close failed (");
            out_num(cfg_last_close_error());
            out_str(")\n");
            if (x_fd >= 0) { api->sys_close(x_fd); x_fd = -1; }
            api->sys_unlink(norm);
            return 1;
        }
        if (out_flush_fd(x_fd) != 0)
            return export_abort((CfgDb *)0, norm, "write failed");
    }
    api->sys_close(x_fd);
    x_fd = -1;
    out_str("cfg: exported ");
    out_num(rows);
    out_str(" records\n");
    return 0;
}

/* ======================================================================== */
/*  import — 票 S3 §2                                                        */
/*                                                                          */
/*  読み取り・検証・2 巡・単一トランザクションは libos32cfg の                */
/*  `cfg_import_file` が持つ。ここは引数の正規化と文言だけ。                 */
/* ======================================================================== */

static int do_import(const CfgArgs *a)
{
    static char norm[OS32_MAX_PATH];
    CfgImportInfo info;
    int rc;

    /* export と同じ正規化 (相対名 / `.` / `..` を絶対名へ)。 */
    if (norm_path(a->path, norm, (int)sizeof(norm)) != 0) {
        out_str("cannot open the import file\n");
        return 1;
    }
    rc = cfg_import_file(norm, a->scope, a->merge, &info);
    if (rc == CFG_IMPORT_OK || rc == CFG_IMPORT_E_CLOSE) {
        out_str("imported ");
        out_num(info.rows);
        out_str(" records (");
        out_str(a->scope ? a->scope : "all scopes");
        out_str("), ");
        out_str(a->merge ? "merged" : "replaced");
        if (rc == CFG_IMPORT_E_CLOSE) {
            /* commit は通っている = **更新済み**。隠さずに出して終了 1。 */
            out_str(" (close failed ");
            out_num(info.detail);
            out_str(")");
        }
        out_str("\n");
        return rc == CFG_IMPORT_OK ? 0 : 1;
    }
    /* 失敗の文言。改行は付けない — 後始末の失敗を同じ行の尾に足すため。 */
    switch (rc) {
    case CFG_IMPORT_E_STATUS:
        out_str("cannot import: ");
        out_str(status_name(info.status));
        break;
    case CFG_IMPORT_E_VERSION:
        out_str("newer backup: schema_version ");
        out_num(info.version);
        break;
    case CFG_IMPORT_E_LINE:
        out_str("bad line ");
        out_num(info.lineno);
        out_str(": ");
        out_str(cfg_import_detail_name(info.detail));
        break;
    case CFG_IMPORT_E_DUP:
        out_str("duplicate record at line ");
        out_num(info.lineno);
        break;
    case CFG_IMPORT_E_MANY:
        out_str("too many records");
        break;
    case CFG_IMPORT_E_LONG:
        out_str("line ");
        out_num(info.lineno);
        out_str(" too long");
        break;
    case CFG_IMPORT_E_HEADER:
        out_str("not a settings backup");
        break;
    case CFG_IMPORT_E_OPEN:
        out_str("cannot open the import file");
        break;
    case CFG_IMPORT_E_IO:
        out_str("read failed");
        break;
    case CFG_IMPORT_E_CHANGED:
        out_str("input changed during import");
        break;
    case CFG_IMPORT_E_ARG:
        out_str("bad scope");
        break;
    default:
        out_str("import failed (");
        out_num(info.detail);
        out_str(")");
        break;
    }
    /* rollback / close が失敗していたら「1 行も残らない」とは言えない。
     * 原因の文言を残したまま別欄で出す (レビュー往復 1 の B4)。 */
    if (info.cleanup != 0) {
        out_str(" (rollback/close failed ");
        out_num(info.cleanup);
        out_str(")");
    }
    out_str("\n");
    if (rc == CFG_IMPORT_E_STATUS && info.status == CFG_MISSING)
        out_str("settings.db missing: run 'cfg init'\n");
    return 1;
}
