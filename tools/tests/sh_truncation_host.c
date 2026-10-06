/* ========================================================================
 *  sh_truncation_host.c — シェルの「黙って切り詰める」経路を **実物のソース
 *                         で** 押さえる足場
 *
 *  対象票: docs/archive/shell/TASK_SH_TRUNCATION.md §5 の段 1「足場」
 *  実行:   python3 -B tools/tests/test_sh_truncation.py
 *  記録:   tools/tests/sh_truncation_tdd.md
 *
 *  1 行も写さずそのまま #include する実物 (模型ではない):
 *    - userland/shell/main.c        (execute_command / execute_single /
 *                                    apply_redirects / split_pipeline、
 *                                    および sh_exec.inc = try_exec /
 *                                    try_exec_from_path / run_cmd_internal)
 *    - userland/shell/cmd_script.c  (source / if / goto / ask)
 *    - userland/shell/cmd_env.c     (env_expand / set / export)
 *    - userland/shell/cmd_base.c    (time / exit)
 *    - userland/shell/cmd_mnt.c     (exec)
 *    - userland/shell/cmd_dir.c / cmd_file.c / cmd_fs_shared.c / cmd_sys.c
 *
 *  つまり **登録表も execute_command も本物**で、`exit` を直接見るような
 *  スタブは 1 つも置いていない (sh_shell_host.c は execute_command を
 *  スタブにしているので、切り詰めの経路はそこでは試験できない)。
 *
 *  main.c の main() は _start から呼ばない (KernelAPI を受け取る形なので)。
 *  代わりに登録関数を並べて呼ぶ sh_boot() を置く — main() が呼ぶ順と同じ。
 *
 *  tools/tests/sh_shell_host.c と同じ様式 — ホスト ILP32 GNU89、libc 無し
 *  (-nostdlib、Linux の int 0x80 で write/exit)。<string.h> / <stdio.h> /
 *  <stdlib.h> は python 側が一時ディレクトリに置く薄いシム。
 * ======================================================================== */

#include "shell.h"
#include "config.h"

/* ---- libc の代わり (str_eq などが引く分だけ) ---------------------------- */

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, unsigned long n)
{
    unsigned long i = 0;
    while (i < n && a[i] && a[i] == b[i]) i++;
    if (i == n) return 0;
    return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

unsigned long strlen(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

void *memcpy(void *d, const void *s, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) ((char *)d)[i] = ((const char *)s)[i];
    return d;
}

void *memset(void *d, int c, unsigned long n)
{
    unsigned long i;
    for (i = 0; i < n; i++) ((unsigned char *)d)[i] = (unsigned char)c;
    return d;
}

char *strncpy(char *d, const char *s, unsigned long n)
{
    unsigned long i = 0;
    while (i < n && s[i]) { d[i] = s[i]; i++; }
    while (i < n) d[i++] = '\0';
    return d;
}

char *strncat(char *d, const char *s, unsigned long n)
{
    unsigned long i = 0, j = 0;
    while (d[i]) i++;
    while (j < n && s[j]) { d[i + j] = s[j]; j++; }
    d[i + j] = '\0';
    return d;
}

char *strcat(char *d, const char *s)
{
    unsigned long i = 0, j = 0;
    while (d[i]) i++;
    while (s[j]) { d[i + j] = s[j]; j++; }
    d[i + j] = '\0';
    return d;
}

/* rshell.c の hotdeploy が引く (10 進 / 0x 16 進だけ見る最小実装) */
unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    while (*s == ' ') s++;
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2; base = 16;
    }
    if (base == 0) base = 10;
    for (;;) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
        s++;
    }
    if (end) *end = (char *)s;
    return v;
}

int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

/* ---- 最小の報告系 (libc 無し) ------------------------------------------ */

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}

static void report(const char *text)
{
    unsigned long len = 0;
    while (text[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static int failures;

static void check(int cond, const char *name)
{
    report(cond ? "  ok   " : "  FAIL ");
    report(name);
    report("\n");
    if (!cond) failures++;
}

/* ---- 出力の捕獲 -------------------------------------------------------- */

/* 行編集の試験は打鍵がそのまま画面へエコーされる (4092 バイト注入など) ので、
 * 2KB のままだと肝心の "command not found" が押し出されて**偽の緑**になる。 */
#define OUT_CAP  (96 * 1024)

static char g_out[OUT_CAP];
static int  g_out_len;

static void out_reset(void) { g_out_len = 0; g_out[0] = '\0'; }

static void out_byte(char c)
{
    if (g_out_len + 1 < OUT_CAP) g_out[g_out_len++] = c;
    g_out[g_out_len] = '\0';
}

static void out_str(const char *s) { while (*s) out_byte(*s++); }

/* 出力に部分列が現れるか */
static int out_has(const char *want)
{
    int i, j;
    int wl = 0;
    while (want[wl]) wl++;
    if (wl == 0) return 1;
    for (i = 0; i + wl <= g_out_len; i++) {
        for (j = 0; j < wl && g_out[i + j] == want[j]; j++) {}
        if (j == wl) return 1;
    }
    return 0;
}

/* 出力に部分列が **何回** 現れるか。断りを PATH 候補の数だけ出していないか
 * (= 断ったら走査ごと止めているか) を見るのに使う。 */
static int out_count(const char *want)
{
    int i, j, n = 0;
    int wl = 0;
    while (want[wl]) wl++;
    if (wl == 0) return 0;
    for (i = 0; i + wl <= g_out_len; i++) {
        for (j = 0; j < wl && g_out[i + j] == want[j]; j++) {}
        if (j == wl) n++;
    }
    return n;
}

/* 書式を 1 つ読んで可変引数を必ず 1 つ消費する (sh_shell_host.c と同じ) */
static void fmt_run(const char **pp, __builtin_va_list *ap)
{
    const char *p = *pp;

    p++;                                   /* '%' の次へ */
    if (*p == '%') { out_byte('%'); *pp = p + 1; return; }
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') p++;
    while (*p >= '0' && *p <= '9') p++;
    while (*p == 'l' || *p == 'h') p++;
    if (*p == 's') {
        out_str(__builtin_va_arg(*ap, const char *));
    } else if (*p) {
        (void)__builtin_va_arg(*ap, int);
        out_byte('#');
    }
    *pp = *p ? p + 1 : p;
}

int printf(const char *fmt, ...)
{
    __builtin_va_list ap;
    const char *p = fmt;

    __builtin_va_start(ap, fmt);
    while (*p) {
        if (*p == '%') fmt_run(&p, &ap);
        else out_byte(*p++);
    }
    __builtin_va_end(ap);
    return 0;
}

/* main.c が <stdio.h> から引くのはこの 3 つだけ */
int   fflush(void *stream)                               { (void)stream; return 0; }
int   setvbuf(void *stream, char *buf, int mode, unsigned long sz)
{
    (void)stream; (void)buf; (void)mode; (void)sz;
    return 0;
}
void *stdout_impl;

/* --- キーの台本 --------------------------------------------------------- */
#define KEYS_CAP 8192

static int g_keys[KEYS_CAP];
static int g_keys_len;
static int g_keys_pos;

/* 台本を使い切ったあと返し続ける値。既定は 0x1B (ESC) — rshell も filer も
 * shell_run も ESC/exit で抜ける。`ask` は ENTER (0x0D) でしか抜けないので、
 * その試験だけ keys_set_eof(0x0D) にする (取り違えると試験がハングする)。 */
static int g_keys_eof = 0x1B;

/* 台本を使い切った後の読みすぎ。数え切れないほど読んだら**ハングではなく
 * 落とす** — 端末ループの試験は台本を読み切ったら抜ける作りなので、
 * ここに来続けるのは試験側の組み立てが壊れている印。 */
static int g_key_overrun;

static void keys_reset(void)
{
    g_keys_len = 0;
    g_keys_pos = 0;
    g_keys_eof = 0x1B;
    g_key_overrun = 0;
}

static void keys_set_eof(int v) { g_keys_eof = v; }

static void key_push(int k)
{
    if (g_keys_len < KEYS_CAP) g_keys[g_keys_len++] = k;
}

static void key_push_str(const char *s)
{
    while (*s) key_push((int)(unsigned char)*s++);
}

static void key_push_run(char c, int n)
{
    int i;
    for (i = 0; i < n; i++) key_push((int)(unsigned char)c);
}

/* 台本を使い切ったら 0x1B (ESC) を返し続ける — どのループも必ず抜ける。 */
static int key_next(void)
{
    if (g_keys_pos < g_keys_len) { g_key_overrun = 0; return g_keys[g_keys_pos++]; }
    if (++g_key_overrun > 1000) {
        report("  FAIL キー台本の読みすぎ (端末ループが抜けていない)\n");
        die(2);
    }
    return g_keys_eof;
}

static int keys_left(void) { return g_keys_len - g_keys_pos; }

/* --- シリアル (rshell の EOT を見る窓) ---------------------------------- */
#define SER_LOG_CAP 4096

static u8  g_ser_log[SER_LOG_CAP];
static int g_ser_len;
static int g_ser_inited = 1;

static void ser_reset(void) { g_ser_len = 0; }

static int ser_count(u8 b)
{
    int i, n = 0;
    for (i = 0; i < g_ser_len; i++) if (g_ser_log[i] == b) n++;
    return n;
}

/* ---- ごく小さなヒープと疑似ファイル ------------------------------------ */

#define POOL_SIZE  (256 * 1024)

static char g_pool[POOL_SIZE];
static unsigned long g_pool_used;

#define FILE_MAX  16

static struct { const char *path; const char *body; } g_files[FILE_MAX];
static int g_file_count;
static int g_open_fd;
/* 読み位置は **fd ごと**。1 本しかないと push / recv のように読み口と書き口を
 * 同時に開く経路で、書き口を開いた拍子に読み位置が 0 へ戻って読み直しになる
 * (T24 の「読み切るまで回す」が無限ループに見える偽の赤になる)。 */
static int g_read_pos[FILE_MAX + 1];
static int g_open_leak;

/* 「どの綴りで開きにいったか」の窓。切り詰めた別のパスを作っていないかを
 * 見る (T19 / T20)。fd 1 / 2 以外への書き込み量は T24 の窓。 */
#define OPEN_LOG_CAP 2048
static char g_open_log[OPEN_LOG_CAP];
static int  g_open_log_len;
static int  g_open_calls;
static u32  g_data_written;
static int  g_data_writes;

static void files_reset(void)
{
    int i;
    g_file_count = 0;
    g_open_fd = 0;
    g_open_leak = 0;
    for (i = 0; i <= FILE_MAX; i++) g_read_pos[i] = 0;
    g_pool_used = 0;
    g_open_log_len = 0;
    g_open_log[0] = '\0';
    g_open_calls = 0;
    g_data_written = 0;
    g_data_writes = 0;
}

/* <want> という綴りで open されたか */
static int opened_path(const char *want)
{
    int i, j;
    int wl = 0;
    while (want[wl]) wl++;
    if (wl == 0) return 1;
    for (i = 0; i + wl <= g_open_log_len; i++) {
        for (j = 0; j < wl && g_open_log[i + j] == want[j]; j++) {}
        if (j == wl) return 1;
    }
    return 0;
}

/* 贋 fd は 10 番から配る。1 から配ると、読み口と書き口を同時に開く経路
 * (push / recv) で fd_out が 2 になり、**ファイルへの書き込みが stderr 扱い**
 * になって T24 の窓 (g_data_written) が動かない。 */
#define FD_BASE 10

static void file_add(const char *path, const char *body)
{
    if (g_file_count < FILE_MAX) {
        g_files[g_file_count].path = path;
        g_files[g_file_count].body = body;
        g_file_count++;
    }
}

/* ---- 差し替える KernelAPI の中身 --------------------------------------- */

static void __cdecl h_kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list ap;
    const char *p = fmt;
    (void)attr;
    __builtin_va_start(ap, fmt);
    while (*p) {
        if (*p == '%') fmt_run(&p, &ap);
        else out_byte(*p++);
    }
    __builtin_va_end(ap);
}

static void __cdecl h_shell_putchar(char c, u8 attr) { (void)attr; out_byte(c); }
static void __cdecl h_shell_print_utf8(const char *s, u8 attr)
{
    (void)attr;
    out_str(s);
}

/* T6: glob が 1 件ごとに確保する文字列 (dir + name + 1 = たかだか数十
 * バイト) **だけ** を N 回目で失敗させる窓。-1 = 無制限 (既定)。
 * 大きな確保 (スクリプトの読み込みバッファ、パイプの段バッファ) を
 * 区別しないと、行が glob に届く前に予算を使い切って別の理由で失敗し、
 * 「glob の確保失敗を見た」つもりの偽の緑になる。 */
#define GLOB_ALLOC_SIZE_MAX 64
static int g_glob_alloc_budget = -1;
static int g_glob_allocs;
static int g_frees;

static void *__cdecl h_mem_alloc(u32 size)
{
    char *p;
    unsigned long n = (unsigned long)size;

    if (size <= (u32)GLOB_ALLOC_SIZE_MAX) {
        if (g_glob_alloc_budget == 0) return (void *)0;
        if (g_glob_alloc_budget > 0) g_glob_alloc_budget--;
        g_glob_allocs++;
    }

    n = (n + 7UL) & ~7UL;
    if (g_pool_used + n > (unsigned long)POOL_SIZE) return (void *)0;
    p = g_pool + g_pool_used;
    g_pool_used += n;
    return (void *)p;
}

static void __cdecl h_mem_free(void *p) { (void)p; g_frees++; }

static int __cdecl h_sys_open(const char *path, int flags)
{
    int i;
    g_open_calls++;
    for (i = 0; path[i] && g_open_log_len < OPEN_LOG_CAP - 2; i++)
        g_open_log[g_open_log_len++] = path[i];
    if (g_open_log_len < OPEN_LOG_CAP - 1) g_open_log[g_open_log_len++] = '\n';
    g_open_log[g_open_log_len] = '\0';

    for (i = 0; i < g_file_count; i++) {
        if (strcmp(g_files[i].path, path) == 0) {
            g_open_fd = i + FD_BASE;
            g_read_pos[i] = 0;
            return i + FD_BASE;
        }
    }
    if (flags != KAPI_O_RDONLY) {
        file_add(path, "");
        g_open_fd = g_file_count - 1 + FD_BASE;
        g_read_pos[g_file_count - 1] = 0;
        return g_file_count - 1 + FD_BASE;
    }
    return -1;
}

/* fd 1 / 2 への書き込みは出力に残す。`echo` は kprintf ではなく
 * sys_write(1, ...) を使うので、ここを捨てると「その段が走ったか」の
 * 痕跡が取れない (パイプの段の検査が偽の GREEN になる)。
 * 本物はリダイレクト中ならファイルへ行くが、この試験が見るのは
 * 「段が実行されたか」なので宛先は区別しない (リダイレクト先を
 * 開いたかどうかは h_sys_redirect_fd の記録で別に見る)。 */
static int __cdecl h_sys_write(int fd, const void *buf, u32 size)
{
    const char *b = (const char *)buf;
    u32 i;
    if (fd == 1 || fd == 2) {
        for (i = 0; i < size; i++) out_byte(b[i]);
    } else {
        /* ファイルへの書き込み量。「読み切ってから送ったか」(T24) と
         * 「切れた設定を書き戻していないか」(T22) の窓。 */
        g_data_written += size;
        g_data_writes++;
    }
    return (int)size;
}

static int __cdecl h_sys_read(int fd, void *buf, u32 size)
{
    const char *src;
    char *dst = (char *)buf;
    int n = 0;
    int i = fd - FD_BASE;
    if (i < 0 || i >= g_file_count) return -1;
    src = g_files[i].body + g_read_pos[i];
    while (src[n] && (u32)n < size) { dst[n] = src[n]; n++; }
    g_read_pos[i] += n;
    return n;
}

static void __cdecl h_sys_close(int fd)
{
    int i = fd - FD_BASE;
    if (i >= 0 && i < FILE_MAX) g_read_pos[i] = 0;
    g_open_fd = 0;
}

/* sys_stat("/") / sys_stat("/etc") の st_dev (マウントの番号)。違えば /etc は別マウント */
static u32 g_dev_root = 1, g_dev_etc = 1;
static int __cdecl h_sys_stat(const char *path, OS32_Stat *st)
{
    int i;
    if (!st) return -1;
    for (i = 0; i < (int)sizeof(OS32_Stat); i++) ((u8 *)st)[i] = 0;
    if (strcmp(path, "/") == 0) { st->st_dev = g_dev_root; return 0; }
    if (strcmp(path, "/etc") == 0) { st->st_dev = g_dev_etc; return 0; }
    for (i = 0; i < g_file_count; i++) {
        if (strcmp(g_files[i].path, path) == 0) {
            st->st_size = (u32)strlen(g_files[i].body);
            return 0;
        }
    }
    return OS32_ERR_NOTFOUND;
}

/* ---- 贋ディレクトリ (glob の T6 / T7 に要る) ---------------------------- */
/*  g_ls_calls は「**照合を試みたか**」を見る唯一の窓。T7 (パターン /         */
/*  ディレクトリ部が上限超過) は sys_ls を 1 度も呼ばないこと。               */
#define DIRENT_MAX 24

static const char *g_dir_path;
static const char *g_dir_names[DIRENT_MAX];
static int g_dir_count;
static int g_ls_calls;

static void dir_reset(void)
{
    g_dir_path = (const char *)0;
    g_dir_count = 0;
    g_ls_calls = 0;
}

static void dir_set(const char *path) { g_dir_path = path; g_dir_count = 0; }

static void dir_add(const char *name)
{
    if (g_dir_count < DIRENT_MAX) g_dir_names[g_dir_count++] = name;
}

/* sys_ls: 贋ディレクトリを 1 階層ぶんコールバックへ流す (glob 用) */
static int __cdecl h_sys_ls(const char *path, void *cb, void *ctx)
{
    DirCallback f = (DirCallback)cb;
    DirEntry_Ext e;
    int i, k;

    g_ls_calls++;
    if (!g_dir_path || !f) return 0;
    if (strcmp(g_dir_path, path) != 0) return 0;

    for (i = 0; i < g_dir_count; i++) {
        for (k = 0; k < (int)sizeof(e.name); k++) e.name[k] = '\0';
        for (k = 0; g_dir_names[i][k] && k + 1 < (int)sizeof(e.name); k++)
            e.name[k] = g_dir_names[i][k];
        e.size = 1;
        e.type = OS32_FILE_TYPE_FILE;
        f(&e, ctx);
    }
    return g_dir_count;
}

static int __cdecl h_sys_isatty(int fd) { (void)fd; return 1; }
static const char *__cdecl h_sys_getcwd(void) { return "/cwd"; }
static int __cdecl h_sys_chdir(const char *p) { (void)p; return 0; }
static int __cdecl h_sys_mkdir(const char *p) { (void)p; return 0; }
static int __cdecl h_sys_unlink(const char *p) { (void)p; return 0; }
/* リダイレクトの記録。`> file` は本物では O_TRUNC で開くので、**この呼び出し
 * が起きたこと自体**が「リダイレクト先が空で上書きされた」ことを意味する。
 * 断った段の後ろの段が走らないことを、痕跡の側から見るための窓。 */
#define REDIR_LOG_CAP 512
static char g_redir_log[REDIR_LOG_CAP];
static int  g_redir_log_len;
static int  g_redir_count;

static void redir_log_reset(void)
{
    g_redir_log_len = 0;
    g_redir_log[0] = '\0';
    g_redir_count = 0;
}

/* リダイレクト先として <want> が開かれたか */
static int redir_opened(const char *want)
{
    int i, j;
    int wl = 0;
    while (want[wl]) wl++;
    if (wl == 0) return 1;
    for (i = 0; i + wl <= g_redir_log_len; i++) {
        for (j = 0; j < wl && g_redir_log[i + j] == want[j]; j++) {}
        if (j == wl) return 1;
    }
    return 0;
}

static int __cdecl h_sys_redirect_fd(int fd, const char *p, int mode)
{
    (void)fd; (void)mode;
    g_redir_count++;
    while (p && *p && g_redir_log_len < REDIR_LOG_CAP - 2) {
        g_redir_log[g_redir_log_len++] = *p++;
    }
    if (g_redir_log_len < REDIR_LOG_CAP - 1) g_redir_log[g_redir_log_len++] = '\n';
    g_redir_log[g_redir_log_len] = '\0';
    return 0;
}
static int __cdecl h_sys_redirect_fd_buf(int fd, u8 *b, u32 cap, u32 len)
{
    (void)fd; (void)b; (void)cap; (void)len;
    return 0;
}
static u32 __cdecl h_sys_redirect_get_buf_len(int fd) { (void)fd; return 0; }
static void __cdecl h_sys_reset_redirect(int fd) { (void)fd; }
/* 1 呼び出しごとに 1 tick 進む。止まったままだと rshell / filer の
 * 「次の tick まで待つ」ループが抜けない (試験がハングする)。
 *
 * tick_freeze(1) の間だけ **止める**。script_exec の譲りは「同じ tick の
 * 中では 1 回だけ」なので、止めた tick でないとその間引きを見られない
 * (窓 30h)。止めるのはその場の試験だけ — fresh() が必ず戻す。 */
static u32 g_tick;
static int g_tick_frozen;
static u32 __cdecl h_get_tick(void)
{
    if (g_tick_frozen) return g_tick;
    return g_tick++;
}
static void tick_freeze(int on) { g_tick_frozen = on; }
/* 贋 tick を 1 つ進める。script_exec の譲りは「同じ tick では 1 回だけ」で、
 * その記憶 (cmd_script.c の static) は fresh() では消せない。tick が動いて
 * いない状態で測ると**正しく間引かれて** 0 回になるので、間引きを見たいとき
 * 以外は測る前にここで 1 つ進める。 */
static void tick_step(void) { g_tick++; }

/* 起動の記録。切り詰めた行で子が起きたかどうかを見る唯一の窓。 */
#define LAUNCH_LOG_CAP 512
static char g_launch_last[LAUNCH_LOG_CAP];
static int  g_launch_count;

static void launch_log_reset(void)
{
    g_launch_count = 0;
    g_launch_last[0] = '\0';
}

/*  この試験が見たいのは「**子が起きたか** (launch_req を呼んだか) と、
 *  その綴り」で、起動の成否そのものではない。
 *
 *  2026-09-16 (票 TASK_EXIT_STATUS R1b) まではここで OS32_ERR_INVAL
 *  (= GUI 外) を返していた。新しい契約では GUI 外は「起こせなかった」=
 *  126 で **PATH 走査が止まる** ので、候補を全部試す形の反例 (18e) や
 *  「解決しにいったか」を "command not found" で見る窓 (ran()) が
 *  成り立たなくなる。そこで **要求は受け付けて、子が見つからなかった**
 *  (LAUNCH_ST_FAILED + EXEC_ERR_NOT_FOUND) という返しに替える。
 *  launch_req の呼び出し回数・綴りの窓は 1 バイトも変わらない。 */
static i32 __cdecl h_launch_req(const char *cmdline)
{
    int i;
    for (i = 0; cmdline[i] && i < LAUNCH_LOG_CAP - 1; i++)
        g_launch_last[i] = cmdline[i];
    g_launch_last[i] = '\0';
    g_launch_count++;
    return 1;                   /* token (受け付けた) */
}
static i32 __cdecl h_launch_poll(i32 token, i32 *status)
{
    (void)token;
    /* 子は見つからなかった = 次の候補へ進む側 (sh_launch は何も印字しない) */
    if (status) *status = LAUNCH_ST_FAILED | (i32)(-EXEC_ERR_NOT_FOUND);
    return 0;
}
/* 譲り (KAPI v49) の回数。script_exec が行ごとに WM へ譲っているかを見る
 * 唯一の窓 — 本物では park / resume の往復になる。
 *
 * **sh_launch も譲る** ことに注意 (要求表の待ちループが sys_yield を回す)。
 * だから外部コマンドを含む行では、この数に「起動の待ち」ぶんが混ざる。
 * 窓 30 はそれを避けるために内蔵コマンドだけで組み、外部を含む形は
 * 30i で **差分** (同じ行を対話で 1 回 / スクリプトで 1 回) で見る。 */
static int g_yield_count;
static i32 __cdecl h_sys_yield(void) { g_yield_count++; return 0; }
static int yield_count(void) { return g_yield_count; }

/* --- 監視キュー (script_exec の ESC 監視が引く口) ------------------------
 *
 *  行編集の台本 (keys_*) とは**別の窓**。keys_* は尽きたら ESC を返し続ける
 *  ので、そこへ相乗りさせると script_exec が毎行 ESC で落ちる。
 *
 *  ここは本物のキューと同じに振る舞う: kbd_peekkey は**先頭を動かさず**返し、
 *  kbd_trygetkey は**取り出す**。既定は空 (= 打鍵なし) なので、これを使わない
 *  試験の見え方は今までと 1 行も変わらない (どちらも -1)。
 *
 *  「食った / 食わなかった」の窓は wq_len() / wq_at() / wq_takes()。 */
#define WQ_CAP 16
static int g_wq[WQ_CAP];
static int g_wq_head;
static int g_wq_len;
static int g_wq_takes;          /* 取り出された回数 (食った数) */

static void wq_reset(void)
{
    g_wq_head = 0;
    g_wq_len = 0;
    g_wq_takes = 0;
}

static void wq_push(int k)
{
    if (g_wq_len < WQ_CAP) g_wq[(g_wq_head + g_wq_len++) % WQ_CAP] = k;
}

static int wq_len(void)   { return g_wq_len; }
static int wq_takes(void) { return g_wq_takes; }

/* 先頭から i 番目。無ければ -1。 */
static int wq_at(int i)
{
    if (i < 0 || i >= g_wq_len) return -1;
    return g_wq[(g_wq_head + i) % WQ_CAP];
}

/* 覗くだけ — キューは 1 つも動かない (KAPI v54)。 */
static int __cdecl h_kbd_peekkey(void)
{
    if (g_wq_len == 0) return -1;
    return g_wq[g_wq_head];
}

/* 取り出す。filer のキーリピート掃除もこれを回すが、空なら -1 で空回りする。 */
static int __cdecl h_kbd_trygetkey(void)
{
    int k;
    if (g_wq_len == 0) return -1;
    k = g_wq[g_wq_head];
    g_wq_head = (g_wq_head + 1) % WQ_CAP;
    g_wq_len--;
    g_wq_takes++;
    return k;
}

/* 行入力・rshell・filer が引くキー源。台本 (keys_*) を 1 つずつ返す。 */
static int __cdecl h_kbd_getchar(void)    { return key_next(); }
static int __cdecl h_kbd_getkey(void)     { return key_next(); }
static int __cdecl h_ime_getkey(void)     { return key_next(); }
/* rshell は「来ていない = -1」で待つ。台本が尽きたら ESC を返して抜けさせる。 */
static int __cdecl h_kbd_trygetchar(void)
{
    if (keys_left() > 0) return key_next();
    return 0x1B;
}

/* --- シリアル (rshell) -------------------------------------------------- */
static void __cdecl h_serial_init(u32 baud) { (void)baud; g_ser_inited = 1; }
static int  __cdecl h_serial_is_initialized(void) { return g_ser_inited; }
static int __cdecl h_serial_putchar(u8 c)
{
    if (g_ser_len < SER_LOG_CAP) g_ser_log[g_ser_len++] = c;
    return 0;
}
static void __cdecl h_serial_puts(const char *s)
{
    while (*s) h_serial_putchar((u8)*s++);
}
static int  __cdecl h_serial_trygetchar(void) { return -1; }
/* KAPI v57: rshell のローカル読み口。ハーネスの台本は同じ列から取る。 */
static int  __cdecl h_kbd_trygetchar_local(void) { return h_kbd_trygetchar(); }
/* PCI は「無い」で答える (票 TASK_LAN_82557 L-A)。ホストにも NP21/W にも
 * PCI は無いので、lspci が通る道は「no PCI」のほう。NULL のままにすると
 * 将来 lspci を叩く試験を足した日に静かに落ちる。 */
static int  __cdecl h_pci_count(void) { return 0; }
static int  __cdecl h_pci_get(u32 idx, void *out) { (void)idx; (void)out; return -1; }
/* KAPI v60: 結線の診断。PCI が 0 件なので記録も無い (票 TASK_HAL_WIRING §1-4)。 */
static int  __cdecl h_pci_bind_info(u32 idx, void *out)
{ (void)idx; (void)out; return -1; }
static u32  __cdecl h_pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{ (void)bus; (void)dev; (void)fn; (void)reg; return 0xFFFFFFFFUL; }
/* KAPI v61: CS4231 の再生 (票 TASK_PCM_CS4231)。ホストに装置は無いので
 * open は NOSYS。**NULL のままにしない** — 将来 sh から叩く日に静かに落ちる。 */
static int  __cdecl h_pcm_open(u32 rate) { (void)rate; return OS32_ERR_NOSYS; }
static int  __cdecl h_pcm_write(const void *buf, u32 bytes)
{ (void)buf; (void)bytes; return OS32_ERR_INVAL; }
static int  __cdecl h_pcm_status(u32 *freeb, u32 *cnt)
{ (void)freeb; (void)cnt; return OS32_ERR_INVAL; }
static int  __cdecl h_pcm_close(void) { return OS32_ERR_INVAL; }
static int  __cdecl h_pcm_set_volume(u32 percent)
{ (void)percent; return OS32_ERR_INVAL; }
static int  __cdecl h_serial_init_vfast(u32 baud) { (void)baud; return -1; }
static int  __cdecl h_serial_get_status(u32 *mode, u32 *baud, u32 *fifo)
{ if (mode) *mode = 0; if (baud) *baud = 9600; if (fifo) *fifo = 0; return 0; }
static int rshell_active_calls;
static void __cdecl h_rshell_set_active(int on) { (void)on; rshell_active_calls++; }
static void __cdecl h_buz_off(void) {}
static void __cdecl h_sys_halt(void) {}
static int  __cdecl h_sys_mount(const char *mp, const char *dev, const char *fs)
{ (void)mp; (void)dev; (void)fs; return 0; }

static int  __cdecl h_console_get_cursor_x(void)   { return 0; }
static int  __cdecl h_console_get_cursor_y(void)   { return 0; }
static void __cdecl h_console_set_cursor(int x, int y) { (void)x; (void)y; }
static void __cdecl h_tvram_clear(void) {}

/* ルートのデバイス名 (vfs_devname("/"))。既定は HDD。FD のルート名は
 * test_sh_truncation.py が kernel/kernel.c から読んで -DFD_ROOT_DEV で渡す
 * (試験の側で "fd0" を決め打ちしない) */
#ifndef FD_ROOT_DEV
#error "FD_ROOT_DEV (kernel.c の FD ルートのデバイス名) を -D で渡すこと"
#endif
static const char *g_root_dev = "hd0";
static const char * __cdecl h_vfs_devname(const char *prefix)
{
    return (prefix[0] == '/' && prefix[1] == '\0') ? g_root_dev : "";
}

static KernelAPI g_fake;
KernelAPI *g_api;

static void build_api(void)
{
    u32 i;
    u8 *raw = (u8 *)&g_fake;
    for (i = 0; i < (u32)sizeof(g_fake); i++) raw[i] = 0;
    g_fake.kprintf = h_kprintf;
    g_fake.shell_putchar = h_shell_putchar;
    g_fake.shell_print_utf8 = h_shell_print_utf8;
    g_fake.mem_alloc = h_mem_alloc;
    g_fake.mem_free = h_mem_free;
    g_fake.sys_open = h_sys_open;
    g_fake.sys_read = h_sys_read;
    g_fake.sys_write = h_sys_write;
    g_fake.sys_ls = h_sys_ls;
    g_fake.sys_stat = h_sys_stat;
    g_fake.sys_close = h_sys_close;
    g_fake.sys_isatty = h_sys_isatty;
    g_fake.sys_getcwd = h_sys_getcwd;
    g_fake.sys_chdir = h_sys_chdir;
    g_fake.sys_mkdir = h_sys_mkdir;
    g_fake.sys_unlink = h_sys_unlink;
    g_fake.sys_redirect_fd = h_sys_redirect_fd;
    g_fake.sys_redirect_fd_buf = h_sys_redirect_fd_buf;
    g_fake.sys_redirect_get_buf_len = h_sys_redirect_get_buf_len;
    g_fake.sys_reset_redirect = h_sys_reset_redirect;
    g_fake.get_tick = h_get_tick;
    g_fake.launch_req = h_launch_req;
    g_fake.launch_poll = h_launch_poll;
    g_fake.sys_yield = h_sys_yield;
    g_fake.kbd_trygetkey = h_kbd_trygetkey;
    g_fake.kbd_peekkey = h_kbd_peekkey;
    g_fake.kbd_getchar = h_kbd_getchar;
    g_fake.kbd_getkey = h_kbd_getkey;
    g_fake.ime_getkey = h_ime_getkey;
    g_fake.kbd_trygetchar = h_kbd_trygetchar;
    g_fake.serial_init = h_serial_init;
    g_fake.serial_is_initialized = h_serial_is_initialized;
    g_fake.serial_putchar = h_serial_putchar;
    g_fake.serial_puts = h_serial_puts;
    g_fake.serial_trygetchar = h_serial_trygetchar;
    g_fake.kbd_trygetchar_local = h_kbd_trygetchar_local;
    g_fake.pci_count = h_pci_count;
    g_fake.pci_get = h_pci_get;
    g_fake.pci_bind_info = h_pci_bind_info;
    g_fake.pci_cfg_read32 = h_pci_cfg_read32;
    g_fake.pcm_open = h_pcm_open;
    g_fake.pcm_write = h_pcm_write;
    g_fake.pcm_status = h_pcm_status;
    g_fake.pcm_close = h_pcm_close;
    g_fake.pcm_set_volume = h_pcm_set_volume;
    g_fake.serial_init_vfast = h_serial_init_vfast;
    g_fake.serial_get_status = h_serial_get_status;
    g_fake.rshell_set_active = h_rshell_set_active;
    g_fake.buz_off = h_buz_off;
    g_fake.sys_halt = h_sys_halt;
    g_fake.sys_mount = h_sys_mount;
    g_fake.console_get_cursor_x = h_console_get_cursor_x;
    g_fake.console_get_cursor_y = h_console_get_cursor_y;
    g_fake.console_set_cursor = h_console_set_cursor;
    g_fake.tvram_clear = h_tvram_clear;
    g_fake.vfs_devname = h_vfs_devname;
    g_api = &g_fake;
}

/* ---- sdk/crt/help.c の代わり (man ページは読まない) -------------------- */
int os32_help_show(const char *name)   { (void)name; return -1; }
int os32_help_exists(const char *name) { (void)name; return 0; }

/* ---- 段 4 で取り込んだ入口 (ui.c / rshell.c / cmd_filer.c) -------------
 *
 *  段 3 まではこの 3 本を試験に入れておらず、「踏んでいない」と記録して
 *  いた。段 4 の T10 / T14 / T15 / T16 / T19 / T20 / T21 はこの 3 本の
 *  中にしか無いので、**実物をそのまま #include** して通す。
 *
 *  端末と GFX を握る部分だけ差し替える:
 *    - キー入力  → 台本 (keys_set) を 1 つずつ返す贋 kbd_*
 *    - シリアル  → 送信バイトを記録する贋 serial_* (EOT を見るため)
 *    - fldraw_*  → TVRAM を直接叩くので、ここで空実装を置く
 *                  (filer_draw.c は取り込まない)
 *  ソースそのものは 1 行も写していない。
 * ---------------------------------------------------------------------- */

/* --- filer の描画 (TVRAM を直接叩くので取り込まない) -------------------- */
#include "filer_draw.h"

static char g_popup[128];
static int  g_popup_count;

void fldraw_init(KernelAPI *api) { (void)api; }
void fldraw_clear_line(int y, u8 attr) { (void)y; (void)attr; }
void fldraw_str(int x, int y, const char *s, int max_len, u8 attr)
{ (void)x; (void)y; (void)s; (void)max_len; (void)attr; }
void fldraw_number(int x, int y, u32 val, u8 attr)
{ (void)x; (void)y; (void)val; (void)attr; }
void fldraw_size_str(int x, int y, u32 size, u8 attr)
{ (void)x; (void)y; (void)size; (void)attr; }
void fldraw_header(const FL_State *st) { (void)st; }
void fldraw_entry(int col_x, int y, const FL_Entry *e, int selected)
{ (void)col_x; (void)y; (void)e; (void)selected; }
void fldraw_entry_at(const FL_State *st, int idx, int selected)
{ (void)st; (void)idx; (void)selected; }
void fldraw_list(const FL_State *st) { (void)st; }
void fldraw_footer(const FL_State *st) { (void)st; }
void fldraw_help(const FL_State *st) { (void)st; }
void fldraw_all(const FL_State *st) { (void)st; }
void fldraw_cursor_update(const FL_State *st, int old_cursor)
{ (void)st; (void)old_cursor; }
void fldraw_popup_message(const char *msg, u8 attr)
{
    int i = 0;
    (void)attr;
    while (msg[i] && i < (int)sizeof(g_popup) - 1) { g_popup[i] = msg[i]; i++; }
    g_popup[i] = '\0';
    g_popup_count++;
}

/* --- libos32save の代わり (rshell.c の hotdeploy が引くだけ) ------------ */
u32 save_crc32(const void *data, u32 len)
{
    (void)data;
    return len;
}

/* ---- 実物のシェル ------------------------------------------------------ */

#include "../../userland/shell/main.c"
#include "../../userland/shell/cmd_base.c"
#include "../../userland/shell/cmd_dir.c"
#include "../../userland/shell/cmd_env.c"
#include "../../userland/shell/cmd_fs_shared.c"
#include "../../userland/shell/cmd_file.c"
#include "../../userland/shell/cmd_mnt.c"
#include "../../userland/shell/cmd_script.c"
#include "../../userland/shell/cmd_sys.c"
#include "../../userland/shell/kbd_watch.c"   /* kbdstat -w の行 (票 TASK_KBD_NAV §3) */
/* cmd_pci.c (lspci / pcidump、票 TASK_LAN_82557 L-A)。main.c が
 * shell_cmd_pci_init() を呼ぶので**取り込まないとリンクが通らない**。
 * 復号の実体 drivers/pci_decode.c は実ビルドでも同じ 1 本をリンクする
 * (build/programs.mk の PCI_DECODE_USER_OBJ)。 */
#include "../../userland/shell/cmd_pci.c"
#include "../../drivers/pci_decode.c"
#include "../../userland/shell/pci_verbose.c"   /* lspci -v の行づくり */
/* cmd_hdprep.c (hdprep、票 TASK_HDD_INSTALL 段 1) も同じ理由 — main.c が
 * shell_cmd_hdprep_init() を呼ぶ。判定の純粋部と区画表の共有部も実ビルドと同じ 1 本
 * (build/programs.mk の PC98PT_USER_OBJ)。 */
#include "../../userland/shell/cmd_hdprep.c"
#include "../../userland/shell/hdprep_plan.c"
#include "../../drivers/pc98pt.c"
#include "../../userland/shell/cmd_filer.c"
#include "../../userland/shell/rshell.c"
#include "../../userland/shell/serial_watchdog.c"   /* rshell.c の番犬 (往復 2) */
#include "../../userland/shell/ui.c"

/* main.c の main() と同じ順で登録表を作る (表そのものは実物) */
static void sh_boot(void)
{
    env_init();
    shell_cmd_base_init();
    shell_cmd_file_init();
    shell_cmd_dir_init();
    shell_cmd_mnt_init();
    shell_cmd_sys_init();
    shell_cmd_env_init();
    shell_cmd_script_init();
    shell_cmd_filer_init();
    /* 常駐版だけが登録する rshell 系も、この試験では表に載せて
     * execute_command 経由でも踏めるようにしておく (main.c の
     * #ifndef SHELL_AS_APP と同じ並び)。 */
    shell_rshell_init();
}

/* ---- 試験の道具 -------------------------------------------------------- */

/* 長さ n の `a` の並びを作り、最後の 1 文字だけ tail に差し替える。
 * 先頭 255 文字が同じで 256 文字目が違う 2 本を作るのに使う。 */
static void fill_run(char *dst, int n, char tail)
{
    int i;
    for (i = 0; i < n; i++) dst[i] = 'a';
    if (n > 0) dst[n - 1] = tail;
    dst[n] = '\0';
}

static char g_line[CMD_BUF_SIZE];

static void line_reset(void) { g_line[0] = '\0'; }

static void line_add(const char *s)
{
    int n = 0;
    while (g_line[n]) n++;
    while (*s && n < (int)sizeof(g_line) - 1) g_line[n++] = *s++;
    g_line[n] = '\0';
}

/* スクリプト本文の組み立て (贋 FS は本文のポインタを持つだけなので静的領域) */
#define SBUF_COUNT  3
#define SBUF_SIZE   1024

static char g_sbuf[SBUF_COUNT][SBUF_SIZE];
static int  g_sn[SBUF_COUNT];
static int  g_scur;

static void s_begin(int i) { g_scur = i; g_sn[i] = 0; g_sbuf[i][0] = '\0'; }

static void s_add(const char *t)
{
    int n = g_sn[g_scur];
    while (*t && n < SBUF_SIZE - 1) g_sbuf[g_scur][n++] = *t++;
    g_sbuf[g_scur][n] = '\0';
    g_sn[g_scur] = n;
}

/* `a` を count 個 */
static void s_run(int count)
{
    int n = g_sn[g_scur];
    int i;
    for (i = 0; i < count && n < SBUF_SIZE - 1; i++) g_sbuf[g_scur][n++] = 'a';
    g_sbuf[g_scur][n] = '\0';
    g_sn[g_scur] = n;
}

static const char *s_body(int i) { return g_sbuf[i]; }

/* 未知のコマンド名 <name> が実際に解決されにいったか。
 * run_cmd_internal は最後に "<name>: command not found" を出す。 */
static int ran(const char *name)
{
    char needle[64];
    int i = 0;
    const char *t = ": command not found";
    int j = 0;

    while (name[i] && i < 40) { needle[i] = name[i]; i++; }
    while (t[j]) needle[i++] = t[j++];
    needle[i] = '\0';
    return out_has(needle);
}

/* 断りの 1 行が出ているか (何が上限を超えたか + 上限) */
static int refused_msg(const char *what)
{
    return out_has(what) && out_has("too long (max ");
}

static void fresh(void)
{
    files_reset();
    out_reset();
    launch_log_reset();
    redir_log_reset();
    dir_reset();
    keys_reset();
    wq_reset();
    ser_reset();
    g_popup_count = 0;
    g_popup[0] = '\0';
    g_root_dev = "hd0";
    g_dev_root = 1;
    g_dev_etc = 1;
    /* shell_run の試験が `exit` で抜けた後、この印が立ったままだと
     * 以降の script_exec が 1 行目で break して**何も走らない**
     * (窓 27e が実際にそれを捕まえた)。 */
    sh_exit_flag = 0;
    sh_refused_flag = 0;
    g_glob_alloc_budget = -1;
    g_glob_allocs = 0;
    g_frees = 0;
    g_yield_count = 0;
    g_tick_frozen = 0;
}

/* 同じ文字列を n 回続けて足す (クォート再付与ぶんの反例を組むのに使う) */
static void line_add_rep(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++) line_add(s);
}

/* 1 文字を n 個足す */
static void line_add_run(char c, int n)
{
    char one[2];
    int i;
    one[0] = c;
    one[1] = '\0';
    for (i = 0; i < n; i++) line_add(one);
}

/* T13 用: CMD_BUF_SIZE を **超える** 行。g_line (= CMD_BUF_SIZE) には
 * 4095 バイトまでしか入らないので別に持つ。 */
static char g_big[CMD_BUF_SIZE + 64];

/* prefix + 'a' * pad (合計の長さを返す) */
static int big_line(const char *prefix, int pad)
{
    int n = 0;
    int i;

    while (prefix[n] && n < (int)sizeof(g_big) - 1) { g_big[n] = prefix[n]; n++; }
    for (i = 0; i < pad && n < (int)sizeof(g_big) - 1; i++) g_big[n++] = 'a';
    g_big[n] = '\0';
    return n;
}

/* ========================================================================
 *  1. U1 — `if` の両辺がクォート除去後 256 文字以上で先頭 255 文字が同じ
 *
 *  段 1 はここを「今は条件が真になって右辺が走る」と記録していた
 *  ([EXPECTED_TO_CHANGE])。段 2 で票 §4 U1 の形へ反転させた:
 *    - コマンドを実行しない (`==` も `!=` も)
 *    - 上限超過を報告する
 *    - スクリプト中なら後続行も実行しない (§2-1)
 * ======================================================================== */
static void case_if_compare_refuses(void)
{
    char a[600], b[600];

    report("1 U1: if の両辺が 256 文字以上・先頭 255 文字が同じ\n");

    /* 256 文字。先頭 255 文字は同じで 256 文字目だけ違う */
    fill_run(a, 256, 'x');
    fill_run(b, 256, 'y');
    check(strcmp(a, b) != 0, "1a 反例そのものは別の文字列 (256 文字目が違う)");
    check(strncmp(a, b, 255) == 0, "1b 先頭 255 文字は同じ");

    /* `==` — 切り詰めて比べれば真になる反例。断って実行しない。 */
    fresh();
    line_reset();
    line_add("if "); line_add(a); line_add(" == "); line_add(b);
    line_add(" markif");
    execute_command(g_line);
    check(!ran("markif") && g_launch_count == 0,
          "1c == : 右辺のコマンドを実行しない");
    check(refused_msg("if: left value"),
          "1d == : 何が上限を超えたか + 上限を 1 行で報告する");

    /* `!=` — 走らない点は前と同じだが、理由がメッセージで分かること */
    fresh();
    line_reset();
    line_add("if "); line_add(a); line_add(" != "); line_add(b);
    line_add(" markif");
    execute_command(g_line);
    check(!ran("markif") && g_launch_count == 0,
          "1e != : 右辺のコマンドを実行しない");
    check(refused_msg("if: left value"), "1f != : 同じく報告する");

    /* 右辺だけが溢れる場合は「右辺」と言う */
    fresh();
    line_reset();
    line_add("if short == "); line_add(b); line_add(" markif");
    execute_command(g_line);
    check(!ran("markif"), "1g 右辺だけ溢れても実行しない");
    check(refused_msg("if: right value"), "1h 溢れた側を名指しする");

    /* スクリプト中: 後続行も実行しない (§2-1 の反例そのもの) */
    fresh();
    s_begin(0);
    s_add("set A=");  s_run(240);  s_add("\n");
    s_add("if ${A}${A}x == ${A}${A}y markif\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("markif"), "1i スクリプト: if の右辺を実行しない");
    check(!ran("marknext"), "1j スクリプト: 後続行も実行しない (§2-1)");
    check(out_has("script: aborted"), "1k スクリプト: 打ち切ったことを言う");
}

/* ========================================================================
 *  2. 足場が本物であることの確認 (段 1 から据え置き)
 * ======================================================================== */
static void case_registry_is_real(void)
{
    int count;
    const ShellCmd *cmds;
    int i, seen_if = 0, seen_set = 0, seen_source = 0, seen_time = 0;

    report("2 登録表と execute_command は実物 (スタブではない)\n");

    cmds = shell_get_cmds(&count);
    for (i = 0; i < count; i++) {
        if (str_eq(cmds[i].name, "if"))     seen_if = 1;
        if (str_eq(cmds[i].name, "set"))    seen_set = 1;
        if (str_eq(cmds[i].name, "source")) seen_source = 1;
        if (str_eq(cmds[i].name, "time"))   seen_time = 1;
    }
    check(count > 20,  "2a 登録表に実物の件数が入っている");
    check(seen_if,     "2b if が実物の表にある");
    check(seen_set,    "2c set が実物の表にある");
    check(seen_source, "2d source が実物の表にある");
    check(seen_time,   "2e time が実物の表にある");

    fresh();
    execute_command("if abc == abd markif");
    check(!ran("markif") && !out_has("too long"),
          "2f 255 文字以下なら `==` は正しく偽になる (断りも出さない)");

    fresh();
    execute_command("if abc == abc markif");
    check(ran("markif"), "2g 255 文字以下で一致すれば右辺が走る");
}

/* ========================================================================
 *  3. U2 — 境界。255 文字ちょうどは通る / 256 文字は断る。
 *     長さは**クォート除去後**で数える (sh_args.inc が既に落としている)。
 * ======================================================================== */
static void case_boundary(void)
{
    char v255[600], v256[600], w255[600];

    report("3 U2: 255 は通る / 256 は断る (クォート除去後で数える)\n");

    fill_run(v255, 255, 'z');
    fill_run(w255, 255, 'z');       /* v255 と同じ内容 */
    fill_run(v256, 256, 'z');

    fresh();
    line_reset();
    line_add("if "); line_add(v255); line_add(" == "); line_add(w255);
    line_add(" markif");
    execute_command(g_line);
    check(ran("markif"), "3a 255 文字ちょうど同士は比較でき、真なら走る");
    check(!out_has("too long"), "3b 255 文字では断らない");

    fresh();
    line_reset();
    line_add("if "); line_add(v255); line_add(" != "); line_add(w255);
    line_add(" markif");
    execute_command(g_line);
    check(!ran("markif") && !out_has("too long"),
          "3c 255 文字ちょうどの != は正しく偽 (断りではない)");

    fresh();
    line_reset();
    line_add("if "); line_add(v256); line_add(" == "); line_add(v256);
    line_add(" markif");
    execute_command(g_line);
    check(!ran("markif") && refused_msg("if: left value"),
          "3d 256 文字は**中身が同じでも**断る");

    /* クォート付き: 生では 257 バイトだが、除去後は 255 なので通る。
     * 長さを「除去前」で数えると、ここが誤って断られる。 */
    fresh();
    line_reset();
    line_add("if \""); line_add(v255); line_add("\" == \""); line_add(w255);
    line_add("\" markif");
    execute_command(g_line);
    check(ran("markif"), "3e クォート込み 257 バイトでも除去後 255 なら通る");
    check(!out_has("too long"), "3f クォートの分を数に入れない");

    /* 除去後 256: クォート込み 258 バイト */
    fresh();
    line_reset();
    line_add("if \""); line_add(v256); line_add("\" == \""); line_add(v256);
    line_add("\" markif");
    execute_command(g_line);
    check(!ran("markif") && out_has("too long"),
          "3g 除去後 256 なら (クォートが付いていても) 断る");
}

/* ========================================================================
 *  4. 経路 1 — `if` / `time` 経由の入れ子 execute_command
 *
 *  どちらも組み立てた行を execute_command へ渡す。内側で断ったことが
 *  外の script_exec まで届くこと (取りこぼしが無いこと) を見る。
 * ======================================================================== */
static void case_nested_execute_command(void)
{
    report("4 経路 1: if / time 経由の入れ子 execute_command\n");

    /* if の右辺がさらに if。内側の if が断る。
     * 片側だけ 300 文字にして、外側の join_args (4096) には収める。 */
    fresh();
    s_begin(0);
    s_add("set B=");  s_run(150);  s_add("\n");
    s_add("if a == a if ${B}${B} == b markinner\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("markinner"), "4a if 入れ子: 内側の右辺を実行しない");
    check(refused_msg("if: left value"), "4b if 入れ子: 断りが出る");
    check(!ran("marknext"), "4c if 入れ子: 外のスクリプトも打ち切る");

    /* time が組み立てた行。cmd_buf は 510 なので 300 文字なら溢れない。 */
    fresh();
    s_begin(0);
    s_add("set B=");  s_run(150);  s_add("\n");
    s_add("time if ${B}${B} == b markinner\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("markinner"), "4d time 経由: 内側の右辺を実行しない");
    check(refused_msg("if: left value"), "4e time 経由: 断りが出る");
    check(!ran("marknext"), "4f time 経由: 外のスクリプトも打ち切る");

    /* 誤発火の裏: 入れ子が**通った**ら印は立たない */
    fresh();
    s_begin(0);
    s_add("if a == a time mk1\n");
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1"), "4g 通る入れ子は今までどおり走る");
    check(ran("mk2"), "4h 通る入れ子の後も打ち切らない (誤発火なし)");
    check(sh_refused_flag == 0, "4i 通った行は印を残さない");

    /* 入口の掃除は「いちばん外側だけ」を直に見る。
     *
     * 段 2 のときは「断った段の後ろの段が time なら取りこぼす」(6g) が
     * この規則の唯一の証人だったが、PM 決裁で断った後の段を走らせなく
     * なったので、その経路からは見えなくなった。規則自体は shell.h の
     * 契約なので、入れ子の深さを直に作って押さえる
     * (`if` / `time` が execute_command を呼ぶときと同じ状態)。 */
    fresh();
    g_exec_depth++;                 /* if / time の中にいるときと同じ */
    sh_refuse_mark();
    execute_command("mk1");
    g_exec_depth--;
    check(ran("mk1"), "4j 入れ子の execute_command は今までどおり走る");
    check(sh_refused_flag == 1,
          "4k 入れ子の入口では印を消さない (内側の断りを外へ届ける)");

    /* いちばん外側 (深さ 0) の入口では消す */
    execute_command("mk2");
    check(sh_refused_flag == 0,
          "4l いちばん外側の入口では印を消す");
}

/* ========================================================================
 *  5. 経路 2 — 入れ子の source (script_source_file の戻り値で親に伝える)
 * ======================================================================== */
static void case_nested_source(void)
{
    report("5 経路 2: 入れ子の source\n");

    fresh();
    s_begin(1);                      /* 子 */
    s_add("set A=");  s_run(240);  s_add("\n");
    s_add("if ${A}${A}x == ${A}${A}y markif\n");
    s_add("markchild\n");
    s_begin(0);                      /* 親 */
    s_add("source /child.sh\n");
    s_add("markparent\n");
    file_add("/parent.sh", s_body(0));
    file_add("/child.sh", s_body(1));
    execute_command("source /parent.sh");
    check(!ran("markif"),     "5a 子: if の右辺を実行しない");
    check(!ran("markchild"),  "5b 子: 後続行も実行しない");
    check(!ran("markparent"), "5c 親: 子が断ったら親も打ち切る (戻り値で伝播)");

    /* 直接 script_source_file を呼んだときの戻り値 */
    fresh();
    s_begin(1);
    s_add("set A=");  s_run(240);  s_add("\n");
    s_add("if ${A}${A}x == ${A}${A}y markif\n");
    file_add("/child.sh", s_body(1));
    check(script_source_file("/child.sh") == SCRIPT_ERR_REFUSED,
          "5d script_source_file は断りを SCRIPT_ERR_REFUSED で返す");

    /* 誤発火の裏: 通る子 source は親を止めない */
    fresh();
    s_begin(1);
    s_add("mk1\n");
    s_begin(0);
    s_add("source /child.sh\n");
    s_add("markparent\n");
    file_add("/parent.sh", s_body(0));
    file_add("/child.sh", s_body(1));
    execute_command("source /parent.sh");
    check(ran("mk1") && ran("markparent"),
          "5e 通る子の後も親は続く (誤発火なし)");
    /* 票 TASK_EXIT_STATUS §2-5-1 で契約が変わった (2026-09-16): 断らなかった
     * source は **最後に実行した行の値** を返す (以前は一律 0)。この子の
     * 最後の行は未知のコマンド `mk1` なので 127。断りの -2 ではないことが
     * ここで見たいこと。 */
    check(script_source_file("/child.sh") == SH_STATUS_NOTFOUND,
          "5f 断らなかった source は最後の行の値 (SCRIPT_ERR_REFUSED ではない)");
}

/* ========================================================================
 *  6. 経路 3 — パイプの段
 *
 *  段は execute_single を直に呼ぶので、段の中で断った印は execute_command
 *  の入口の掃除に消されない。
 *
 *  段 2 の時点では「行を打ち切るだけで、段の続行は bash の `false | cat`
 *  どおり」にしていた。**PM 決裁 (独立レビューの指摘を受けて) で票
 *  §2の「行全体を実行しない」に合わせた** — 断った段があったら
 *  **後続の段も実行しない**。理由は `<断られる段> | tee 重要ファイル` —
 *  続けると断ったのに書き込みが起き、`> file` は O_TRUNC なので
 *  リダイレクト先が空で上書きされ得る。
 * ======================================================================== */
static void case_pipe_stage(void)
{
    char a[600], b[600];

    report("6 経路 3: パイプの段\n");

    /* 先頭 255 文字が同じ 256 文字の 2 本 (1c と同じ反例) */
    fill_run(a, 256, 'x');
    fill_run(b, 256, 'y');

    fresh();
    s_begin(0);
    s_add("set B=");  s_run(150);  s_add("\n");
    s_add("if ${B}${B} == b markpipe | echo tail\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("markpipe"), "6a 段の中の if は右辺を実行しない");
    check(refused_msg("if: left value"), "6b 段の中の断りも 1 行出る");
    check(!ran("marknext"), "6c 段で断ったら後続行を実行しない (取りこぼしなし)");

    /* 取りこぼしの罠: 断った段の**後ろ**の段が execute_command を入れ子で
     * 呼ぶ (`time ...`)。入口の掃除を入れ子でも走らせると、ここで印が
     * 消えて後続行へ落ちる。 */
    fresh();
    s_begin(0);
    s_add("set B=");  s_run(150);  s_add("\n");
    s_add("if ${B}${B} == b markpipe | time echo tail\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("markpipe"), "6f 後段が time でも右辺を実行しない");
    check(!ran("marknext"),
          "6g 後段の入れ子に印を消させない (取りこぼしなし)");

    /* 誤発火の裏: 通るパイプは今までどおり */
    fresh();
    s_begin(0);
    s_add("echo hello | echo tail\n");
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk2"), "6d 通るパイプの後も打ち切らない");
    check(sh_refused_flag == 0, "6e 通るパイプは印を残さない");

    /* ---- PM 決裁: 断った段があったら後続の段を実行しない ----------- */

    /* 6h: 後続の段が走ったかは、その段が書く痕跡 (echo の出力) で見る。
     * SHELL_AS_APP では外部段を含むパイプが丸ごと断られるので、
     * 段は全部内蔵コマンドにする。 */
    fresh();
    line_reset();
    line_add("if "); line_add(a); line_add(" == "); line_add(b);
    line_add(" markpipe | echo STAGE2RAN");
    execute_command(g_line);
    check(refused_msg("if: left value"), "6h 段 1 で断る");
    check(!ran("markpipe"),      "6i 断った段の右辺は実行しない");
    check(!out_has("STAGE2RAN"),
          "6j 断った段の**後続の段**を実行しない");

    /* 6k: 後続の段の `> file` は O_TRUNC。走らせると断ったのに
     * リダイレクト先が空で上書きされる (この決裁の根拠)。 */
    fresh();
    file_add("/keep.txt", "IMPORTANT");
    line_reset();
    line_add("if "); line_add(a); line_add(" == "); line_add(b);
    line_add(" markpipe | echo tail > /keep.txt");
    execute_command(g_line);
    check(!redir_opened("/keep.txt"),
          "6k 後続の段のリダイレクト先を開かない (空で上書きしない)");
    check(g_redir_count == 0, "6l 断った行はリダイレクトを 1 つも張らない");

    /* 6m: 3 段の真ん中で断ったときも 3 段目は走らない。
     * 1 段目 (STAGE1RAN) は断る前なので今までどおり走る。 */
    fresh();
    line_reset();
    line_add("echo STAGE1RAN | if ");
    line_add(a); line_add(" == "); line_add(b);
    line_add(" markpipe | echo STAGE3RAN");
    execute_command(g_line);
    check(out_has("STAGE1RAN"), "6m 断る前の段は走る");
    check(!out_has("STAGE3RAN"), "6n 断った段の後ろの段は走らない (3 段)");

    /* 誤発火の裏 1: 断っていないパイプは今までどおり全段走る */
    fresh();
    execute_command("echo ONE | echo TWO | echo THREE");
    check(out_has("ONE") && out_has("TWO") && out_has("THREE"),
          "6o 誤発火なし: 通る 3 段は全段走る");
    check(sh_refused_flag == 0, "6p 通る 3 段は印を残さない");

    /* 誤発火の裏 2: 通るパイプの最終段の `> file` は今までどおり張る */
    fresh();
    file_add("/keep.txt", "IMPORTANT");
    execute_command("echo head | echo tail > /keep.txt");
    check(redir_opened("/keep.txt"),
          "6q 誤発火なし: 通るパイプのリダイレクトは今までどおり張る");

    /* 誤発火の裏 3: `if` が**偽**で右辺を走らせないだけのときは
     * 断りではないので、後続の段は今までどおり走る。 */
    fresh();
    execute_command("if abc == abd markpipe | echo STAGE2RAN");
    check(!ran("markpipe"), "6r 偽の if は右辺を走らせない");
    check(out_has("STAGE2RAN"),
          "6s 誤発火なし: 偽の if は断りではないので後続の段は走る");
}

/* ========================================================================
 *  7. 経路 4 / 5 — rshell と対話 (スクリプトではないので打ち切らない)
 *
 *  rshell.c は serial を握るのでこの試験には取り込んでいない。rshell が
 *  やっているのは「1 行ずつ execute_command を呼ぶ」だけ (rshell.c:149)
 *  なので、ここでは同じ呼び方を並べて「次の行が動くこと」を押さえる。
 *  EOT (票 §2-2) は段 4 の担当。
 * ======================================================================== */
static void case_interactive_not_aborted(void)
{
    char a[600], b[600];

    report("7 経路 4/5: rshell / 対話では打ち切らない\n");

    fill_run(a, 256, 'x');
    fill_run(b, 256, 'y');

    fresh();
    execute_command("mk1");
    line_reset();
    line_add("if "); line_add(a); line_add(" == "); line_add(b);
    line_add(" markif");
    execute_command(g_line);               /* ← 断られる行 */
    execute_command("mk2");                /* ← 次の行 */
    check(ran("mk1"), "7a 断る前の行は走る");
    check(!ran("markif"), "7b 断った行は走らない");
    check(ran("mk2"), "7c 断った**次の**行は今までどおり走る");
    check(sh_refused_flag == 0, "7d 次の行の入口で印が消えている");

    /* 対話で断った印が、その後のスクリプトを巻き添えにしないこと */
    fresh();
    line_reset();
    line_add("if "); line_add(a); line_add(" == "); line_add(b);
    line_add(" markif");
    execute_command(g_line);               /* 印が立ったまま誰も読まない */
    s_begin(0);
    s_add("mk1\n");
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1") && ran("mk2"),
          "7e 直前に対話で断っても、次のスクリプトは最後まで走る");
}

/* ========================================================================
 *  8. 経路 6 — 起動時の /etc/profile
 *
 *  断ったらメッセージを出して**既定値で続行する** (起動を止めない)。
 *  ui.c の shell_run() は端末を握るので取り込めない。shell_run が呼ぶ
 *  script_source_profile() を実物のまま通す。
 * ======================================================================== */
static void case_profile_continues(void)
{
    report("8 経路 6: /etc/profile は断っても起動を止めない\n");

    fresh();
    s_begin(0);
    s_add("set A=");  s_run(240);  s_add("\n");
    s_add("if ${A}${A}x == ${A}${A}y markif\n");
    s_add("markprofile\n");
    file_add("/etc/profile", s_body(0));

    script_source_profile("/etc/profile");
    check(!ran("markif"),      "8a profile: 断った行は実行しない");
    check(!ran("markprofile"), "8b profile: その後の行も実行しない");
    check(out_has("continuing with defaults"),
          "8c profile: 既定値で続けると言う");
    check(sh_refused_flag == 0,
          "8d profile: 印を残さない (次の行を巻き添えにしない)");

    /* 起動は続く — profile の後の行が動く */
    execute_command("mk1");
    check(ran("mk1"), "8e profile の後のコマンドは動く (起動を止めない)");

    /* 誤発火の裏: 通る profile は何も言わない */
    fresh();
    s_begin(0);
    s_add("mk2\n");
    file_add("/etc/profile", s_body(0));
    script_source_profile("/etc/profile");
    check(ran("mk2"), "8f 通る profile は今までどおり走る");
    check(!out_has("continuing with defaults"), "8g 通ったら黙っている");
}

/* ========================================================================
 *  9. 誤発火の総まとめ — 断る理由が無いスクリプトは最後まで走る
 * ======================================================================== */
static void case_no_false_abort(void)
{
    report("9 誤発火なし: 断る理由が無ければ最後まで走る\n");

    fresh();
    s_begin(0);
    s_add("mk1\n");
    s_add("if abc == abc mk2\n");
    s_add("if abc == abd mk3\n");        /* 偽 — 断りではない */
    s_add("mk4\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1"), "9a 1 行目が走る");
    check(ran("mk2"), "9b 真の if が走る");
    check(!ran("mk3"), "9c 偽の if は走らない (が打ち切りではない)");
    check(ran("mk4"), "9d 偽の if の後も走り続ける");
    check(!out_has("script: aborted"), "9e 打ち切りの報告を出さない");
}

/* ========================================================================
 *  10. U4 — try_exec の再構築が溢れたら **子を起こさない** (T3)
 *
 *  「子が起きたか」の窓は g_launch_count (= launch_req を呼んだ回数)。
 *  段 2b の報告どおり、この窓が無いと「断りの行が出た」だけで偽の緑になる。
 *  h_launch_req は OS32_ERR_INVAL を返す (= GUI 外 = 起こさなかった) が、
 *  **呼ばれたこと自体**が「切り詰めた行で起動しにいった」証拠になる。
 *
 *  数え方はクォートの再付与ぶんを含める (票 §1 T3):
 *    区切りの空白 1 + 本体 (" と \ は 2 倍) + クォートが要るなら +2
 * ======================================================================== */
static void case_try_exec_refuses(void)
{
    report("10 U4: try_exec の再構築が溢れたら子を起こさない (T3)\n");

    /* 名前に `/` を入れて 2a (直接実行) の枝へ落とす — PATH 走査に入ると
     * 候補ごとに接頭辞のぶん長さが変わり、境界の検査が読めなくなる。
     * 以下 bin は "./mk1.bin" = 9 バイト。
     *
     * 境界の **裏側** (ちょうど 510) は、sh.bin では try_exec を通り抜けても
     * 要求表の上限 (LAUNCH_CMDLINE_MAX - 1 = 255、T11) に当たって断られる。
     * したがって裏側の証人は「launch_req を呼んだか」ではなく
     * 「**この層** (sh: argument list) が断っていないこと」— 層が別なのは
     * 断りの文言で見分ける。子が起きないことは両方で launch_count が示す。 */

    /* --- (a) 素の長さで溢れる場合 ------------------------------------- */
    /* 9 + 1 + 501 = 511 > 510 */
    fresh();
    line_reset();
    line_add("./mk1.bin ");
    line_add_run('a', 501);
    execute_command(g_line);
    check(g_launch_count == 0, "10a 511 バイト: 子を起こさない");
    check(refused_msg("sh: argument list"), "10b 何が溢れたか + 上限を出す");

    /* 誤発火の裏: ちょうど 510 ではこの層は断らない (境界は「以上」で数える) */
    fresh();
    line_reset();
    line_add("./mk1.bin ");
    line_add_run('a', 500);
    execute_command(g_line);
    check(!out_has("sh: argument list"), "10c 510 ちょうどはこの層で断らない");
    check(out_has("sh: launch command line"),
          "10d 510 は次の層 (要求表 255) が断る — 層が違うことを文言で見る");

    /* --- (b) クォートの再付与で伸びる場合 ------------------------------ */
    /*  引数 1 つが `"` 250 個。素直に数えると 9 + 1 + 250 = 260 で余裕だが、
     *  try_exec は `"` を \" にして前後をクォートで包むので
     *  9 + 1 + 2 + 250*2 = 512 > 510。**エスケープを数えない実装はここを
     *  通してしまい、引数が途中で切れたまま子が起きる**。 */
    fresh();
    line_reset();
    line_add("./mk1.bin ");
    line_add_rep("\\\"", 250);
    execute_command(g_line);
    check(g_launch_count == 0, "10e \" の 2 倍を数える: 子を起こさない");
    check(refused_msg("sh: argument list"), "10f 同上: 断りが出る");

    /* 境界の裏: 249 個なら 9 + 1 + 2 + 498 = 510 でちょうど通る */
    fresh();
    line_reset();
    line_add("./mk1.bin ");
    line_add_rep("\\\"", 249);
    execute_command(g_line);
    check(!out_has("sh: argument list"), "10g \" 249 個 (= 510) はこの層を通る");

    /* --- (c) 空白入りの引数は前後の `"` で +2 -------------------------- */
    /*  84 個の `a b` = 84 * (1 + 3 + 2) = 504、+ 9 = 513 > 510。
     *  クォートを数えないと 9 + 84*4 = 345 で通ってしまう。 */
    fresh();
    line_reset();
    line_add("./mk1.bin");
    line_add_rep(" \"a b\"", 84);
    execute_command(g_line);
    check(g_launch_count == 0, "10i 空白入り引数の +2 を数える: 子を起こさない");
    check(refused_msg("sh: argument list"), "10j 同上: 断りが出る");

    fresh();
    line_reset();
    line_add("./mk1.bin");
    line_add_rep(" \"a b\"", 83);       /* 9 + 498 = 507 */
    execute_command(g_line);
    check(!out_has("sh: argument list"), "10k 83 個 (= 507) はこの層を通る");

    /* --- (d) スクリプト中なら打ち切る --------------------------------- */
    /*  スクリプトの 1 行は 255 バイトまで (T2、段 4 の担当) なので、
     *  511 バイトの行は変数展開で作る。${A} は 247 バイト。
     *  9 + 3 * (1 + 247) = 753 > 510 */
    fresh();
    s_begin(0);
    s_add("set A=");  s_run(247);  s_add("\n");
    s_add("./mk1.bin ${A} ${A} ${A}\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(g_launch_count == 0, "10l スクリプト: 子を起こさない");
    check(refused_msg("sh: argument list"), "10m スクリプト: 断りが出る");
    check(!ran("marknext"),    "10n スクリプト: 後続行も実行しない");
}

/* ========================================================================
 *  11. U4 — 内蔵 `exec` (255) と `time` (510) も起動前に断る (T3)
 * ======================================================================== */
static void case_exec_time_refuse(void)
{
    report("11 U4: 内蔵 exec (255) と time (510)\n");

    /* exec: argv[1..] を空白で繋いだ長さが 256 以上なら断る。
     * "mk1.bin" (7) + " " + 'a'*248 = 256 */
    fresh();
    line_reset();
    line_add("exec mk1.bin ");
    line_add_run('a', 248);
    execute_command(g_line);
    check(g_launch_count == 0, "11a exec 256 バイト: 子を起こさない");
    check(refused_msg("exec: command line"), "11b exec: 断りが出る");

    /* 誤発火の裏: 255 ちょうどは通る */
    fresh();
    line_reset();
    line_add("exec mk1.bin ");
    line_add_run('a', 247);
    execute_command(g_line);
    check(g_launch_count == 1, "11c exec 255 ちょうどは launch_req まで行く");
    check(!out_has("too long"), "11d exec 255 では断らない");

    /* time: 組み立てる行が 511 以上なら断る。
     * "./mk1.bin" (9) + " " + 'a'*501 = 511 */
    fresh();
    line_reset();
    line_add("time ./mk1.bin ");
    line_add_run('a', 501);
    execute_command(g_line);
    check(g_launch_count == 0, "11e time 511 バイト: 内側を実行しない");
    check(refused_msg("time: command line"), "11f time: 断りが出る");
    check(!out_has("real  "), "11g time: 計測結果も出さない");
    check(!out_has("sh: argument list"),
          "11h time: 内側の try_exec まで行かせない (断るのは time の層)");

    /* 誤発火の裏: 510 ちょうどは time を通り抜けて内側へ渡る。
     * (内側は要求表の 255 に当たるので子は起きない — 層が違う) */
    fresh();
    line_reset();
    line_add("time ./mk1.bin ");
    line_add_run('a', 500);
    execute_command(g_line);
    check(!out_has("time: command line"), "11i time 510 では断らない");
    check(out_has("sh: launch command line"),
          "11j time 510 は内側へ渡る (次の層が断る)");
}

/* ========================================================================
 *  12. U5 — 251 バイトで切った名前に .bin を付けて **別のファイル** を
 *      起動しない (T4)
 *
 *  窓は g_launch_last: 切り詰める実装はここに「先頭 251 文字 + .bin」が
 *  入る (= 実在する別のファイル)。直った実装は launch_req を呼ばない。
 * ======================================================================== */
static void case_cmd_name_refuses(void)
{
    report("12 U5: 251 で切った名前で別のファイルを起動しない (T4)\n");

    fresh();
    line_reset();
    line_add_run('P', 252);              /* PATH_MAX_LEN - 5 = 251 を 1 超える */
    execute_command(g_line);
    check(g_launch_count == 0, "12a 252 文字の名前: 子を起こさない");
    check(refused_msg("sh: command name"), "12b 断りが出る");
    check(!out_has("command not found"),
          "12c 断った行に \"command not found\" を足さない");

    /* 誤発火の裏: 251 ちょうどは今までどおり解決を試みる */
    fresh();
    line_reset();
    line_add_run('P', 251);
    execute_command(g_line);
    check(g_launch_count >= 1, "12d 251 ちょうどは起動しにいく");
    check(!out_has("sh: command name"), "12e 251 では名前の断りを出さない");
}

/* ========================================================================
 *  13. U6 — パイプの段を捨てない (T5)
 *
 *  窓は段が書く痕跡 (echo の出力)。以前は 9 段目と空の段を **黙って捨てて**
 *  いたので、`echo ok |` が 1 段として実行されていた。
 *
 *  クォートは今までどおり見ない。したがって `echo "a||b"` の中の `|` も
 *  区切りのままで、空の段として断られる (票 §6 で範囲外と決めた分割規則)。
 * ======================================================================== */
static void case_pipeline_stages(void)
{
    report("13 U6: 9 段 / 空の段 / a || b を捨てずに断る (T5)\n");

    /* 9 段 — 1 段目も実行しない (行全体を断る) */
    fresh();
    execute_command("echo S1|echo S2|echo S3|echo S4|echo S5|echo S6|echo S7|"
                    "echo S8|echo S9");
    check(refused_msg("sh: pipeline"), "13a 9 段: 上限つきで断る");
    check(!out_has("S1") && !out_has("S8") && !out_has("S9"),
          "13b 9 段: どの段も実行しない");

    /* 誤発火の裏: 8 段ちょうどは全段走る */
    fresh();
    execute_command("echo S1|echo S2|echo S3|echo S4|echo S5|echo S6|echo S7|"
                    "echo S8");
    check(out_has("S1") && out_has("S8"), "13c 8 段ちょうどは全段走る");
    check(!out_has("too long"), "13d 8 段では断らない");

    /* 末尾の空の段 — 以前は 1 段として **実行されていた** */
    fresh();
    execute_command("echo PIPEOK |");
    check(out_has("empty pipeline stage"), "13e `cmd |`: 空の段を断る");
    check(!out_has("PIPEOK"), "13f `cmd |`: 前の段も実行しない");
    check(sh_refused_flag == 1, "13g 断りの印が立っている");

    /* 先頭の空の段 */
    fresh();
    execute_command("| echo PIPEOK");
    check(out_has("empty pipeline stage"), "13h `| cmd`: 空の段を断る");
    check(!out_has("PIPEOK"), "13i `| cmd`: 後ろの段も実行しない");

    /* `a || b` — 真ん中が空 */
    fresh();
    execute_command("echo PA || echo PB");
    check(out_has("empty pipeline stage"), "13j `a || b`: 断る");
    check(!out_has("PA") && !out_has("PB"), "13k `a || b`: どちらも実行しない");

    /* クォートの中の `|` も今までどおり区切り = 同じ規則で断る (票 §6) */
    fresh();
    execute_command("echo \"a||b\"");
    check(out_has("empty pipeline stage"),
          "13l `echo \"a||b\"`: クォートは見ないので同じ規則で断る");

    /* スクリプト中なら後続行も実行しない (印が立っていること) */
    fresh();
    s_begin(0);
    s_add("echo PIPEOK |\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!out_has("PIPEOK"),  "13m スクリプト: 断った行は実行しない");
    check(!ran("marknext"),    "13n スクリプト: 後続行も実行しない (印が立つ)");

    /* 誤発火の裏: 素直な 2 段は今までどおり */
    fresh();
    execute_command("echo ONE | echo TWO");
    check(out_has("ONE") && out_has("TWO"), "13o 通る 2 段は今までどおり");
    check(sh_refused_flag == 0, "13p 通る 2 段は印を残さない");
}

/* ========================================================================
 *  14. U7 — glob の mem_alloc が失敗したら行ごと断る (T6)
 *
 *  以前は黙って戻っていたので、一致の **一部だけ** が handler へ渡った
 *  (`rm /tmp/item*` が 1 件だけ消えて成功に見える)。
 * ======================================================================== */
static void case_glob_alloc_fail(void)
{
    report("14 U7: glob の確保失敗は行ごと断る (T6)\n");

    fresh();
    dir_set("/d/");
    dir_add("item1");
    dir_add("item2");
    dir_add("item3");
    g_glob_alloc_budget = 1;            /* 2 件目の確保で失敗する */
    execute_command("echo /d/item*");
    check(!out_has("item1") && !out_has("item2"),
          "14a 一致の一部だけを handler へ渡さない");
    check(out_has("glob: out of memory"), "14b 理由を 1 行出す");
    check(g_glob_allocs == 1 && g_frees >= 1,
          "14c 確保済みの文字列を解放する");

    /* 誤発火の裏: 確保が通れば今までどおり全件展開して handler を呼ぶ */
    fresh();
    dir_set("/d/");
    dir_add("item1");
    dir_add("item2");
    execute_command("echo /d/item*");
    check(out_has("item1") && out_has("item2"),
          "14d 確保が通れば今までどおり全件渡す");
    check(!out_has("out of memory"), "14e 誤発火なし");

    /* スクリプト中なら打ち切る */
    fresh();
    dir_set("/d/");
    dir_add("item1");
    dir_add("item2");
    s_begin(0);
    s_add("echo /d/item*\n");
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    g_glob_alloc_budget = 1;
    execute_command("source /t.sh");
    check(out_has("glob: out of memory"),
          "14f スクリプト: 断ったのは glob の確保失敗 (別の理由ではない)");
    check(!ran("marknext"), "14g スクリプト: 後続行も実行しない");
}

/* ========================================================================
 *  15. U8 — 上限を超える glob パターン / ディレクトリ部は **照合を試みない**
 *      (T7)
 *
 *  窓は g_ls_calls (sys_ls を呼んだ回数)。切ったパターンで照合すると
 *  別のファイルに当たるので、呼ぶ前に断ること。
 * ======================================================================== */
static void case_glob_pattern_refuses(void)
{
    report("15 U8: 長い glob パターン / ディレクトリ部は照合しない (T7)\n");

    /* パターン 256 文字 ('a'*255 + '*') */
    fresh();
    dir_set(".");
    dir_add("aaa");
    line_reset();
    line_add("echo ");
    line_add_run('a', 255);
    line_add("*");
    execute_command(g_line);
    check(g_ls_calls == 0, "15a 256 文字のパターン: 照合を試みない");
    check(refused_msg("sh: glob pattern"), "15b 断りが出る");

    /* 誤発火の裏: 255 文字ちょうど ('a'*254 + '*') は照合する */
    fresh();
    dir_set(".");
    dir_add("aaa");
    line_reset();
    line_add("echo ");
    line_add_run('a', 254);
    line_add("*");
    execute_command(g_line);
    check(g_ls_calls == 1, "15c 255 文字ちょうどは照合する");
    check(!out_has("too long"), "15d 255 文字では断らない");

    /* ディレクトリ部 256 文字 ('/' + 'a'*254 + '/') */
    fresh();
    dir_set(".");
    line_reset();
    line_add("echo /");
    line_add_run('a', 254);
    line_add("/x*");
    execute_command(g_line);
    check(g_ls_calls == 0, "15e 256 文字のディレクトリ部: 照合を試みない");
    check(refused_msg("sh: glob directory"), "15f 断りが出る");

    /* 誤発火の裏: 255 文字ちょうど ('/' + 'a'*253 + '/') は照合する */
    fresh();
    dir_set(".");
    line_reset();
    line_add("echo /");
    line_add_run('a', 253);
    line_add("/x*");
    execute_command(g_line);
    check(g_ls_calls == 1, "15g 255 文字ちょうどのディレクトリ部は照合する");
    check(!out_has("too long"), "15h 同上: 断らない");
}

/* ========================================================================
 *  16. U12 — 256 バイト以上の行で launch_req を呼ばない (T11)
 *
 *  try_exec の上限 (510) は通るが要求表の上限 (256) は超える長さで、
 *  「送る前に測って断る」が効いていることを見る。以前は launch_req が
 *  OS32_ERR_INVAL を返し、sh.bin がそれを **GUI 外** と読み違えていた。
 * ======================================================================== */
static void case_launch_cmdline_refuses(void)
{
    report("16 U12: 256 バイト以上は launch_req を呼ばない (T11)\n");

    /* "./mk1.bin" (9) + " " + 'a'*246 = 256。名前に `/` を入れて PATH 走査を
     * 避ける (候補ごとに接頭辞のぶん長さが変わるため)。 */
    fresh();
    line_reset();
    line_add("./mk1.bin ");
    line_add_run('a', 246);
    execute_command(g_line);
    check(g_launch_count == 0, "16a 256 バイト: launch_req を呼ばない");
    check(refused_msg("sh: launch command line"), "16b 断りが出る");
    check(!out_has("need the GUI terminal"),
          "16c 理由を「GUI 外」と取り違えない");

    /* 誤発火の裏: 255 ちょうどは今までどおり送る */
    fresh();
    line_reset();
    line_add("./mk1.bin ");
    line_add_run('a', 245);
    execute_command(g_line);
    check(g_launch_count == 1, "16d 255 ちょうどは launch_req を呼ぶ");
    check(!out_has("too long"), "16e 255 では断らない");

    /* PATH 走査の途中で断ったら **そこで止める**。候補ごとに接頭辞のぶん
     * 行が伸びるので、止めないと同じ赤字が候補の数だけ出る。
     *   "mk1.bin" (7)            + 1 + 240 = 248  … 通る
     *   "/bin/mk1.bin" (12)      + 1 + 240 = 253  … 通る
     *   "/usr/bin/mk1.bin" (16)  + 1 + 240 = 257  … 断る (ここで止める)
     *   "/usr/local/bin/…" (22)  + 1 + 240 = 263  … 止めていなければもう 1 行 */
    fresh();
    env_set("PATH", "/bin:/usr/bin:/usr/local/bin");
    line_reset();
    line_add("mk1.bin ");
    line_add_run('a', 240);
    execute_command(g_line);
    check(out_count("sh: launch command line") == 1,
          "16f 断りは 1 行だけ (PATH 走査を止める)");
    check(g_launch_count == 2,
          "16g 断った後の候補を試さない (カレント + /bin の 2 回だけ)");
    check(!out_has("command not found"),
          "16h 断った行に \"command not found\" を足さない");
    env_set("PATH", SYS_DEFAULT_PATH);
}

/* ========================================================================
 *  17. U13 — CMD_BUF_SIZE 以上の行は空行と区別して断る (T13)
 * ======================================================================== */
static void case_long_line_refuses(void)
{
    report("17 U13: 長すぎる行を空行と同じ扱いにしない (T13)\n");

    /* execute_command: "echo " (5) + 'a'*4091 = 4096 */
    fresh();
    (void)big_line("echo ", CMD_BUF_SIZE - 5);
    execute_command(g_big);
    check(refused_msg("sh: command line"), "17a execute_command: 断る");

    /* 誤発火の裏: 4095 ちょうどは今までどおり走る */
    fresh();
    (void)big_line("echo ", CMD_BUF_SIZE - 6);
    execute_command(g_big);
    check(out_has("aaaa"), "17b 4095 ちょうどは今までどおり走る");
    check(!out_has("too long"), "17c 4095 では断らない");

    /* 空行は今までどおり黙って戻る (断りではない) */
    fresh();
    execute_command("");
    check(!out_has("too long"), "17d 空行は断らない (黙って戻る)");
    check(sh_refused_flag == 0, "17e 空行は印を立てない");

    /* execute_single も同じ規則 (パイプの段はここを直に通る) */
    fresh();
    (void)big_line("echo ", CMD_BUF_SIZE - 5);
    execute_single(g_big);
    check(refused_msg("sh: command"), "17f execute_single: 断る");

    fresh();
    execute_single("");
    check(!out_has("too long"), "17g execute_single: 空行は断らない");

    /* スクリプト中なら後続行も実行しない */
    fresh();
    (void)big_line("echo ", CMD_BUF_SIZE - 5);
    s_begin(0);
    s_add("marknext\n");
    file_add("/t.sh", s_body(0));
    execute_command(g_big);
    execute_command("source /t.sh");
    check(ran("marknext"),
          "17h 対話では次の行を巻き添えにしない (印は入口で消える)");
}

/* ========================================================================
 *  18. U17 — 254 文字を超える PATH 項目で区切りを見失わない (T17)
 *
 *  以前は `di < PATH_MAX_LEN - 2` で止まって残りが **次の項目** になり、
 *  別のディレクトリの同名バイナリを試していた。窓は g_launch_count と
 *  g_launch_last (どのパスを起こしにいったか)。
 * ======================================================================== */
static void case_path_entry_refuses(void)
{
    static char path_buf[ENV_VALUE_MAX];
    int i;

    report("18 U17: 長い PATH 項目で区切りを見失わない (T17)\n");

    /* 項目 255 文字 ('/' + 'a'*254) — 254 バイトで切れて残り 1 文字が
     * 次の項目になる */
    path_buf[0] = '/';
    for (i = 1; i < 255; i++) path_buf[i] = 'a';
    path_buf[255] = '\0';

    fresh();
    env_set("PATH", path_buf);
    execute_command("mk1");
    check(g_launch_count == 1,
          "18a 255 文字の項目: カレントの 1 回だけ (別のディレクトリを試さない)");
    check(refused_msg("sh: PATH entry"), "18b 断りが出る");

    /* 項目は収まるが dir + '/' + name が入り切らない場合も断る
     * ('/' + 'a'*249 = 250、+ '/' + "mk1.bin" (7) = 258 > 255) */
    path_buf[0] = '/';
    for (i = 1; i < 250; i++) path_buf[i] = 'a';
    path_buf[250] = '\0';

    fresh();
    env_set("PATH", path_buf);
    execute_command("mk1");
    check(g_launch_count == 1,
          "18c 連結が入り切らない: カレントの 1 回だけ (切ったパスを試さない)");
    check(refused_msg("sh: command path"), "18d 断りが出る");

    /* 誤発火の裏: 普通の PATH は今までどおり全候補を試す */
    fresh();
    env_set("PATH", "/bin:/usr/bin");
    execute_command("mk1");
    check(g_launch_count >= 2, "18e 普通の PATH は候補ぶん試す");
    check(!out_has("too long"), "18f 普通の PATH では断らない");
    check(out_has("command not found"), "18g 見つからなければ今までどおり");

    env_set("PATH", SYS_DEFAULT_PATH);   /* 後の試験のために戻す */
}

/* ========================================================================
 *  19. I1 — 引数が多すぎて行を捨てるときも印を立てる (PM 決裁 2026-09-16)
 *
 *  T6 と同じ関数の中にある同じ型の欠陥。`sh: too many arguments` は赤字を
 *  出すが印を立てていなかったので、**スクリプトが次の行へ落ちていた**。
 *  文言は据え置きで印だけ足す。
 *
 *  MAX_ARGS は 256 で、argv[argc] へ NUL を置くぶん **格納は 255 個まで**。
 *  つまり語が 255 個の行は通り、256 個目で断る。
 *  断る場所は 3 か所あるので全部踏む:
 *    (a) glob の展開中に溢れる  (ctx.overflow)
 *    (b) 一致しない glob を足せない (!matched_any の側)
 *    (c) 素の語を足せない       (else の側)
 * ======================================================================== */
static void case_too_many_args_marks(void)
{
    report("19 I1: 引数が多すぎて捨てるときも印を立てる\n");

    /* --- (c) 素の語で溢れる: `echo` + 255 個 = 256 語 ------------------ */
    /*  `echo` は内蔵なので try_exec を通らない (T3 の断りと混ざらない) */
    fresh();
    line_reset();
    line_add("echo");
    line_add_rep(" a", 255);
    execute_command(g_line);
    check(out_has("too many arguments"), "19a 256 語: 行ごと捨てる");
    check(sh_refused_flag == 1, "19b 256 語: 印を立てる");

    /* 誤発火の裏: 255 語ちょうどは今までどおり通って echo が走る */
    fresh();
    line_reset();
    line_add("echo");
    line_add_rep(" a", 254);
    execute_command(g_line);
    check(!out_has("too many arguments"), "19c 255 語ちょうどは通る");
    check(sh_refused_flag == 0, "19d 255 語は印を残さない");

    /* --- (a) glob の展開中に溢れる (ctx.overflow) --------------------- */
    /*  echo (1) + 素の語 250 個 = argc 251。/d/ の 8 件のうち 4 件までは
     *  入り (argc 255)、5 件目で ctx.overflow が立つ。 */
    fresh();
    dir_set("/d/");
    dir_add("item1"); dir_add("item2"); dir_add("item3"); dir_add("item4");
    dir_add("item5"); dir_add("item6"); dir_add("item7"); dir_add("item8");
    line_reset();
    line_add("echo");
    line_add_rep(" a", 250);
    line_add(" /d/item*");
    execute_command(g_line);
    check(out_has("too many arguments"), "19e glob の展開で溢れたら捨てる");
    check(sh_refused_flag == 1, "19f 同上: 印を立てる");
    check(!out_has("item1"), "19g 同上: 一部だけ渡さない");

    /* --- (b) 一致しない glob を足せない (!matched_any の側) ----------- */
    /*  echo (1) + 素の語 254 個 = argc 255。一致しない glob はそのまま
     *  1 語として足したいが、もう入らない。 */
    fresh();
    dir_set("/d/");
    dir_add("item1");
    line_reset();
    line_add("echo");
    line_add_rep(" a", 254);
    line_add(" /d/zzz*");
    execute_command(g_line);
    check(out_has("too many arguments"), "19h 一致しない glob も足せなければ捨てる");
    check(sh_refused_flag == 1, "19i 同上: 印を立てる");

    /* --- スクリプト中なら後続の行を実行しない ------------------------- */
    /*  スクリプトの 1 行は 255 バイトまで (T2、段 4) なので、語は変数展開で
     *  増やす。${A} は 60 語ぶんの並び (値はクォートで 1 語として渡す)。
     *  展開後は 1 + 60*5 = 301 語で、上限 255 を超える。 */
    {
        int k;
        fresh();
        s_begin(0);
        s_add("set A=\"a");
        for (k = 1; k < 60; k++) s_add(" a");
        s_add("\"\n");
        s_add("echo ${A} ${A} ${A} ${A} ${A}\n");
        s_add("marknext\n");
        file_add("/t.sh", s_body(0));
        execute_command("source /t.sh");
        check(out_has("too many arguments"), "19j スクリプト: 301 語の行を捨てる");
        check(!ran("marknext"),
              "19k スクリプト: 後続の行を実行しない (印が立っている)");
        check(out_has("script: aborted"), "19l スクリプト: 打ち切ったと言う");
    }

    /* --- 対話では打ち切らない ----------------------------------------- */
    fresh();
    line_reset();
    line_add("echo");
    line_add_rep(" a", 255);
    execute_command(g_line);         /* ← 捨てられる行 */
    execute_command("mk1");          /* ← 次の行 */
    check(out_has("too many arguments"), "19m 対話: 捨てた行は報せる");
    check(ran("mk1"), "19n 対話: 次の行は今までどおり走る");
    check(sh_refused_flag == 0, "19o 対話: 次の行の入口で印が消えている");

    /* --- パイプの段で捨てたら後続の段も実行しない (段 2b の規則が効く) -- */
    fresh();
    line_reset();
    line_add("echo");
    line_add_rep(" a", 255);
    line_add(" | echo STAGE2RAN");
    execute_command(g_line);
    check(out_has("too many arguments"), "19p 段の中で捨てる");
    check(!out_has("STAGE2RAN"), "19q 捨てた段の後続の段を実行しない");
}


/* ========================================================================
 *  20. U3 — スクリプトの読み込み (T2 / T2')
 *
 *  「切り詰めたら実行しない」をロードの段で守る。切れた行・捨てた行・
 *  読み切れなかったファイルのどれでも **1 行も実行しない**。
 *  走ったかどうかの窓は ran() (run_cmd_internal の "command not found")。
 * ======================================================================== */

/* 32KB を超えるスクリプト本文 (SBUF_SIZE には入らないので別に持つ) */
#define BIGSCRIPT_CAP 40000
static char g_bigscript[BIGSCRIPT_CAP];

/* head + unit を n 回。長さを返す */
static int bigscript_build(const char *head, const char *unit, int n)
{
    int len = 0;
    int i, j;

    for (i = 0; head[i] && len < BIGSCRIPT_CAP - 1; i++) g_bigscript[len++] = head[i];
    for (i = 0; i < n; i++)
        for (j = 0; unit[j] && len < BIGSCRIPT_CAP - 1; j++)
            g_bigscript[len++] = unit[j];
    g_bigscript[len] = '\0';
    return len;
}

static void case_script_load_refuses(void)
{
    int i;

    report("20 U3: スクリプトの行 / 行数 / 読み込み上限 (T2)\n");

    /* --- 256 文字以上の行 → スクリプトを 1 行も実行しない -------------- */
    fresh();
    s_begin(0);
    s_add("mk1\n");
    s_add("echo ");  s_run(251);  s_add("\n");    /* 5 + 251 = 256 文字 */
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("mk1"), "20a 切れる行があればスクリプトを実行しない (前の行も)");
    check(!ran("mk2"), "20b 同上: 後ろの行も実行しない");
    check(refused_msg("source: script line"),
          "20c 何が上限を超えたか + 上限を報告する");

    /* 255 文字ちょうどは通る (誤発火の裏) */
    fresh();
    s_begin(0);
    s_add("mk1\n");
    s_add("echo ");  s_run(250);  s_add("\n");    /* 5 + 250 = 255 文字 */
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1"), "20d 255 文字ちょうどの行は通る");
    check(!out_has("too long"), "20e 同上: 断らない");

    /* --- 129 行目 → 先頭 128 行も実行しない ---------------------------- */
    fresh();
    s_begin(0);
    s_add("mk1\n");
    for (i = 0; i < 128; i++) s_add("z\n");       /* 合計 129 行 */
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("mk1"), "20f 129 行あれば先頭 128 行も実行しない");
    check(!ran("z"),   "20g 同上: 捨てた行も当然実行しない");
    check(out_has("too many lines"), "20h 行数超過を報せる");

    /* 128 行ちょうどは通る (裏) */
    fresh();
    s_begin(0);
    s_add("mk1\n");
    for (i = 0; i < 127; i++) s_add("z\n");       /* 合計 128 行 */
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1"), "20i 128 行ちょうどは通る");
    check(!out_has("too many lines"), "20j 同上: 断らない");

    /* --- 読み込み上限 (32767B) を超えるファイル ------------------------ */
    /*  "mk1\n" (4) + "#\n" * 16384 (32768) = 32772 > 32767 */
    fresh();
    bigscript_build("mk1\n", "#\n", 16384);
    file_add("/big.sh", g_bigscript);
    execute_command("source /big.sh");
    check(!ran("mk1"), "20k 読み切れないスクリプトは 1 行も実行しない");
    check(out_has("too large"), "20l 読み切れなかったことを報せる");

    /*  ちょうど 32767B は通る (裏)。"mk1\n" (4) + "#\n" * 16381 (32762)
     *  + "#" (1) = 32767 */
    fresh();
    i = bigscript_build("mk1\n", "#\n", 16381);
    g_bigscript[i++] = '#';
    g_bigscript[i] = '\0';
    check(i == 32767, "20m 反例は 32767 バイトちょうど");
    file_add("/big.sh", g_bigscript);
    execute_command("source /big.sh");
    check(ran("mk1"), "20n 32767 バイトちょうどは通る");
    check(!out_has("too large"), "20o 同上: 断らない");

    /* --- 入れ子の source: 内側が断ったら外側も打ち切る ----------------- */
    fresh();
    s_begin(0);
    s_add("mk1\n");
    s_add("echo ");  s_run(251);  s_add("\n");
    s_begin(1);
    s_add("source /bad.sh\n");
    s_add("mk2\n");
    file_add("/bad.sh", s_body(0));
    file_add("/outer.sh", s_body(1));
    execute_command("source /outer.sh");
    check(!ran("mk1"), "20p 入れ子: 内側は 1 行も実行しない");
    check(!ran("mk2"), "20q 入れ子: 外側の後続行も実行しない");

    /* --- 対話では次の行が動く (誤発火の裏) ----------------------------- */
    fresh();
    s_begin(0);
    s_add("echo ");  s_run(251);  s_add("\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    execute_command("mk3");
    check(ran("mk3"), "20r 対話: 断った source の次の行は今までどおり");

    /* --- 起動スクリプトで断っても起動を止めない (票 R2) --------------- */
    /*  script_load で断ると script_exec を通らないので、印を下ろすのは
     *  script_source_profile の仕事。残したままだと起動後の 1 行目が
     *  巻き添えで捨てられる。 */
    fresh();
    s_begin(0);
    s_add("mkprof1\n");
    s_add("echo ");  s_run(251);  s_add("\n");
    file_add("/etc/profile", s_body(0));
    script_source_profile("/etc/profile");
    check(!ran("mkprof1"), "20s 起動スクリプトも切れていれば実行しない");
    check(sh_refused_flag == 0, "20t 起動スクリプトの断りは印を残さない (R2)");
}

/* ========================================================================
 *  21. U9 — set / export / ask は切って登録しない (T8 / T18)
 *
 *  走った窓は「env の一覧にその名前が出るか」。切って登録していれば
 *  先頭 31 文字の名前が現れる。
 * ======================================================================== */
static void case_env_set_refuses(void)
{
    report("21 U9: set / export / ask の名前 32 / 値 256 (T8)\n");

    /* --- 名前が 32 文字 → 登録しない ---------------------------------- */
    fresh();
    line_reset();
    line_add("set ");
    line_add_run('n', 32);
    line_add("=v");
    execute_command(g_line);
    check(refused_msg("set: variable name"), "21a 名前 32 文字を断る");
    fresh();
    execute_command("env");
    check(!out_has("nnnnnnnnnnnnnnnnnnnnnnnnnnnnnnn"),
          "21b 切った名前で登録していない (31 文字の n が出ない)");

    /* 31 文字ちょうどは通る (裏) */
    fresh();
    line_reset();
    line_add("set ");
    line_add_run('m', 31);
    line_add("=v31");
    execute_command(g_line);
    check(!out_has("too long"), "21c 名前 31 文字は断らない");
    fresh();
    execute_command("env");
    check(out_has("mmmmmmmmmmmmmmmmmmmmmmmmmmmmmmm=v31"),
          "21d 名前 31 文字はそのまま登録される");

    /* --- 値が 256 文字 → 登録しない ----------------------------------- */
    fresh();
    line_reset();
    line_add("set V256=");
    line_add_run('a', 256);
    execute_command(g_line);
    check(refused_msg("set: variable value"), "21e 値 256 文字を断る");
    fresh();
    execute_command("env");
    check(!out_has("V256="), "21f 切った値で登録していない");

    /* 255 文字ちょうどは通る (裏) */
    fresh();
    line_reset();
    line_add("set V255=");
    line_add_run('a', 255);
    execute_command(g_line);
    check(!out_has("too long"), "21g 値 255 文字は断らない");
    fresh();
    execute_command("env");
    check(out_has("V255="), "21h 値 255 文字は登録される");

    /* --- `set NAME VALUE` 形 (スペース区切り) -------------------------- */
    fresh();
    line_reset();
    line_add("set VSP ");
    line_add_run('b', 256);
    execute_command(g_line);
    check(refused_msg("set: variable value"), "21i set NAME VALUE 形も断る");
    fresh();
    execute_command("env");
    check(!out_has("VSP="), "21j 同上: 登録していない");

    /* --- export も同じ登録口を通る ------------------------------------ */
    fresh();
    line_reset();
    line_add("export VEX ");
    line_add_run('c', 256);
    execute_command(g_line);
    check(refused_msg("set: variable value"), "21k export も断る");
    fresh();
    execute_command("env");
    check(!out_has("VEX="), "21l 同上: 登録していない");

    /* --- ask: 255 文字目以降を打鍵したら登録しない (T18) --------------- */
    fresh();
    keys_set_eof(0x0D);          /* ask は ENTER でしか抜けない */
    key_push_run('x', 255);
    key_push(0x0D);
    execute_command("ask p ASKV");
    check(refused_msg("ask: input"), "21m ask: 255 文字目の打鍵を断る");
    fresh();
    execute_command("env");
    check(!out_has("ASKV="), "21n ask: 切れた値を登録していない");

    /* 254 文字ちょうどは通る (裏) */
    fresh();
    keys_set_eof(0x0D);
    key_push_run('y', 254);
    key_push(0x0D);
    execute_command("ask p ASKW");
    check(!out_has("too long"), "21o ask: 254 文字は断らない");
    fresh();
    execute_command("env");
    check(out_has("ASKW=yyy"), "21p ask: 254 文字は登録される");

    /* --- ask のプロンプトが 255 文字 → 訊かずに断る -------------------- */
    fresh();
    keys_set_eof(0x0D);
    key_push(0x0D);
    line_reset();
    line_add("ask ");
    line_add_run('p', 255);
    line_add(" ASKP");
    execute_command(g_line);
    check(refused_msg("ask: prompt"), "21q ask: 255 文字のプロンプトを断る");
    fresh();
    execute_command("env");
    check(!out_has("ASKP="), "21r ask: プロンプトを断ったら訊かない");

    /* --- スクリプト中なら後続行も実行しない / 対話は続く --------------- */
    {
        int k;
        fresh();
        s_begin(0);
        s_add("set ");
        for (k = 0; k < 32; k++) s_add("n");
        s_add("=v\n");
        s_add("mknext\n");
        file_add("/t.sh", s_body(0));
        execute_command("source /t.sh");
        check(!ran("mknext"), "21s スクリプト: 断ったら後続行を実行しない");
    }

    fresh();
    line_reset();
    line_add("set ");
    line_add_run('n', 32);
    line_add("=v");
    execute_command(g_line);
    execute_command("mkafter");
    check(ran("mkafter"), "21t 対話: 断った次の行は今までどおり");
}

/* ========================================================================
 *  22. U10 — ${NAME} / $NAME の名前が 32 文字以上 (T9)
 *
 *  以前は 31 文字で打ち切り、残り (と `}`) をリテラルとして素通しした。
 *  「展開されない文字列がコマンド行に混ざる」= 切り詰め。
 * ======================================================================== */
static void case_env_expand_name(void)
{
    report("22 U10: ${32 文字以上} と $32 文字以上 (T9)\n");

    /* --- ブレース付き --------------------------------------------------- */
    fresh();
    line_reset();
    line_add("echo ${");
    line_add_run('N', 32);
    line_add("}TAIL");
    execute_command(g_line);
    check(refused_msg("sh: variable name"), "22a ${32 文字} を断る");
    check(!out_has("TAIL"), "22b 素通りさせない (残りを実行しない)");

    /* --- 非ブレース ----------------------------------------------------- */
    fresh();
    line_reset();
    line_add("echo $");
    line_add_run('N', 32);
    line_add(" TAIL");
    execute_command(g_line);
    check(refused_msg("sh: variable name"), "22c 裸の $32 文字も断る");
    check(!out_has("TAIL"), "22d 同上: 素通りさせない");

    /* --- 31 文字ちょうどは展開でき、`}` も漏れない (裏) ---------------- */
    fresh();
    line_reset();
    line_add("set ");
    line_add_run('N', 31);
    line_add("=ok31");
    execute_command(g_line);
    fresh();
    line_reset();
    line_add("echo [${");
    line_add_run('N', 31);
    line_add("}]");
    execute_command(g_line);
    check(out_has("[ok31]"),
          "22e ${31 文字} は展開され、閉じ `}` も食べる (漏れない)");
    check(!out_has("too long"), "22f 同上: 断らない");

    /* 裸の $NAME は区切り (空白 / `/` / `.` / `:` / `$`) までが名前。
     * `]` は区切りではないので `$<31 文字>]` は「32 文字目がある名前」と
     * 見なされて断られる — これは切り詰めの拒否として**正しい**
     * (以前は 31 文字で打ち切って `]` をリテラルに漏らしていた)。
     * ここで見るのは区切りで終わる正しい形。 */
    fresh();
    line_reset();
    line_add("echo ");
    line_add_run('$', 1);
    line_add_run('N', 31);
    line_add(" tail31");
    execute_command(g_line);
    check(out_has("ok31") && out_has("tail31"),
          "22g 裸の $31 文字も展開され、後ろの語は残る");
    check(!out_has("too long"), "22g2 同上: 断らない");

    fresh();
    line_reset();
    line_add("echo [$");
    line_add_run('N', 31);
    line_add("]");
    execute_command(g_line);
    check(refused_msg("sh: variable name"),
          "22g3 裸の $31 文字 + 区切りでない文字は「32 文字目」なので断る");

    /* --- 未定義の 31 文字は空に展開 (従来どおり) ----------------------- */
    fresh();
    line_reset();
    line_add("echo [${");
    line_add_run('U', 31);
    line_add("}]");
    execute_command(g_line);
    check(out_has("[]"), "22h 未定義の 31 文字は空に展開 (従来どおり)");

    /* --- スクリプト中なら後続行も実行しない / 対話は続く --------------- */
    {
        int k;
        fresh();
        s_begin(0);
        s_add("echo ${");
        for (k = 0; k < 32; k++) s_add("N");
        s_add("}\n");
        s_add("mknext\n");
        file_add("/t.sh", s_body(0));
        execute_command("source /t.sh");
        check(!ran("mknext"), "22i スクリプト: 断ったら後続行を実行しない");
    }

    fresh();
    line_reset();
    line_add("echo ${");
    line_add_run('N', 32);
    line_add("}");
    execute_command(g_line);
    execute_command("mkafter");
    check(ran("mkafter"), "22j 対話: 断った次の行は今までどおり");
}


/* ========================================================================
 *  23. U11 / U18 — rshell (T10 / T19 / T24)
 *
 *  **rshell.c を実物のまま通す**。段 3 までは「rshell.c は取り込んでいない」
 *  と記録していたので、EOT も読み捨ても試験で踏めていなかった。
 *  窓は 3 つ:
 *    - ran()              切れた接頭辞を実行していないか
 *    - ser_count(0x04)    EOT を返したか (返さないと /api/cmd が全滅する)
 *    - g_open_calls /
 *      opened_path()      切れた別のパスを作りにいっていないか
 * ======================================================================== */

/* rshell へ「1 行 + 改行」を流す台本を積む */
static void rs_line(const char *head, char pad, int pad_n)
{
    key_push_str(head);
    key_push_run(pad, pad_n);
    key_push('\n');
}

static void case_rshell_line(void)
{
    char *av[4];

    fresh();
    av[0] = "rshell";
    rshell_active_calls=0;
    check(cmd_rshell(1, av) == SH_STATUS_ERROR, "sh.bin rshell rejected");
    check(out_has("resident shell only") && ser_count(0x04) == 0,
          "sh.bin rshell rejection visible without EOT");
    check(!rshell_active_calls, "sh.bin rshell does not try to claim serial output");
    fresh(); rshell_active_calls=0;
    key_push('T'); key_push(0x1B);
    check(cmd_terminal(1, av)==0 && ser_count('T')==1 && !rshell_active_calls,
          "sh.bin terminal sends directly without rshell activation");
    /* Remaining cases exercise the resident implementation directly. */
    report("23 U11: rshell の 1 行上限 126 と EOT (T10)\n");

    /* --- 127 文字 → 実行しない。EOT は返す。 -------------------------- */
    fresh();
    rs_line("mk10 ", 'a', 122);          /* 5 + 122 = 127 文字 */
    av[0] = "rshell";
    cmd_rshell_resident(1, av);
    check(!ran("mk10"), "23a 127 文字の行は接頭辞も実行しない");
    check(refused_msg("rshell: command line"), "23b 上限を報告する");
    check(ser_count(0x04) == 2,
          "23c 断った後も EOT を返す (起動時の 1 つ + 断りの 1 つ)");

    /* 126 文字ちょうどは通る (誤発火の裏) */
    fresh();
    rs_line("mk11 ", 'a', 121);          /* 5 + 121 = 126 文字 */
    cmd_rshell_resident(1, av);
    check(ran("mk11"), "23d 126 文字ちょうどは今までどおり実行する");
    check(!out_has("too long"), "23e 同上: 断らない");
    check(ser_count(0x04) == 2, "23f 同上: EOT も今までどおり 1 つ");

    /* --- 断った行の**残り**が次の入力にならない ----------------------- */
    /*  127 文字の行の 127 文字目以降を読み捨てていなければ、残りが次の
     *  コマンドとして実行される。次に正しい行を流して両方を見る。 */
    fresh();
    rs_line("mk12 ", 'b', 122);          /* 127 文字 — 断られる */
    rs_line("mk13", 'c', 0);             /* 次の行 — 通る */
    cmd_rshell_resident(1, av);
    check(!ran("mk12"), "23g 断った行は実行しない");
    check(ran("mk13"), "23h 次の行は正常に動く");
    check(ser_count(0x04) == 3, "23i EOT は 起動 + 断り + 次の行 の 3 つ");

    /* --- U18 / T19: push / recv の host: パス ------------------------- */
    report("23 U18: push / recv の host: パス (T19)\n");

    fresh();
    line_reset();
    line_add("host:");
    line_add_run('p', 255);              /* /host + '/' + 255 = 261 > 255 */
    av[0] = "push";
    av[1] = "/local.txt";
    av[2] = g_line;
    cmd_push(3, av);
    check(refused_msg("push: host path"), "23j push: 収まらない host: を断る");
    check(g_open_calls == 0, "23k push: 切れた別のパスを開きにいかない");

    fresh();
    line_reset();
    line_add("host:");
    line_add_run('p', 255);
    av[0] = "recv";
    av[1] = g_line;
    cmd_recv(2, av);
    check(refused_msg("recv: host path"), "23l recv: 収まらない host: を断る");
    check(g_open_calls == 0, "23m recv: 切れた別のパスを開きにいかない");

    /* 収まる長さは今までどおり (裏)。/host + 250 = 255 */
    fresh();
    line_reset();
    line_add("host:/");
    line_add_run('p', 249);
    av[0] = "push";
    av[1] = "/nosuch.txt";
    av[2] = g_line;
    cmd_push(3, av);
    check(!out_has("too long"), "23n 収まる host: は断らない");
    check(g_open_calls == 1, "23o 同上: ローカル側を開きにいく");

    /* --- T24: 4KB を超えるファイルも読み切ってから送る ---------------- */
    fresh();
    bigscript_build("", "a", 10000);
    file_add("/local.big", g_bigscript);
    av[0] = "push";
    av[1] = "/local.big";
    av[2] = "host:/up.big";
    cmd_push(3, av);
    check(g_data_written == 10000,
          "23p push: 4KB を超えるファイルを全部送る (先頭 4KB だけにしない)");
    check(out_has("Sent"), "23q push: 成功として報告する");

    /* --- 抜ける 3 経路の EOT (票 §2-2 の残り) -------------------------
     *
     *  rshell の抜け口は 3 つある。ホストが待っているのは「行のバイトを
     *  渡したのに EOT が返っていない」ときだけなので、そこだけ閉じて
     *  **待っていないところで足さない** (足すと 1 コマンドに EOT が 2 つ
     *  出て、/api/cmd が次のコマンドの終端と取り違える)。
     *    - ホストの `exit`   → 待っている  → 返す
     *    - 行の途中の ESC    → 待っている  → 返す
     *    - 待ち中の ESC      → 返し終えている → 返さない
     */
    report("23 U11: rshell を抜ける 3 経路の EOT (票 §2-2)\n");

    /* ホストの `exit`: EOT はきっかり 1 つ (0 個だとホストが 15 秒待つ) */
    fresh();
    rs_line("exit", ' ', 0);
    rs_line("mk14", ' ', 0);             /* exit の後ろ — 読まれてはいけない */
    av[0] = "rshell";
    cmd_rshell_resident(1, av);
    check(ser_count(0x04) == 2,
          "23r exit にも EOT を返す (起動の 1 つ + exit の 1 つ)");
    check(!ran("mk14"), "23s exit の後ろの行は実行しない");

    /* 待ち中の ESC: 直前の行の EOT は返し終えている → 足さない */
    fresh();
    cmd_rshell_resident(1, av);                   /* 台本なし = いきなり ESC */
    check(ser_count(0x04) == 1,
          "23t 待ち中の ESC では EOT を足さない (起動の 1 つだけ)");

    fresh();
    rs_line("mk15", ' ', 0);             /* 1 行実行してから台本切れ = ESC */
    cmd_rshell_resident(1, av);
    check(ran("mk15"), "23u 普通の行は今までどおり実行する");
    check(ser_count(0x04) == 2,
          "23v 同上: EOT は 起動 + その行 の 2 つ (ESC で 3 つにしない)");

    /* 行の途中の ESC: 改行を積まない = 受けかけのまま ESC が来る */
    fresh();
    key_push_str("mk16");
    cmd_rshell_resident(1, av);
    check(!ran("mk16"), "23w 受けかけの行は実行しない");
    check(ser_count(0x04) == 2,
          "23x 受けかけで ESC なら EOT を返す (ホストを待たせない)");
}

/* ========================================================================
 *  29. U11 — rshell は断りの印を行をまたいで持ち越さない
 *
 *  **execute_command("rshell") で回す**ところが肝。rshell はシェルの組み込み
 *  コマンドなので、実機では rshell のループの中の execute_command は必ず
 *  入れ子 (g_exec_depth >= 1) になり、main.c の「いちばん外側だけ印を消す」
 *  が効かない。案内どおり cmd_rshell を直に呼ぶ case 23 は深さ 0 のままで、
 *  2026-09-16 の退行 (一度断ると以降どのスクリプトも 1 行目で打ち切られる)
 *  をすり抜けていた。深さを作らない検査はこの穴を見られない。
 * ======================================================================== */
/* 実機の rshell は execute_command("rshell") の中で走るので、そのループの
 * 中の execute_command は **必ず入れ子** (g_exec_depth >= 1) になる。この
 * 試験は -DSHELL_AS_APP で組むため sh_is_cui_only (sh_exec.inc) が `rshell`
 * を表の手前で弾き、同じ姿を execute_command からは作れない。そこで
 * **深さだけ**常駐版に合わせて cmd_rshell を回す。 */
static void rshell_nested(void)
{
    char *av[2];
    av[0] = "rshell";
    av[1] = (char *)0;
    g_exec_depth++;
    cmd_rshell_resident(1, av);
    g_exec_depth--;
}

static void case_rshell_nested_refuse_flag(void)
{
    report("29 U11: rshell の断りの印は行をまたがない (入れ子の姿)\n");

    /* 深さを作る理由を試験の中に残す — 表から踏めないことを先に確かめる */
    fresh();
    execute_command("rshell");
    check(out_has("sh: cui only"),
          "29a -DSHELL_AS_APP では表から rshell を踏めない (深さを作る理由)");

    /* --- 行そのものが断られた次の行 ---------------------------------- */
    fresh();
    /* 2 行ある。印が持ち越されると **1 行目を出した直後に** 打ち切られ、
     * TWOMARK が出ない (実機で見えた姿そのもの)。 */
    file_add("/one.sh", "echo ONEMARK\necho TWOMARK\n");
    key_push_str("set ");
    key_push_run('n', 32);               /* 名前 32 文字 = 断る */
    key_push_str("=v");
    key_push('\n');
    key_push_str("source /one.sh");      /* 1 行だけのスクリプト */
    key_push('\n');
    rshell_nested();
    check(refused_msg("set: variable name"),
          "29b 入れ子でも断りは今までどおり出る");
    check(out_has("TWOMARK"),
          "29c 断りの次の行のスクリプトは最後まで走る");
    check(!out_has("script: aborted"),
          "29d 印を持ち越さない (前の行の断りで打ち切らない)");

    /* --- スクリプトの中の断りは打ち切る。ただし次の行へは残さない ---- */
    fresh();
    file_add("/one.sh", "echo ONEMARK\necho TWOMARK\n");
    s_begin(0);
    s_add("set B=");  s_run(150);  s_add("\n");
    s_add("if ${B}${B} == b markinner\n");   /* 左辺 300 文字 > 255 */
    s_add("markafter\n");
    file_add("/bad.sh", s_body(0));
    key_push_str("source /bad.sh");
    key_push('\n');
    key_push_str("source /one.sh");
    key_push('\n');
    key_push_str("mk17");
    key_push('\n');
    rshell_nested();
    check(refused_msg("if: left value"), "29e スクリプト中の断りは今までどおり出る");
    check(!ran("markafter"), "29f 断ったスクリプトはそこで打ち切る");
    check(out_has("script: aborted"), "29g 打ち切りを報告する");
    check(out_has("TWOMARK"), "29h 次の行のスクリプトは最後まで走る");
    check(out_count("script: aborted") == 1,
          "29i 打ち切りは断ったスクリプトの 1 回だけ (次の行に巻き添えを出さない)");
    check(ran("mk17"), "29j その次の普通のコマンドも走る");
}

/* ========================================================================
 *  24. U14 / U15 / U19 — ui.c の履歴・行編集・HOME (T14 / T15 / T20)
 *
 *  **ui.c を実物のまま通す**。shell_run は端末のキーを握るので、キー源
 *  (kbd_getkey) を台本に差し替えて動かす。抜けるのは `exit` の印。
 *  窓は hist_count / hist_buf (履歴に入ったか) と ran() (実行したか)。
 * ======================================================================== */

/* shell_run を 1 回動かす前の後始末 */
static void ui_reset(void)
{
    hist_count = 0;
    hist_idx = 0;
    hist_dirty = 0;
    sh_exit_flag = 0;
}

/* 台本の末尾に `exit` + ENTER を積む (これが無いと抜けない) */
static void ui_exit(void)
{
    key_push_str("exit");
    key_push(0x0D);
}

static void case_ui_history_and_line(void)
{
    report("24 U14: 切れた行を履歴に入れない (T14)\n");

    /* --- 512 バイト以上の行は履歴に入らない (実行はする) -------------- */
    fresh();
    ui_reset();
    key_push_str("echo MK20OUT ");
    key_push_run('a', 600);              /* 613 バイト — 実行はされる */
    key_push(0x0D);
    ui_exit();
    shell_run();
    /* 打鍵はそのまま画面へエコーされるので、出力に 1 回は必ず出る。
     * **2 回目**が「echo が実際に走った」証拠 (外部コマンド名では T3 の
     * 510 バイト上限が先に断つので窓にならない)。 */
    check(out_count("MK20OUT") == 2,
          "24a 512 バイト超の行は今までどおり**実行される**");
    check(hist_count == 1 && str_eq(hist_buf[0], "exit"),
          "24b 512 バイト超の行は履歴に入らない (入るのは exit だけ)");

    /* 511 バイトちょうどは入る (裏) */
    fresh();
    ui_reset();
    key_push_str("echo ");
    key_push_run('a', 506);              /* 511 バイトちょうど */
    key_push(0x0D);
    ui_exit();
    shell_run();
    check(hist_count == 2, "24c 511 バイトちょうどは履歴に入る");
    check(str_eq(hist_buf[1], "exit"), "24d 同上: 2 件目は exit");

    /* --- hist_load: 切れた行を読み込まない ---------------------------- */
    fresh();
    ui_reset();
    env_set("HOME", "/h");
    s_begin(0);
    s_add("short1\n");
    s_run(600);                          /* 600 バイトの行 */
    s_add("\nshort2\n");
    file_add("/h/.sh_history", s_body(0));
    hist_load();
    check(hist_count == 2, "24e 512 バイト超の行は読み込まない");
    check(str_eq(hist_buf[0], "short1") && str_eq(hist_buf[1], "short2"),
          "24f 同上: 前後の短い行はそのまま読む");

    /* 読み切れなかったファイルの末尾行も入れない。
     * "ab\n" * 2730 = 8190B + "LONGTAIL\n" → 8191B しか読めず末尾が切れる。 */
    fresh();
    ui_reset();
    env_set("HOME", "/h");
    bigscript_build("", "ab\n", 2730);
    {
        int n = 8190;
        const char *t = "LONGTAIL\n";
        int k;
        for (k = 0; t[k]; k++) g_bigscript[n++] = t[k];
        g_bigscript[n] = '\0';
    }
    file_add("/h/.sh_history", g_bigscript);
    hist_load();
    check(hist_count == 2730,
          "24g 読み切れなかった末尾の行は履歴に入れない (2731 にならない)");

    /* --- U15: 行編集が打鍵を捨てたら ENTER で断る (T15) ---------------- */
    report("24 U15: 4092 バイトで捨てたら行ごと断る (T15)\n");

    fresh();
    ui_reset();
    key_push_str("echo MK22OUT ");
    key_push_run('a', 4090);             /* 4103 バイト — 4092 で捨てる */
    key_push(0x0D);
    ui_exit();
    shell_run();
    check(out_count("MK22OUT") == 1,
          "24h 打鍵を捨てた行は**実行しない** (エコーの 1 回だけ)");
    check(out_has("sh: line too long"), "24i 断りを 1 行出す");
    check(hist_count == 1 && str_eq(hist_buf[0], "exit"),
          "24j 打鍵を捨てた行は履歴にも入れない");

    /* 4092 バイトちょうどは通る (裏) */
    fresh();
    ui_reset();
    key_push_str("echo MK23OUT ");
    key_push_run('a', 4079);             /* 4092 バイトちょうど */
    key_push(0x0D);
    ui_exit();
    shell_run();
    check(out_count("MK23OUT") == 2,
          "24k 4092 バイトちょうどは今までどおり実行する");
    check(!out_has("sh: line too long"), "24l 同上: 断らない");

    /* ESC で印が消える */
    fresh();
    ui_reset();
    key_push_str("echo MK24OUT ");
    key_push_run('a', 4090);
    key_push(0x1B);                      /* ESC — 行を捨てる */
    key_push_str("mk25");
    key_push(0x0D);
    ui_exit();
    shell_run();
    check(out_count("MK24OUT") == 1, "24m ESC で捨てた行は実行されない");
    check(ran("mk25"), "24n ESC の後の行は普通に実行する");
    check(!out_has("sh: line too long"), "24o ESC で印が消えている");

    /* --- U19: HOME が長いと履歴 / profile を使わない (T20) ------------ */
    report("24 U19: HOME が長いときの履歴 / .profile (T20)\n");

    fresh();
    ui_reset();
    line_reset();
    line_add("/");
    line_add_run('h', 240);              /* 241 バイト — 240 に収まらない */
    env_set("HOME", g_line);
    hist_load();
    check(refused_msg("sh: $HOME for history path"),
          "24p HOME が長いと履歴のパスを組み立てずに断る");
    check(g_open_calls == 0, "24q 同上: 切れた別のパスを開かない");

    /* 239 バイトは通る (裏) */
    fresh();
    ui_reset();
    line_reset();
    line_add("/");
    line_add_run('h', 239);              /* 240 バイト = 上限ちょうど */
    env_set("HOME", g_line);
    hist_load();
    check(!out_has("too long"), "24r HOME 240 バイトちょうどは断らない");
    check(g_open_calls == 1, "24s 同上: 履歴ファイルを開きにいく");

    /* .profile: 切れた別ディレクトリの .profile を読まない */
    fresh();
    ui_reset();
    line_reset();
    line_add("/");
    line_add_run('h', 249);              /* 250 バイト */
    env_set("HOME", g_line);
    {
        /* 直す前に読まれていた綴り = HOME の先頭 244 バイト + "/.profile" */
        static char cut_path[PATH_MAX_LEN];
        int k;
        for (k = 0; k < 244; k++) cut_path[k] = g_line[k];
        cut_path[k] = '\0';
        strcat(cut_path, "/.profile");
        file_add(cut_path, "mkprof\n");
        key_push_str("exit");
        key_push(0x0D);
        shell_run();
        check(!ran("mkprof"),
              "24t HOME が長いとき切れた別ディレクトリの .profile を読まない");
        check(!opened_path(cut_path), "24u 同上: そもそも開きにいかない");
    }
    env_set("HOME", SYS_DEFAULT_HOME);
}

/* ========================================================================
 *  25. U16 — filer (T16)
 *
 *  **cmd_filer.c を実物のまま通す**。描画 (fldraw_*) は TVRAM を直に叩く
 *  ので、この試験では空実装を置いてある (filer_draw.c は取り込まない)。
 *  窓は fl_state.count / fl_state.dropped と g_launch_count (起動したか)。
 * ======================================================================== */
static void case_filer_names(void)
{
    static char long_name[80];
    static char deep_dir[PATH_MAX_LEN];
    int i;

    report("25 U16: filer が切った名前で別のものを開かない (T16)\n");

    /* --- 64 バイト以上の名前は表に載せず件数だけ ---------------------- */
    for (i = 0; i < 70; i++) long_name[i] = 'L';
    long_name[70] = '\0';

    /* fl_scan_dir は先頭に `..` を足す (ルート以外)。以下の count は
     * その 1 行を含む。 */
    fresh();
    dir_set("/d");
    dir_add("short.bin");
    dir_add(long_name);
    fl_init("/d");
    check(fl_state.count == 2, "25a 64 バイト以上の名前は表に載せない (.. + 1 件)");
    check(fl_state.dropped == 1, "25b 載せなかった件数を数える");
    check(str_eq(fl_state.entries[1].name, "short.bin"),
          "25c 収まる名前はそのまま載る");

    /* 63 バイトちょうどは載る (裏) */
    long_name[63] = '\0';
    fresh();
    dir_set("/d");
    dir_add(long_name);
    fl_init("/d");
    check(fl_state.count == 2 && fl_state.dropped == 0,
          "25d 63 バイトちょうどは載る");
    check(str_eq(fl_state.entries[1].name, long_name),
          "25e 同上: 名前が切れていない");

    /* --- fl_path_join が溢れたら起動しない ---------------------------- */
    deep_dir[0] = '/';
    for (i = 1; i < 250; i++) deep_dir[i] = 'd';
    deep_dir[250] = '\0';

    fresh();
    dir_set(deep_dir);
    dir_add("prog.bin");
    fl_init(deep_dir);
    check(fl_state.count == 2, "25f 深いディレクトリでも名前自体は載る");
    fl_state.cursor = 1;                 /* 0 は `..` */
    fl_action_enter();
    check(g_launch_count == 0,
          "25g 連結が溢れたら**切れた別のパスで起動しない**");
    check(g_popup_count == 1, "25h 同上: 断りを画面に出す");

    /* 収まるなら今までどおり起動する (裏) */
    fresh();
    dir_set("/d");
    dir_add("prog.bin");
    fl_init("/d");
    fl_state.cursor = 1;                 /* 0 は `..` */
    fl_action_enter();
    check(g_launch_count == 1, "25i 収まるパスは今までどおり起動する");
    check(str_eq(g_launch_last, "/d/prog.bin"), "25j 同上: 綴りも正しい");

    /* --- ft_load: 読み切れない /etc/filetypes は使わない -------------- */
    fresh();
    dir_set("/d");
    dir_add("a.txt");
    /* "#pad\n" * 1637 = 8185B のあと ".txt=mkassoc\n" (13B) = 8198B。
     * 8191B しか読めないので、直す前は最後の行が ".txt=m" に切れて
     * **別のコマンド** (`m`) に関連付いていた。 */
    bigscript_build("", "#pad\n", 1637);
    {
        int n = 8185;
        const char *t = ".txt=mkassoc\n";
        int k;
        for (k = 0; t[k]; k++) g_bigscript[n++] = t[k];
        g_bigscript[n] = '\0';
    }
    file_add("/etc/filetypes", g_bigscript);
    fl_init("/d");
    check(ft_count == 0, "25k 読み切れない filetypes は関連付け表を作らない");
    fl_state.cursor = 1;                 /* 0 は `..` */
    fl_action_enter();
    check(g_launch_count == 0, "25k2 同上: 切れた関連付けで起動しない");
    ft_free();

    /* 収まる filetypes は今までどおり (裏) */
    fresh();
    dir_set("/d");
    dir_add("a.txt");
    file_add("/etc/filetypes", ".txt=mkassoc\n");
    fl_init("/d");
    check(ft_count == 1, "25l 収まる filetypes は今までどおり読む");
    fl_state.cursor = 1;                 /* 0 は `..` */
    fl_action_enter();
    check(str_eq(g_launch_last, "mkassoc /d/a.txt"),
          "25m 同上: 関連付けで起動する");
    ft_free();

    /* --- 8.3 の短い名前 /etc/filetype は FD のときだけ (Codex 実装レビュー) --- */
    /* HDD: 正規名が無ければ失敗のまま。残っている短い名前を掴まない */
    fresh();
    dir_set("/d");
    dir_add("a.txt");
    file_add("/etc/filetype", ".txt=shortname\n");
    fl_init("/d");
    check(ft_count == 0, "25n HDD では /etc/filetype (短い名前) を読まない");
    fl_state.cursor = 1;
    fl_action_enter();
    check(g_launch_count == 0, "25n2 同上: 短い名前の関連付けで起動しない");
    ft_free();

    /* FD: 正規名が無ければ短い名前を読む (裏) */
    fresh();
    g_root_dev = FD_ROOT_DEV;
    dir_set("/d");
    dir_add("a.txt");
    file_add("/etc/filetype", ".txt=shortname\n");
    fl_init("/d");
    check(ft_count == 1, "25o FD では /etc/filetype を読む");
    fl_state.cursor = 1;
    fl_action_enter();
    check(str_eq(g_launch_last, "shortname /d/a.txt"), "25o2 同上: 関連付けで起動する");
    ft_free();

    /* FD でも正規名があれば正規名 (短い名前より先) */
    fresh();
    g_root_dev = FD_ROOT_DEV;
    dir_set("/d");
    dir_add("a.txt");
    file_add("/etc/filetypes", ".txt=longname\n");
    file_add("/etc/filetype", ".txt=shortname\n");
    fl_init("/d");
    fl_state.cursor = 1;
    fl_action_enter();
    check(str_eq(g_launch_last, "longname /d/a.txt"), "25p FD でも正規名を先に読む");
    ft_free();

    /* FD ルートでも /etc に別のマウント (HDD の ext2) が載っていれば短い名前を
     * 読まない (Codex 実装レビュー往復 2) */
    fresh();
    g_root_dev = FD_ROOT_DEV;
    g_dev_etc = 2;
    dir_set("/d");
    dir_add("a.txt");
    file_add("/etc/filetype", ".txt=shortname\n");
    fl_init("/d");
    check(ft_count == 0, "25q FD ルート + /etc に ext2 をマウント: 短い名前を読まない");
    fl_state.cursor = 1;
    fl_action_enter();
    check(g_launch_count == 0, "25q2 同上: 短い名前の関連付けで起動しない");
    ft_free();
}

/* ========================================================================
 *  26. U20 — cfg_set_key (T22)
 *
 *  /etc/system.cfg を 1023 バイトで読んで**切れたまま書き戻す**と、
 *  1KB を超える設定が消える。窓は g_data_writes (書き戻したか)。
 * ======================================================================== */
static void case_cfg_set_key(void)
{
    report("26 U20: /etc/system.cfg を切れたまま書き戻さない (T22)\n");

    /* --- 1023 バイトを超える system.cfg → 書き戻さない ---------------- */
    /* `os32gui` は sh.bin では sh_is_cui_only が先に断つので、
     * 書き戻しの本体 (cfg_set_key) を直に呼ぶ。 */
    fresh();
    bigscript_build("GUI=0\n", "K=v\n", 400);   /* 6 + 1600 = 1606B */
    file_add("/etc/system.cfg", g_bigscript);
    check(cfg_set_key("GUI", "1") == -1, "26a 読み切れない設定を断る");
    check(refused_msg("system.cfg"), "26b 何が上限を超えたかを報告する");
    check(g_data_writes == 0, "26c 切れたまま書き戻さない (1 度も書かない)");

    /* --- 収まる system.cfg は今までどおり書き戻す (裏) ---------------- */
    fresh();
    file_add("/etc/system.cfg", "GUI=0\nGFXMODE=1\n");
    check(cfg_set_key("GUI", "1") == 0, "26d 収まる設定は今までどおり書き戻す");
    check(!out_has("too long"), "26e 同上: 断らない");
    check(g_data_writes == 1, "26f 同上: 書き込みは 1 回");
}

/* ========================================================================
 *  27. T12 — `if` が組み立てたコマンド行が溢れたら実行しない
 *
 *  到達するのは glob 展開で argv が伸びたとき。ここでは同じ経路
 *  (join_args) を素の語で溢れさせて見る。
 * ======================================================================== */
/* glob で伸びる argv を作るための長い名前 (贋ディレクトリへ流す) */
static char g_globnames[DIRENT_MAX][256];

static void globnames_build(int n, int len)
{
    int i, k;
    for (i = 0; i < n && i < DIRENT_MAX; i++) {
        for (k = 0; k < len; k++) g_globnames[i][k] = 'L';
        g_globnames[i][0] = 'L';
        g_globnames[i][len - 2] = (char)('a' + (i / 10));
        g_globnames[i][len - 1] = (char)('0' + (i % 10));
        g_globnames[i][len] = '\0';
        dir_add(g_globnames[i]);
    }
}

static void case_if_join_refuses(void)
{
    report("27 T12: if の組み立てが溢れたら実行しない\n");

    /* 票のとおり、この経路へ届くのは **glob で argv が伸びたとき** だけ。
     * 打った行は 20 バイトほどでも、展開後の argv を繋ぎ直すと
     * CMD_BUF_SIZE を超える。 */
    fresh();
    dir_set("/d/");
    globnames_build(20, 250);        /* 20 件 × ("/d/" + 250) = 5060B */
    execute_command("if a == a echo MK30OUT /d/L*");
    check(!out_has("MK30OUT"), "27a 組み立てが溢れたらコマンドを実行しない");
    check(refused_msg("if: command line"), "27b 断りを 1 行出す");

    /* 収まるなら今までどおり実行する (裏) */
    fresh();
    dir_set("/d/");
    globnames_build(10, 250);        /* 10 件 × 253 = 2530B — 収まる */
    execute_command("if a == a echo MK31OUT /d/L*");
    check(out_has("MK31OUT"), "27c 収まる組み立ては今までどおり実行する");
    check(!out_has("too long"), "27d 同上: 断らない");

    /* スクリプト中なら後続行も実行しない */
    fresh();
    dir_set("/d/");
    globnames_build(20, 250);
    s_begin(0);
    s_add("echo MK32PRE\n");        /* この行が出ない = 台本が走っていない */
    s_add("if a == a echo MK32OUT /d/L*\n");
    s_add("mknext\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(out_has("MK32PRE"), "27e スクリプトが実際に走っている (窓)");
    check(!out_has("MK32OUT"), "27f スクリプト: 断った行は実行しない");
    check(!ran("mknext"), "27g スクリプト: 後続行も実行しない");
}


/* ========================================================================
 *  28. T21 / T17 — タブ補完 (ui.c)
 *
 *  補完は画面に出るだけに見えるが、確定した綴りはそのまま行編集の
 *  バッファへ書き込まれ、ENTER で**別のファイルに作用する**。
 *  収まらない候補は補完しない。
 * ======================================================================== */
static void case_tab_completion(void)
{
    static char big_name[200];
    char buf[CMD_BUF_SIZE];
    int i, n;

    report("28 T21: 収まらない補完候補は挿さない (ui.c)\n");

    /* --- コマンド名補完 (.bin を外した名前が name_store[64] に入る) ---- */
    for (i = 0; i < 70; i++) big_name[i] = 'C';
    strncpy(big_name + 70, ".bin", 5);          /* base_len = 70 > 63 */

    fresh();
    env_set("PATH", "/p");
    dir_set("/p");
    dir_add(big_name);
    buf[0] = 'C'; buf[1] = 'C'; buf[2] = '\0';
    n = tab_complete(buf, 2, 0);
    check(n == 2 && str_eq(buf, "CC"),
          "28a 64 バイト以上の候補では補完しない (行は変わらない)");

    /* 63 バイトちょうどの候補は補完する (裏) */
    for (i = 0; i < 63; i++) big_name[i] = 'C';
    strncpy(big_name + 63, ".bin", 5);
    fresh();
    env_set("PATH", "/p");
    dir_set("/p");
    dir_add(big_name);
    buf[0] = 'C'; buf[1] = 'C'; buf[2] = '\0';
    n = tab_complete(buf, 2, 0);
    check(n == 64, "28b 63 バイトちょうどの候補は補完する (名前 63 + 空白)");
    check(buf[62] == 'C' && buf[63] == ' ',
          "28c 同上: 綴りが切れていない");

    /* --- ファイル名補完 (name_store[128]) ------------------------------ */
    for (i = 0; i < 130; i++) big_name[i] = 'F';
    big_name[130] = '\0';

    fresh();
    dir_set(".");
    dir_add(big_name);
    strncpy(buf, "cat FF", 7);
    n = tab_complete(buf, 6, 0);
    check(n == 6 && str_eq(buf, "cat FF"),
          "28d 127 バイト以上のファイル名では補完しない");

    /* 126 バイトちょうどは補完する (裏) */
    big_name[126] = '\0';
    fresh();
    dir_set(".");
    dir_add(big_name);
    strncpy(buf, "cat FF", 7);
    n = tab_complete(buf, 6, 0);
    check(n == 4 + 126 + 1, "28e 126 バイトちょうどは補完する");

    /* --- T17 の ui.c 側: 区切りを見失ったら走査ごと止める --------------
     *  ui.c の取り込みは PATH_MAX_LEN - 1 = 255 なので、区切りを見失うには
     *  **1 項目が 256 バイト以上**要る。PATH は環境変数なので値の上限が
     *  ENV_VALUE_MAX - 1 = 255 で、**今は到達できない** (T25 / T26 と同じ
     *  「将来の地雷」)。守りは入れたが反例は作れないので、ここでは
     *  「普通の PATH は今までどおり」だけを押さえる。 */
    fresh();
    env_set("PATH", "/p");
    dir_set("/p");
    dir_add("zzcmd.bin");
    buf[0] = 'z'; buf[1] = 'z'; buf[2] = '\0';
    n = tab_complete(buf, 2, 0);
    check(n == 6 && str_eq(buf, "zzcmd "), "28f 普通の PATH は今までどおり補完する");
    check(g_ls_calls == 1, "28g 同上: PATH の項目を 1 つ走査する");

    env_set("PATH", SYS_DEFAULT_PATH);
}

/* ========================================================================
 *  29. 継承バグ「source が ESC 以外も食う」 — 行ごとの ESC 監視 (cmd_script.c)
 *
 *  script_exec は **1 行ごと**にキーを見て ESC なら打ち切る。以前はそれを
 *  kbd_trygetkey で見ていたので、ESC 以外の打鍵も**取り出して捨てて**いた:
 *  スクリプト実行中に打った文字が消え、終わった後の入力の先頭が欠ける。
 *  KAPI v54 の kbd_peekkey (覗くだけ) に替え、取り除くのは ESC と分かって
 *  からにした。
 *
 *  窓は監視キューの模型 (wq_*)。wq_takes() が「食った数」で、これが 0 の
 *  ままなのが直った印。ESC の打ち切り (script_abort_flag) は据え置き。
 * ======================================================================== */
static void case_script_esc_watch(void)
{
    report("29 継承バグ: 行ごとの ESC 監視が ESC 以外を食わない\n");

    /* --- 29a〜d: ESC 以外は 1 つも食わない (2 行以上で毎行回る) -------- */
    fresh();
    wq_push('a');
    s_begin(0);
    s_add("mk1\n");
    s_add("mk2\n");
    s_add("mk3\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1") && ran("mk2") && ran("mk3"),
          "29a 3 行とも走る (打鍵は打ち切りにならない)");
    check(!out_has("Script aborted"), "29b 打ち切っていない");
    check(wq_takes() == 0, "29c 1 つも取り出していない (毎行の監視が食わない)");
    check(wq_len() == 1 && wq_at(0) == 'a',
          "29d 打った 'a' がスクリプトの後もキューに残っている");

    /* --- 29e〜g: 複数の打鍵が順序どおり全部残る ----------------------- */
    fresh();
    wq_push('a');
    wq_push('b');
    wq_push('c');
    s_begin(0);
    s_add("mk1\n");
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1") && ran("mk2"), "29e 2 行とも走る");
    check(wq_len() == 3, "29f 3 打鍵とも残る (行数ぶん食わない)");
    check(wq_at(0) == 'a' && wq_at(1) == 'b' && wq_at(2) == 'c',
          "29g 順序が入れ替わっていない");

    /* --- 29h〜k: ESC は今までどおり打ち切る (誤発火の裏) -------------- */
    fresh();
    wq_push(0x1B);
    s_begin(0);
    s_add("mk1\n");
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(!ran("mk1"), "29h ESC: 1 行目の前に打ち切る");
    check(!ran("mk2"), "29i ESC: 後続行も走らない");
    check(out_has("Script aborted"), "29j ESC: 打ち切りを報せる");
    check(wq_takes() == 1 && wq_len() == 0,
          "29k ESC 自身は取り除く (後の行編集へ ESC を残さない)");

    /* --- 29l〜n: ESC の前に別のキーが積まれている場合の順序 -----------
     *  先頭は 'a' なので打ち切らない。**覗くのは先頭だけ**なので、後ろに
     *  ある ESC はこの行では見えない — その ESC は消えたのではなく、
     *  'a' の次に読み手へ順序どおり届く。行ごとに取り出していた以前は
     *  'a' を捨ててから ESC で打ち切っていた (打鍵が消える側)。 */
    fresh();
    wq_push('a');
    wq_push(0x1B);
    s_begin(0);
    s_add("mk1\n");
    s_add("mk2\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1") && ran("mk2"), "29l 先頭が ESC でなければ打ち切らない");
    check(wq_takes() == 0, "29m 同上: 1 つも食わない");
    check(wq_len() == 2 && wq_at(0) == 'a' && wq_at(1) == 0x1B,
          "29n 'a' → ESC の順序でそのまま残る");

    /* --- 29o〜q: 入れ子 source でも食わない --------------------------- */
    fresh();
    wq_push('z');
    s_begin(1);
    s_add("mk2\n");
    s_add("mk3\n");
    file_add("/inner.sh", s_body(1));
    s_begin(0);
    s_add("mk1\n");
    s_add("source /inner.sh\n");
    s_add("mk4\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1") && ran("mk2") && ran("mk3") && ran("mk4"),
          "29o 入れ子 source: 内外とも最後まで走る");
    check(wq_takes() == 0, "29p 入れ子 source: 1 つも食わない");
    check(wq_len() == 1 && wq_at(0) == 'z', "29q 入れ子 source: 打鍵が残る");

    /* --- 29r: 打鍵が無いときは今までどおり (空回り) ------------------- */
    fresh();
    s_begin(0);
    s_add("mk1\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(ran("mk1") && wq_takes() == 0 && wq_len() == 0,
          "29r 打鍵が無ければ何も起きない");
}

/* ========================================================================
 *  30. GUI 端末: スクリプトの行ごとに WM へ譲る (cmd_script.c)
 *
 *  sh.bin は協調型 GUI の中の CPL=3 アプリなので、譲らないかぎり WM は
 *  1 度も回らない。2026-09-16 まで、この譲りは行ごとの ESC 監視の副作用で
 *  出ていた (kbd_trygetkey の空振り → drivers/kbd.c の exec_park_poll)。
 *  監視を kbd_peekkey へ替えたときに副作用ごと消えたので、script_exec が
 *  KAPI v49 の sys_yield で明示的に譲り直す。
 *
 *  窓は贋 KAPI の呼び出し回数 (yield_count)。間引き (同じ tick では 1 回)
 *  は tick を止めて見る。**常駐 (CUI) 側は呼び出しごと消える** —
 *  script_yield_gui は #ifdef SHELL_AS_APP の外で ((void)0) なので、この
 *  試験の枠 (-DSHELL_AS_APP) では見られない。CUI 側は
 *  test_sh_truncation.py の cui_has_no_yield() が前処理で見る。
 *
 *  **窓の較正 (2026-09-16、票 TASK_EXIT_STATUS の着地で調整)**:
 *  sh_launch は要求表の待ちループで sys_yield を回すので、**外部コマンドを
 *  含む行では起動の待ちぶんが同じ窓に混ざる**。以前の贋 launch_req は
 *  「GUI 外 (OS32_ERR_INVAL)」を返して待ちループへ入らなかったため混ざって
 *  いなかったが、いまは「要求を受け付けて子が見つからない」を返すので入る。
 *  そこで 30a〜h は **内蔵コマンド (`echo`) だけ**で組み、script_exec の
 *  譲りだけを数える。外部コマンドを含む形は 30i〜j で、同じ行を対話で 1 回 /
 *  スクリプトで 1 回流した **差分がちょうど 1** であることで見る
 *  (候補の数に依存しない)。走った行は `echo` の出力で確かめる。
 * ======================================================================== */
static void case_script_yield(void)
{
    report("30 GUI 端末: スクリプトの行ごとに WM へ譲る\n");

    /* --- 30a〜b: 行ごとに 1 回 (贋 tick は呼ぶたびに進む = 間引かれない) -- */
    fresh();
    s_begin(0);
    s_add("echo yA\n");
    s_add("echo yB\n");
    s_add("echo yC\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(out_has("yA") && out_has("yB") && out_has("yC"), "30a 3 行とも走る");
    check(yield_count() == 3, "30b 行ごとに 1 回ずつ WM へ譲る");

    /* --- 30c: ラベル行では譲らない (実行する行だけ) -------------------- */
    fresh();
    s_begin(0);
    s_add(":L\n");
    s_add("echo yA\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(out_has("yA") && yield_count() == 1, "30c ラベル行では譲らない");

    /* --- 30d: スクリプト以外 (対話の 1 行) では譲らない ---------------- */
    fresh();
    execute_command("echo yA");
    check(out_has("yA") && yield_count() == 0, "30d 対話の 1 行では譲らない");

    /* --- 30e〜f: 入れ子 source でも内側の行ごとに譲る ------------------ */
    fresh();
    s_begin(1);
    s_add("echo yB\n");
    s_add("echo yC\n");
    file_add("/inner.sh", s_body(1));
    s_begin(0);
    s_add("echo yA\n");
    s_add("source /inner.sh\n");
    s_add("echo yD\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    check(out_has("yA") && out_has("yB") && out_has("yC") && out_has("yD"),
          "30e 入れ子 source: 内外とも最後まで走る");
    check(yield_count() == 5, "30f 入れ子でも行ごと (外 3 行 + 内 2 行)");

    /* --- 30g〜h: 同じ tick の中では 1 回だけ (間引き) ------------------
     *  譲りは park / resume の往復なので、`goto` で回る軽い行のループでは
     *  往復のほうが行より重い。消えた側 (exec_park_poll) と同じ間引き。 */
    fresh();
    tick_freeze(1);
    s_begin(0);
    s_add("echo yA\n");
    s_add("echo yB\n");
    s_add("echo yC\n");
    file_add("/t.sh", s_body(0));
    execute_command("source /t.sh");
    tick_freeze(0);
    check(out_has("yA") && out_has("yB") && out_has("yC"),
          "30g 間引いても行は全部走る");
    check(yield_count() == 1, "30h 同じ tick の中では 1 回だけ譲る");

    /* --- 30i〜j: 外部コマンドを含む行でも、行ごとの譲りが 1 回増える ----
     *  sh_launch の待ちループが同じ窓を回すので、絶対値では見ない。
     *  **同じ 1 行**を対話とスクリプトで流し、差がちょうど 1 であることで
     *  「script_exec が足した 1 回」を取り出す (PATH 候補の数に依存しない)。*/
    {
        int base, in_script;

        fresh();
        tick_step();          /* 直前の 30h が tick を止めたままにしている */
        execute_command("mk1");
        base = yield_count();
        check(ran("mk1"), "30i 対話: 外部コマンドを解決しにいった");

        fresh();
        tick_step();
        s_begin(0);
        s_add("mk1\n");
        file_add("/t.sh", s_body(0));
        execute_command("source /t.sh");
        in_script = yield_count();
        check(ran("mk1") && in_script == base + 1,
              "30j 同じ行でもスクリプトなら譲りがちょうど 1 回増える");
    }
}

/* ---- entry ------------------------------------------------------------- */

void _start(void)
{
    build_api();
    sh_boot();
    case_registry_is_real();
    case_if_compare_refuses();
    case_boundary();
    case_nested_execute_command();
    case_nested_source();
    case_pipe_stage();
    case_interactive_not_aborted();
    case_profile_continues();
    case_no_false_abort();
    /* 段 3「ルーター」 — T3 / T4 / T5 / T6 / T7 / T11 / T13 / T17 */
    case_try_exec_refuses();
    case_exec_time_refuse();
    case_cmd_name_refuses();
    case_pipeline_stages();
    case_glob_alloc_fail();
    case_glob_pattern_refuses();
    case_launch_cmdline_refuses();
    case_long_line_refuses();
    case_path_entry_refuses();
    case_too_many_args_marks();
    /* 段 4「内蔵と入口」 */
    case_script_load_refuses();
    case_env_set_refuses();
    case_env_expand_name();
    case_rshell_line();
    case_ui_history_and_line();
    case_filer_names();
    case_cfg_set_key();
    case_if_join_refuses();
    case_tab_completion();
    case_rshell_nested_refuse_flag();
    case_script_esc_watch();
    case_script_yield();
    report(failures ? "SOME FAIL\n" : "ALL PASS\n");
    die(failures ? 1 : 0);
}
