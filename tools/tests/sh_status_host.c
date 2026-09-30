/* ========================================================================
 *  sh_status_host.c — 終了コードの配線と `$?` を **実物のソースで** 押さえる
 *
 *  対象票: docs/archive/shell/TASK_EXIT_STATUS.md (受入 S1〜S15 / R1〜R2)
 *  実行:   python3 -B tools/tests/test_sh_status.py [--mutate]
 *  記録:   tools/tests/sh_status_tdd.md
 *
 *  1 行も写さずそのまま #include する実物 (模型ではない):
 *    - userland/shell/main.c        (execute_command / execute_single /
 *                                    sh_exec.inc = try_exec /
 *                                    try_exec_from_path / run_cmd_internal /
 *                                    sh_exec_result)
 *    - userland/shell/cmd_script.c  (source / if / goto / exit / set -e)
 *    - userland/shell/cmd_env.c     (`$?` の展開 / set -e の入口)
 *    - userland/shell/cmd_base.c    (time / sh.bin の exit)
 *    - userland/shell/cmd_mnt.c     (exec)
 *    - userland/shell/cmd_dir.c / cmd_file.c / cmd_fs_shared.c / cmd_sys.c
 *    - userland/shell/rshell.c / ui.c / cmd_filer.c (入口)
 *
 *  **登録表も execute_command も本物**。`exit` を文字列で直接見るスタブは
 *  1 つも置いていない (受入 R2 — sh_shell_host.c の作りにしない)。
 *
 *  この 1 本を **2 通り** にコンパイルする (test_sh_status.py):
 *    -DSHELL_AS_APP … 端末の子 (要求表経由の起動、`exit` は端末を閉じる)
 *    定義なし       … 常駐シェル (exec_run + exec_last_result、KAPI v55)
 *  種別ごとの写像はビルドで実装が違う (§2-2) ので、両方を通す。
 *
 *  「その経路が実際に走ったか」の窓 (偽の緑を防ぐ):
 *    - 子が起きたか : g_exec_calls (常駐 = exec_run / sh.bin = launch_req) と
 *                     g_exec_paths (どの綴りで起こしにいったか)
 *    - 出力が出たか : g_out (kprintf / sys_write(1) の写し)
 *    - どの行が走ったか: `echo` の出力と "command not found" の並び
 *
 *  tools/tests/sh_truncation_host.c と同じ様式 — ホスト ILP32 GNU89、
 *  libc 無し (-nostdlib、Linux の int 0x80 で write/exit)。
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

static int __cdecl h_sys_stat(const char *path, OS32_Stat *st)
{
    int i;
    if (!st) return -1;
    for (i = 0; i < (int)sizeof(OS32_Stat); i++) ((u8 *)st)[i] = 0;
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
/* 失敗を差し込む窓 (受入 S3: 組み込みの成功 / 失敗) */
static int g_chdir_rc;
static int g_mkdir_rc;
static int g_redir_fail;
static int __cdecl h_sys_chdir(const char *p) { (void)p; return g_chdir_rc; }
static int __cdecl h_sys_mkdir(const char *p) { (void)p; return g_mkdir_rc; }
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
    /* 受入 S4: リダイレクト先が開けない行は handler へ届かない (`$?` = 2) */
    if (g_redir_fail) return -1;
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
 * 「次の tick まで待つ」ループが抜けない (試験がハングする)。 */
static u32 g_tick;
static u32 __cdecl h_get_tick(void) { return g_tick++; }

/* ------------------------------------------------------------------------
 *  贋の「子」— 終了コードと種別を台本で決める
 *
 *  常駐   : exec_run が呼ばれたら台本を引き、**カーネルと同じ規則で**
 *           記録 (g_rec_kind / g_rec_code) を全 return 点で書く。
 *           exec_last_result はその記録を返す (無ければ OS32_ERR_INVAL)。
 *  sh.bin : launch_req / launch_poll が同じ台本を LAUNCH_ST_* に写す。
 *           終了コードは表に載らない (§2-6) ので DONE = (EXITED, 0)。
 * ---------------------------------------------------------------------- */
#define PROG_MAX 12

static struct {
    const char *path;     /* 起動する綴り (cmdline の先頭語と完全一致) */
    int kind;             /* EXEC_KIND_* */
    int code;             /* EXITED のときの終了コード */
} g_progs[PROG_MAX];
static int g_prog_count;

/* カーネル側の記録に相当 (exec/exec.c の g_last_kind / g_last_code) */
static int g_rec_kind = EXEC_KIND_NONE;
static int g_rec_code;
/* 「記録が無い」を演じる窓 (受入 S15: 前の記録を読まない) */
static int g_rec_suppress;

static void progs_reset(void)
{
    g_prog_count = 0;
    g_rec_kind = EXEC_KIND_NONE;
    g_rec_code = 0;
    g_rec_suppress = 0;
}

static void prog_add(const char *path, int kind, int code)
{
    if (g_prog_count < PROG_MAX) {
        g_progs[g_prog_count].path = path;
        g_progs[g_prog_count].kind = kind;
        g_progs[g_prog_count].code = code;
        g_prog_count++;
    }
}

/* cmdline の先頭語 (空白まで) を取り出して台本を引く。無ければ -1。 */
#define PROG_NAME_CAP 512
static int prog_find(const char *cmdline)
{
    char name[PROG_NAME_CAP];
    int i = 0, j;

    while (cmdline[i] && cmdline[i] != ' ' && i < PROG_NAME_CAP - 1) {
        name[i] = cmdline[i];
        i++;
    }
    name[i] = '\0';
    for (j = 0; j < g_prog_count; j++) {
        if (strcmp(g_progs[j].path, name) == 0) return j;
    }
    return -1;
}

/* 起こしにいった綴りを全部並べる (どの候補を試したかの窓) */
#define EXEC_PATHS_CAP 1024
static char g_exec_paths[EXEC_PATHS_CAP];
static int  g_exec_paths_len;
static int  g_exec_calls;

static void exec_log_reset(void)
{
    g_exec_paths_len = 0;
    g_exec_paths[0] = '\0';
    g_exec_calls = 0;
}

static void exec_log(const char *cmdline)
{
    int i;
    g_exec_calls++;
    for (i = 0; cmdline[i] && g_exec_paths_len < EXEC_PATHS_CAP - 2; i++)
        g_exec_paths[g_exec_paths_len++] = cmdline[i];
    if (g_exec_paths_len < EXEC_PATHS_CAP - 1)
        g_exec_paths[g_exec_paths_len++] = '\n';
    g_exec_paths[g_exec_paths_len] = '\0';
}

/* <want> を何回起こしにいったか (1 コピーで 2 回走っていないかの窓) */
static int exec_count(const char *want)
{
    int i, j, n = 0;
    int wl = 0;
    while (want[wl]) wl++;
    if (wl == 0) return 0;
    for (i = 0; i + wl <= g_exec_paths_len; i++) {
        for (j = 0; j < wl && g_exec_paths[i + j] == want[j]; j++) {}
        if (j == wl) n++;
    }
    return n;
}

static int exec_tried(const char *want)
{
    int i, j;
    int wl = 0;
    while (want[wl]) wl++;
    if (wl == 0) return 1;
    for (i = 0; i + wl <= g_exec_paths_len; i++) {
        for (j = 0; j < wl && g_exec_paths[i + j] == want[j]; j++) {}
        if (j == wl) return 1;
    }
    return 0;
}

/* 起動しなかったときの戻り値 (exec/exec.c の exec_map_launch_err の逆) */
static int kind_to_rc(int kind, int code)
{
    switch (kind) {
    case EXEC_KIND_EXITED:    return code;
    case EXEC_KIND_FAULT:     return EXEC_ERR_FAULT;
    case EXEC_KIND_ABORTED:   return EXEC_ERR_FAULT;
    case EXEC_KIND_NOT_FOUND: return EXEC_ERR_NOT_FOUND;
    case EXEC_KIND_INVALID:   return EXEC_ERR_INVALID;
    case EXEC_KIND_NOMEM:     return EXEC_ERR_NOMEM;
    default:                  return EXEC_ERR_GENERAL;
    }
}

/* 常駐の起動口。**カーネルと同じ**く全 return 点で記録を書く。 */
static int __cdecl h_exec_run(const char *cmdline)
{
    int idx;

    exec_log(cmdline);
    /* exec_run の入口で消す (前回の記録を残さない) */
    g_rec_kind = EXEC_KIND_NONE;
    g_rec_code = 0;

    idx = prog_find(cmdline);
    if (idx < 0) {
        g_rec_kind = EXEC_KIND_NOT_FOUND;
        return EXEC_ERR_NOT_FOUND;
    }
    g_rec_kind = g_progs[idx].kind;
    g_rec_code = (g_progs[idx].kind == EXEC_KIND_EXITED) ? g_progs[idx].code : 0;
    return kind_to_rc(g_progs[idx].kind, g_progs[idx].code);
}

static int __cdecl h_exec_last_result(int *kind, int *code)
{
    if (g_rec_suppress || g_rec_kind == EXEC_KIND_NONE) {
        if (kind) *kind = EXEC_KIND_NONE;
        if (code) *code = 0;
        return OS32_ERR_INVAL;
    }
    if (kind) *kind = g_rec_kind;
    if (code) *code = g_rec_code;
    return 0;
}

static void __cdecl h_gfx_shutdown(void) {}

/* 常駐のパイプバッファ (KAPI 経由)。alloc を失敗させる窓つき。 */
#define PIPE_SLOTS 2
static u8  g_pipe_mem[PIPE_SLOTS][PIPE_BUF_SIZE];
static int g_pipe_used[PIPE_SLOTS];
static int g_pipe_alloc_fail;

static void pipe_reset(void)
{
    int i;
    for (i = 0; i < PIPE_SLOTS; i++) g_pipe_used[i] = 0;
    g_pipe_alloc_fail = 0;
}

static int __cdecl h_sys_pipe_alloc(void)
{
    int i;
    if (g_pipe_alloc_fail) return -1;
    for (i = 0; i < PIPE_SLOTS; i++) {
        if (!g_pipe_used[i]) { g_pipe_used[i] = 1; return i; }
    }
    return -1;
}
static u8 *__cdecl h_sys_pipe_get_buf(int slot)
{
    if (slot < 0 || slot >= PIPE_SLOTS) return (u8 *)0;
    return g_pipe_mem[slot];
}
static void __cdecl h_sys_pipe_free(int slot)
{
    if (slot >= 0 && slot < PIPE_SLOTS) g_pipe_used[slot] = 0;
}

/* 起動の記録。切り詰めた行で子が起きたかどうかを見る唯一の窓。 */
#define LAUNCH_LOG_CAP 512
static char g_launch_last[LAUNCH_LOG_CAP];
static int  g_launch_count;

static void launch_log_reset(void)
{
    g_launch_count = 0;
    g_launch_last[0] = '\0';
}

static i32 __cdecl h_launch_req(const char *cmdline)
{
    int i, idx;
    for (i = 0; cmdline[i] && i < LAUNCH_LOG_CAP - 1; i++)
        g_launch_last[i] = cmdline[i];
    g_launch_last[i] = '\0';
    g_launch_count++;
    exec_log(cmdline);
    idx = prog_find(cmdline);
    /* token は「台本の番号 + 1」。台本に無いときも **要求は受け付けて**
     * 範囲外の token を配り、launch_poll が FAILED + NOT_FOUND を返す
     * (本物の WM も「起動してみて見つからなかった」をそう返す)。 */
    return (i32)(idx >= 0 ? idx + 1 : PROG_MAX + 1);
}
static i32 __cdecl h_launch_poll(i32 token, i32 *status)
{
    int idx = (int)token - 1;
    int kind;

    if (idx < 0 || idx >= g_prog_count) {
        if (status) *status = LAUNCH_ST_FAILED | (i32)(-EXEC_ERR_NOT_FOUND);
        return 0;
    }
    kind = g_progs[idx].kind;
    if (kind == EXEC_KIND_EXITED) {
        /* §2-6: 要求表に終了コードは載らない。DONE = 「起動できて終わった」 */
        if (status) *status = LAUNCH_ST_DONE;
        return 0;
    }
    if (status)
        *status = LAUNCH_ST_FAILED | (i32)(-kind_to_rc(kind, 0));
    return 0;
}
static i32 __cdecl h_sys_yield(void) { return 0; }

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
static void __cdecl h_rshell_set_active(int on) { (void)on; }
static void __cdecl h_buz_off(void) {}
static void __cdecl h_sys_halt(void) {}
static int  __cdecl h_sys_mount(const char *mp, const char *dev, const char *fs)
{ (void)mp; (void)dev; (void)fs; return 0; }

static int  __cdecl h_console_get_cursor_x(void)   { return 0; }
static int  __cdecl h_console_get_cursor_y(void)   { return 0; }
static void __cdecl h_console_set_cursor(int x, int y) { (void)x; (void)y; }
static void __cdecl h_tvram_clear(void) {}

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
    /* 常駐の起動口 (KAPI v55) とその周り */
    g_fake.exec_run = h_exec_run;
    g_fake.exec_last_result = h_exec_last_result;
    g_fake.gfx_shutdown = h_gfx_shutdown;
    g_fake.sys_pipe_alloc = h_sys_pipe_alloc;
    g_fake.sys_pipe_get_buf = h_sys_pipe_get_buf;
    g_fake.sys_pipe_free = h_sys_pipe_free;
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
#ifndef SHELL_AS_APP
    /* 常駐だけが登録する (main.c と同じ #ifndef) */
    shell_rshell_init();
#endif
    shell_cmd_env_init();
    shell_cmd_script_init();
    shell_cmd_filer_init();
}

/* ---- 試験の道具 -------------------------------------------------------- */

static char g_line[CMD_BUF_SIZE + 64];   /* 上限超えの反例を作るので少し大きく */

static void line_reset(void) { g_line[0] = '\0'; }

static void line_add(const char *s)
{
    int n = 0;
    while (g_line[n]) n++;
    while (*s && n < (int)sizeof(g_line) - 1) g_line[n++] = *s++;
    g_line[n] = '\0';
}

static void line_add_run(char c, int n)
{
    int i;
    int k = 0;
    while (g_line[k]) k++;
    for (i = 0; i < n && k < (int)sizeof(g_line) - 1; i++) g_line[k++] = c;
    g_line[k] = '\0';
}

/* スクリプト本文 (贋 FS は本文のポインタを持つだけなので静的領域) */
#define SBUF_COUNT  4
#define SBUF_SIZE   40960

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

static void s_run(int count)
{
    int n = g_sn[g_scur];
    int i;
    for (i = 0; i < count && n < SBUF_SIZE - 1; i++) g_sbuf[g_scur][n++] = 'a';
    g_sbuf[g_scur][n] = '\0';
    g_sn[g_scur] = n;
}

static const char *s_body(int i) { return g_sbuf[i]; }

static void fresh(void)
{
    files_reset();
    out_reset();
    exec_log_reset();
    progs_reset();
    pipe_reset();
    launch_log_reset();
    redir_log_reset();
    dir_reset();
    keys_reset();
    wq_reset();
    ser_reset();
    g_popup_count = 0;
    g_popup[0] = '\0';
    sh_exit_flag = 0;
    sh_exit_code = 0;
    sh_refused_flag = 0;
    g_glob_alloc_budget = -1;
    g_glob_allocs = 0;
    g_frees = 0;
    g_chdir_rc = 0;
    g_mkdir_rc = 0;
    g_redir_fail = 0;
    script_errexit_set(0);
    sh_status_set(0);
    env_set("PATH", SYS_DEFAULT_PATH);
}

/* 1 行流して `$?` を返す (execute_command は実物) */
static int st(const char *cmd)
{
    return execute_command(cmd);
}

/* `echo $?` の展開結果を見る。値そのものではなく **展開の形** を見たいとき用。*/
static int echoed(const char *line, const char *want)
{
    out_reset();
    (void)execute_command(line);
    return out_has(want);
}

/* 数値の比較を 1 行で報告できるように */
static char g_note[96];

static const char *note2(const char *a, int v)
{
    int n = 0, i;
    char num[12];
    int d = 0;
    int neg = 0;
    while (a[n] && n < (int)sizeof(g_note) - 16) { g_note[n] = a[n]; n++; }
    g_note[n++] = ' ';
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) num[d++] = '0';
    while (v > 0) { num[d++] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) num[d++] = '-';
    for (i = d - 1; i >= 0; i--) g_note[n++] = num[i];
    g_note[n] = '\0';
    return g_note;
}

/* ========================================================================
 *  S1 — 子の終了コードがそのまま `$?` になる (`-2` が FAULT にならない)
 * ======================================================================== */
static void case_s1_exit_codes(void)
{
    report("S1 子の終了コード -> $? (値から種別を作らない)\n");

#ifndef SHELL_AS_APP
    {
        static const int in[]  = {   0,  1,  -1,  -2,  -3,  -4,  -5,  2, 255 };
        static const int out[] = {   0,  1, 255, 254, 253, 252, 251,  2, 255 };
        int i;
        for (i = 0; i < 9; i++) {
            fresh();
            prog_add("t1.bin", EXEC_KIND_EXITED, in[i]);
            check(st("t1") == out[i], note2("S1 exit -> $?", in[i]));
            check(g_exec_calls == 1, note2("S1 1 回だけ起こす", in[i]));
            check(!out_has("command not found"),
                  note2("S1 not found と言わない", in[i]));
        }
    }
    /* `$?` の展開でも同じ値が出る (環境変数表は経由しない) */
    fresh();
    prog_add("t1.bin", EXEC_KIND_EXITED, 42);
    (void)st("t1");
    check(echoed("echo $?", "42"), "S1 $? の展開が子の値になる");
    check(env_get("?") == (const char *)0, "S1 `?` を環境変数表に書かない");
#else
    /* sh.bin は終了コードを運べない (§2-6)。DONE = (EXITED, 0)。 */
    fresh();
    prog_add("t1.bin", EXEC_KIND_EXITED, 7);
    check(st("t1") == 0, "S1 sh.bin: DONE は 0 (終了コードは別票)");
    check(g_exec_calls == 1, "S1 sh.bin: 1 回だけ起こす");
#endif
}

/* ========================================================================
 *  S1b — fault と CTRL+STOP
 * ======================================================================== */
static void case_s1b_fault_abort(void)
{
    report("S1b fault / CTRL+STOP の $?\n");

    fresh();
    prog_add("tf.bin", EXEC_KIND_FAULT, 0);
    check(st("tf") == SH_STATUS_FAULT, "S1b fault は 139");
    check(out_has("[Process crashed]"), "S1b 表示は今までどおり");

#ifndef SHELL_AS_APP
    fresh();
    prog_add("ta.bin", EXEC_KIND_ABORTED, 0);
    check(st("ta") == SH_STATUS_ABORTED, "S1b CTRL+STOP は 130");
    check(out_has("[Process crashed]"), "S1b 表示は fault と同じ");
    /* 誤発火の裏: exit(-2) は FAULT ではない */
    fresh();
    prog_add("tx.bin", EXEC_KIND_EXITED, -2);
    check(st("tx") == 254, "S1b exit(-2) は 254 (139 ではない)");
    check(!out_has("[Process crashed]"), "S1b exit(-2) は crashed と言わない");
#endif
}

/* ========================================================================
 *  S2 / S2b — PATH 走査の停止条件 (実害 1 の修正)
 * ======================================================================== */
static void case_s2_path_scan(void)
{
    report("S2 PATH 走査: 種別で止める\n");

    /* S2: 候補 1 は無い / 候補 2 が在る */
    fresh();
    env_set("PATH", "/bin:/usr/bin");
    prog_add("/usr/bin/t2.bin", EXEC_KIND_EXITED, 0);
    check(st("t2") == 0, "S2 候補 2 を実行して 0");
    check(exec_tried("/bin/t2.bin"), "S2 候補 1 も試している");
    check(exec_tried("/usr/bin/t2.bin"), "S2 候補 2 を実行した");

    /* 成功の直後に未知のコマンド — 前回の記録を読まない (事実 4) */
    check(st("nosuchcmd") == SH_STATUS_NOTFOUND,
          "S2 直後の未知コマンドは 127 (前の記録を読まない)");
    check(out_has("command not found"), "S2 未知コマンドは今までどおり報せる");

    /* S2b: exit(-1) の子を 1 本だけ置く → 実行は 1 回、not found と言わない */
    fresh();
    env_set("PATH", "/bin:/usr/bin");
    prog_add("/usr/bin/t3.bin", EXEC_KIND_EXITED, -1);
#ifndef SHELL_AS_APP
    check(st("t3") == 255, "S2b exit(-1) は 255");
#else
    /* sh.bin は終了コードを運べない (§2-6)。それでも **止まる** ことが肝。 */
    check(st("t3") == 0, "S2b sh.bin: DONE は 0");
#endif
    check(exec_count("/usr/bin/t3.bin") == 1,
          "S2b 同じものを 2 回実行しない (1 コピーで 1 回)");
    check(!out_has("command not found"), "S2b command not found を出さない");

    /* S2d: 起こせなかった (NOMEM / FULL 相当) は **どんな値でも止まる** */
    fresh();
    env_set("PATH", "/bin:/usr/bin");
    prog_add("t5.bin", EXEC_KIND_NOMEM, 0);
    check(st("t5") == SH_STATUS_NOEXEC, "S2d 起こせない = 126");
    check(g_exec_calls == 1, "S2d PATH の数だけ繰り返さない");
    check(!out_has("command not found"), "S2d not found と言わない");
}

/* ========================================================================
 *  S2c / S8 — INVALID は次の候補へ進み、尽きたら 126
 * ======================================================================== */
static void case_s2c_s8_invalid(void)
{
    report("S2c / S8 OS32X ヘッダが不正なとき\n");

    fresh();
    env_set("PATH", "/bin:/usr/bin");
    prog_add("t4.bin", EXEC_KIND_INVALID, 0);          /* cwd の壊れた 1 本 */
    prog_add("/bin/t4.bin", EXEC_KIND_EXITED, 7);      /* PATH の正しい 1 本 */
#ifndef SHELL_AS_APP
    check(st("t4") == 7, "S2c cwd が不正なら PATH の方を実行する");
#else
    check(st("t4") == 0, "S2c sh.bin: PATH の方を実行して DONE (0)");
#endif
    check(exec_tried("/bin/t4.bin"), "S2c PATH の候補を実際に起こした");

    /* S8: cwd に不正な 1 本、PATH には同名が無い → 126 (127 ではない) */
    fresh();
    env_set("PATH", "/bin:/usr/bin");
    prog_add("t6.bin", EXEC_KIND_INVALID, 0);
    check(st("t6") == SH_STATUS_NOEXEC,
          "S8 INVALID を見たら 126 (見つからないとは言わない)");
    check(!out_has("command not found"), "S8 command not found を出さない");
    check(out_has("not a valid executable"), "S8 理由を報せる");

    /* 誤発火の裏: 全部 NOT_FOUND なら 127 */
    fresh();
    env_set("PATH", "/bin:/usr/bin");
    check(st("t7") == SH_STATUS_NOTFOUND, "S8 裏 全部無ければ 127");
    check(out_has("command not found"), "S8 裏 文言は今までどおり");
}

/* ========================================================================
 *  S3 — 組み込みの成功 / 失敗
 * ======================================================================== */
static void case_s3_builtins(void)
{
    report("S3 組み込みの成功 / 失敗\n");

    fresh();
    check(st("pwd") == 0, "S3 pwd は 0");
    fresh();
    check(st("echo hi") == 0, "S3 echo は 0");
    check(out_has("hi"), "S3 echo は実際に書いた");

    fresh();
    g_chdir_rc = OS32_ERR_NOTFOUND;
    check(st("cd /nonexistent") != 0, "S3 cd の失敗は非 0");
    fresh();
    g_chdir_rc = 0;
    check(st("cd /tmp") == 0, "S3 cd の成功は 0");

    fresh();
    check(st("cat /nosuch.txt") != 0, "S3 無いファイルの cat は非 0");
    fresh();
    file_add("/a.txt", "hello");
    check(st("cat /a.txt") == 0, "S3 読めた cat は 0");
    check(out_has("hello"), "S3 中身を実際に出した");

    fresh();
    g_mkdir_rc = OS32_ERR_EXIST;
    check(st("mkdir /a") != 0, "S3 既存への mkdir は非 0");
    fresh();
    g_mkdir_rc = 0;
    check(st("mkdir /b") == 0, "S3 作れた mkdir は 0");

    /* usage (引数不足) は 2 */
    fresh();
    check(st("mkdir") == SH_STATUS_USAGE, "S3 引数不足は 2");
}

/* ========================================================================
 *  S3b — source / time / if / パイプライン / goto / ESC
 * ======================================================================== */
static void case_s3b_composites(void)
{
    report("S3b source / time / if / パイプ / goto / ESC\n");

    /* source は最後に実行した行の値 */
    fresh();
    s_begin(0);
    s_add("echo one\n");
    s_add("nosuchcmd\n");
    file_add("/s.sh", s_body(0));
    check(st("source /s.sh") == SH_STATUS_NOTFOUND,
          "S3b source は最後の行の値");
    check(out_has("one"), "S3b 1 行目は実際に走った");

    fresh();
    s_begin(0);
    s_add("echo one\n");
    file_add("/s.sh", s_body(0));
    check(st("source /s.sh") == 0, "S3b 通った source は 0");

    /* time は内側の値 */
    fresh();
    check(st("time echo hi") == 0, "S3b time 成功は 0");
    check(out_has("hi"), "S3b time は中身を走らせた");
    fresh();
    check(st("time nosuchcmd") == SH_STATUS_NOTFOUND, "S3b time 失敗は内側の値");

    /* if: 真は内側の値 / 偽は 0 */
    fresh();
    check(st("if a == a nosuchcmd") == SH_STATUS_NOTFOUND, "S3b 真の if は内側の値");
    fresh();
    sh_status_set(5);            /* 偽の if が前の値を持ち越さないこと */
    check(st("if a == b nosuchcmd") == 0, "S3b 偽の if は 0");
    check(!out_has("command not found"), "S3b 偽の if は中身を走らせない");

    /* パイプラインは最後の段の値。段はどちらも**内蔵**にする —
     * sh.bin はパイプの中から外部プログラムへ行けない (別票の制約) ので、
     * 外部を混ぜると両ビルドで同じ反例にならない。 */
    fresh();
    check(st("echo a | cat /nosuch.txt") != 0, "S3b パイプは最後の段の値");
    fresh();
    file_add("/ok.txt", "body");
    check(st("cat /nosuch.txt | cat /ok.txt") == 0, "S3b 最後が成功なら 0");
    check(out_has("body"), "S3b 後段は実際に走った");

    /* goto のラベル無しは非 0 で打ち切る */
    fresh();
    s_begin(0);
    s_add("goto nowhere\n");
    s_add("echo after\n");
    file_add("/g.sh", s_body(0));
    check(st("source /g.sh") != 0, "S3b goto のラベル無しは非 0");
    check(!out_has("after"), "S3b 打ち切って後続を走らせない");

    /* ESC 中断は 130 */
    fresh();
    s_begin(0);
    s_add("echo one\n");
    s_add("echo two\n");
    file_add("/e.sh", s_body(0));
    wq_push(0x1B);
    check(st("source /e.sh") == SH_STATUS_ABORTED, "S3b ESC 中断は 130");
    check(!out_has("one"), "S3b ESC は 1 行目の前に効く");
}

/* ========================================================================
 *  S4 — handler に届かない行の `$?`
 * ======================================================================== */
static void case_s4_unreached(void)
{
    report("S4 handler に届かない行の $?\n");

    /* リダイレクト失敗 */
    fresh();
    g_redir_fail = 1;
    check(st("echo x > /bad/out") == SH_STATUS_USAGE, "S4 リダイレクト失敗は 2");
    check(!out_has("x\n"), "S4 handler を呼んでいない");

    /* 行が長すぎる */
    fresh();
    line_reset();
    line_add("echo ");
    line_add_run('a', CMD_BUF_SIZE - 5);
    check(st(g_line) == SH_STATUS_USAGE, "S4 長すぎる行は 2");

    /* 引数過多 */
    fresh();
    line_reset();
    line_add("echo");
    {
        int i;
        for (i = 0; i < MAX_ARGS + 4; i++) line_add(" x");
    }
    check(st(g_line) == SH_STATUS_USAGE, "S4 引数過多は 2");

#ifndef SHELL_AS_APP
    /* パイプバッファの確保失敗 */
    fresh();
    g_pipe_alloc_fail = 1;
    check(st("echo a | echo b") == SH_STATUS_USAGE, "S4 パイプ確保失敗は 2");
    check(out_has("buffer alloc failed"), "S4 実際に確保で落ちた経路");
#endif

    /* command not found */
    fresh();
    check(st("nosuchcmd") == SH_STATUS_NOTFOUND, "S4 command not found は 127");

    /* 空行・コメントだけの行は `$?` を変えない */
    fresh();
    prog_add("t1.bin", EXEC_KIND_EXITED, 5);
#ifndef SHELL_AS_APP
    (void)st("t1");
    check(sh_status_get() == 5, "S4 下ごしらえ: $? = 5");
    check(st("") == 5, "S4 空行は $? を変えない");
    check(st("   ") == 5, "S4 空白だけの行も変えない");
#endif
}

/* ========================================================================
 *  S5 / S5b — set -e
 * ======================================================================== */
static void case_s5_errexit(void)
{
    report("S5 set -e で失敗行の後を実行しない\n");

    /* (1) 外部コマンドの失敗 */
    fresh();
    prog_add("tbad.bin", EXEC_KIND_EXITED, 3);
    s_begin(0);
    s_add("set -e\n");
    s_add("tbad\n");
    s_add("echo after\n");
    file_add("/e1.sh", s_body(0));
#ifndef SHELL_AS_APP
    check(st("source /e1.sh") == 3, "S5 外部の失敗: source は子の値");
    check(!out_has("after"), "S5 外部の失敗: 3 行目を実行しない");
    check(out_has("script: line "), "S5 止めた行を報せる");
#endif

    /* (2) 組み込みの失敗 */
    fresh();
    g_chdir_rc = OS32_ERR_NOTFOUND;
    s_begin(0);
    s_add("set -e\n");
    s_add("cd /nonexistent\n");
    s_add("echo after\n");
    file_add("/e2.sh", s_body(0));
    check(st("source /e2.sh") != 0, "S5 組み込みの失敗: source は非 0");
    check(!out_has("after"), "S5 組み込みの失敗: 3 行目を実行しない");

    /* (3) リダイレクト失敗 */
    fresh();
    g_redir_fail = 1;
    s_begin(0);
    s_add("set -e\n");
    s_add("echo x > /bad/out\n");
    s_add("echo after\n");
    file_add("/e3.sh", s_body(0));
    check(st("source /e3.sh") == SH_STATUS_USAGE,
          "S5 リダイレクト失敗: source は 2");
    check(!out_has("after"), "S5 リダイレクト失敗: 3 行目を実行しない");

    /* (4) 未知のコマンド */
    fresh();
    s_begin(0);
    s_add("set -e\n");
    s_add("nosuchcmd\n");
    s_add("echo after\n");
    file_add("/e4.sh", s_body(0));
    check(st("source /e4.sh") == SH_STATUS_NOTFOUND,
          "S5 未知コマンド: source は 127");
    check(!out_has("after"), "S5 未知コマンド: 3 行目を実行しない");

    /* 誤発火の裏: set -e でも成功した行の後は続く */
    fresh();
    s_begin(0);
    s_add("set -e\n");
    s_add("echo one\n");
    s_add("echo after\n");
    file_add("/e5.sh", s_body(0));
    check(st("source /e5.sh") == 0, "S5 裏 成功続きなら 0");
    check(out_has("after"), "S5 裏 後続も走る");

    /* set -e を **付けなければ** 止まらない */
    fresh();
    s_begin(0);
    s_add("nosuchcmd\n");
    s_add("echo after\n");
    file_add("/e6.sh", s_body(0));
    check(out_has("") && st("source /e6.sh") == 0, "S5 裏 -e 無しは最後の行の値");
    check(out_has("after"), "S5 裏 -e 無しは後続も走る");

    report("S5b set +e と入れ子の save / restore\n");

    /* set +e で戻る */
    fresh();
    s_begin(0);
    s_add("set -e\n");
    s_add("set +e\n");
    s_add("nosuchcmd\n");
    s_add("echo after\n");
    file_add("/e7.sh", s_body(0));
    check(st("source /e7.sh") == 0, "S5b set +e で止まらなくなる");
    check(out_has("after"), "S5b 後続が走る");

    /* 入れ子の source を抜けたら旗が戻る */
    fresh();
    s_begin(1);
    s_add("set -e\n");
    s_add("echo inner\n");
    file_add("/child.sh", s_body(1));
    s_begin(0);
    s_add("source /child.sh\n");
    s_add("nosuchcmd\n");
    s_add("echo after\n");
    file_add("/parent.sh", s_body(0));
    check(st("source /parent.sh") == 0, "S5b 内側の -e は外へ漏れない");
    check(out_has("inner") && out_has("after"), "S5b 外側は最後まで走る");
    check(script_errexit_get() == 0, "S5b 旗は元へ戻っている");

    /* set -e は `-e: not set` に落ちない (往復 1 所見 6) */
    fresh();
    check(st("set -e") == 0, "S5b set -e は値の表示に落ちない");
    check(!out_has("not set"), "S5b `-e: not set` を出さない");
    check(script_errexit_get() == 1, "S5b 旗が立った");
    check(st("set +e") == 0 && script_errexit_get() == 0, "S5b set +e で下りる");
}

/* ========================================================================
 *  S6 / S6b — exit [N]
 * ======================================================================== */
static void case_s6_exit(void)
{
#ifndef SHELL_AS_APP
    report("S6 常駐の exit [N]\n");

    /* 対話: シェルを終わらせず `$?` だけ変える */
    fresh();
    check(st("exit 3") == 3, "S6 対話の exit 3 は $? = 3");
    check(sh_exit_flag == 0, "S6 対話では要求を 1 行で下ろす (終わらない)");
    check(echoed("echo $?", "3"), "S6 次の行で $? が読める");

    /* exit 0 でも要求は立つ (真偽値に値を入れていない) */
    fresh();
    check(st("exit 0") == 0, "S6 exit 0 は 0");

    /* スクリプト中: 打ち切って source が N */
    fresh();
    s_begin(0);
    s_add("echo one\n");
    s_add("exit 3\n");
    s_add("echo after\n");
    file_add("/x.sh", s_body(0));
    check(st("source /x.sh") == 3, "S6 スクリプト中の exit 3 は source が 3");
    check(out_has("one"), "S6 1 行目は走った");
    check(!out_has("after"), "S6 exit の後は走らない");

    /* 引数が不正なら実行しない (2) */
    fresh();
    check(st("exit abc") == SH_STATUS_USAGE, "S6 非数値は 2");
    check(sh_exit_flag == 0, "S6 非数値では要求を立てない");
    fresh();
    check(st("exit 300") == SH_STATUS_USAGE, "S6 範囲外は 2");
    fresh();
    check(st("exit 1 2") == SH_STATUS_USAGE, "S6 引数 2 つは 2");
    /* 数字で始まって途中から数字でない形も断る (先頭だけ読んで通さない) */
    fresh();
    check(st("exit 1a") == SH_STATUS_USAGE, "S6 `1a` は 2");
    check(sh_exit_flag == 0, "S6 `1a` では要求を立てない");
    /* 誤発火の裏: 素の数字は通る */
    fresh();
    check(st("exit 7") == 7, "S6 裏 `7` は通る");

    /* 引数なしは直前の値 */
    fresh();
    prog_add("t1.bin", EXEC_KIND_EXITED, 9);
    (void)st("t1");
    check(st("exit") == 9, "S6 引数なしは直前の $?");
#else
    report("S6b sh.bin の exit [N]\n");

    fresh();
    check(st("exit") == 0, "S6b sh.bin: exit は 0");
    check(sh_exit_flag == 1, "S6b sh.bin: 端末を閉じる印が立つ");

    fresh();
    check(st("exit 3") == 3, "S6b sh.bin: exit 3 は 3");
    check(sh_exit_flag == 1 && sh_exit_code == 3, "S6b sh.bin: N が載る");

    fresh();
    check(st("exit abc") == SH_STATUS_USAGE, "S6b sh.bin: 非数値は 2");
    check(sh_exit_flag == 0, "S6b sh.bin: 非数値では閉じない");
#endif
}

/* ========================================================================
 *  S7 — `$?` の展開
 * ======================================================================== */
static void case_s7_expand(void)
{
    report("S7 $? の展開\n");

    fresh();
    check(echoed("echo $?", "0"), "S7 初期値は 0");

    fresh();
    sh_status_set(12);
    check(echoed("echo [$?]", "[12]"), "S7 $? が数字に展開される");

    /* 往復 5 の注意 2: 展開後が数字で始まる形 (`$?x` -> `0x`) */
    fresh();
    sh_status_set(0);
    check(echoed("echo $?x", "0x"), "S7 $?x は 0x に展開される");
    fresh();
    sh_status_set(12);
    check(echoed("echo $?x", "12x"), "S7 $?x は <値>x (16 進ではない)");

    /* `$` 単独はリテラル */
    fresh();
    check(echoed("echo $", "$"), "S7 裸の $ はそのまま");

    /* `${?}` は対応しない (空に展開される) */
    fresh();
    sh_status_set(5);
    check(echoed("echo [${?}]", "[]"), "S7 ${?} は対応しない (空)");

    /* 環境変数との併用。既存の展開を壊さない */
    fresh();
    sh_status_set(4);
    env_set("VV", "zz");
    /* 区切りは ' ' / '/' / '.' / ':' / '$' (env_expand の規則)。`-` は名前の
     * 一部なので、併用は区切り文字を挟んで見る。 */
    check(echoed("echo $VV:$?", "zz:4"), "S7 $VAR と併用できる");
    fresh();
    env_set("VV", "zz");
    check(echoed("echo $VV", "zz"), "S7 既存の $VAR を壊さない");

    /* `set ?=5` で作った変数があっても特別扱いが先 */
    fresh();
    sh_status_set(7);
    env_set("?", "99");
    check(echoed("echo $?", "7"), "S7 特別扱いが環境変数より先");
    env_unset("?");

    /* パイプ行の `$?` は段を回す前に 1 回だけ展開される */
    fresh();
    sh_status_set(6);
    out_reset();
    (void)execute_command("echo $? | echo tail");
    check(out_has("tail"), "S7 パイプ行も走る");
    check(sh_status_get() == 0, "S7 パイプ行の後は最後の段の値");
}

/* ========================================================================
 *  S9 — `exit 3 | echo tail` (往復 5 の注意 1)
 * ======================================================================== */
static void case_s9_exit_in_pipe(void)
{
    report("S9 パイプの中の exit\n");

    fresh();
    check(st("exit 3 | echo tail") == 3, "S9 exit 3 | echo tail は 3");
    check(!out_has("tail"), "S9 後段を実行しない");

    fresh();
    check(st("exit 0 | echo tail") == 0, "S9 exit 0 でも要求が立つ");
    check(!out_has("tail"), "S9 exit 0 でも後段を実行しない");

    /* `time` 経由 */
    fresh();
    check(st("time exit 3") == 3, "S9 time 経由でも値が届く");

    /* `if` 経由 */
    fresh();
    check(st("if a == a exit 3") == 3, "S9 if 経由でも値が届く");

#ifndef SHELL_AS_APP
    /* 入れ子 source: 内側の exit が外側も止める */
    fresh();
    s_begin(1);
    s_add("exit 4\n");
    file_add("/child.sh", s_body(1));
    s_begin(0);
    s_add("source /child.sh\n");
    s_add("echo after\n");
    file_add("/parent.sh", s_body(0));
    check(st("source /parent.sh") == 4, "S9 入れ子 source の exit は外まで");
    check(!out_has("after"), "S9 外側も後続を実行しない");
#endif

    /* 誤発火の裏: exit の無いパイプは最後の段の値 */
    fresh();
    check(st("echo a | echo tail") == 0, "S9 裏 普通のパイプは 0");
    check(out_has("tail"), "S9 裏 後段が走る");
}

/* ========================================================================
 *  S10 / S11 / S12 — 断った行の `$?` は 2 (値の割り当てだけが本票の範囲)
 * ======================================================================== */
static void case_s10_s12_refused(void)
{
    report("S10 スクリプトの切り詰め -> source は 2\n");

    /* 255 文字を超える行 */
    fresh();
    s_begin(0);
    s_add("echo ");
    s_run(SCRIPT_MAX_LINE);
    s_add("\n");
    file_add("/r1.sh", s_body(0));
    check(st("source /r1.sh") == SH_STATUS_USAGE, "S10 長い行は 2");

    /* 129 行以上 */
    fresh();
    s_begin(0);
    {
        int i;
        for (i = 0; i < SCRIPT_MAX_LINES + 1; i++) s_add("echo x\n");
    }
    file_add("/r2.sh", s_body(0));
    check(st("source /r2.sh") == SH_STATUS_USAGE, "S10 行数超過は 2");

    /* 誤発火の裏: 収まる行は今までどおり */
    fresh();
    s_begin(0);
    s_add("echo ok\n");
    file_add("/r3.sh", s_body(0));
    check(st("source /r3.sh") == 0, "S10 裏 収まるスクリプトは 0");

    report("S11 再構築が溢れる行 -> 起動する前に 2\n");

    fresh();
    prog_add("t1.bin", EXEC_KIND_EXITED, 0);
    line_reset();
    line_add("t1 ");
    line_add_run('a', 600);
    check(st(g_line) == SH_STATUS_USAGE, "S11 外部: 2");
    check(g_exec_calls == 0, "S11 外部: 子を起こさない");

    fresh();
    line_reset();
    line_add("exec t1.bin ");
    line_add_run('a', EXEC_CMDLINE_MAX);
    check(st(g_line) == SH_STATUS_USAGE, "S11 exec: 2");
    check(g_exec_calls == 0, "S11 exec: 子を起こさない");

    fresh();
    line_reset();
    line_add("time t1 ");
    line_add_run('a', TIME_CMD_MAX);
    check(st(g_line) == SH_STATUS_USAGE, "S11 time: 2");
    check(g_exec_calls == 0, "S11 time: 子を起こさない");

    report("S12 パイプの段の異常 -> 行全体を実行せず 2\n");

    fresh();
    check(st("echo a|echo b|echo c|echo d|echo e|echo f|echo g|echo h|echo i")
          == SH_STATUS_USAGE, "S12 9 段は 2");
    fresh();
    check(st("echo ok |") == SH_STATUS_USAGE, "S12 末尾の | は 2");
    check(!out_has("ok"), "S12 段を 1 つも実行しない");
    fresh();
    check(st("| echo ok") == SH_STATUS_USAGE, "S12 先頭の | は 2");
    fresh();
    check(st("echo a || echo b") == SH_STATUS_USAGE, "S12 空の段は 2");

    /* 誤発火の裏: 8 段ちょうどは通る */
    fresh();
    check(st("echo a|echo b|echo c|echo d|echo e|echo f|echo g|echo h") == 0,
          "S12 裏 8 段ちょうどは通る");
}

/* ========================================================================
 *  起動時の profile — 失敗しても `$?` に残さない (票 §2-5-1)
 * ======================================================================== */
static void case_profile_status(void)
{
    report("profile の結果を $? に残さない\n");

    fresh();
    s_begin(0);
    s_add("nosuchcmd\n");
    file_add("/etc/profile", s_body(0));
    script_source_profile("/etc/profile");
    check(sh_status_get() == 0, "profile の失敗は $? に残らない");
    check(out_has("command not found"), "profile の行は実際に走った");

    /* 断った profile でも同じ (起動は止めない) */
    fresh();
    s_begin(0);
    s_add("echo ");
    s_run(SCRIPT_MAX_LINE);
    s_add("\n");
    file_add("/etc/profile", s_body(0));
    script_source_profile("/etc/profile");
    check(sh_status_get() == 0, "断った profile でも $? は 0");
    check(sh_refused_peek() == 0, "断りの印も残さない");
}

/* ========================================================================
 *  S13 — 値を決めておく行 (0 / 0 / 0)
 * ======================================================================== */
static void case_s13_zero_rows(void)
{
    report("S13 リダイレクトだけ / help の短絡 / 空の source\n");

#ifndef SHELL_AS_APP
    /* sh.bin は「先頭語が内蔵でない行のリダイレクト」を一律で断る (別票の
     * 制約) ので、リダイレクトだけの行は常駐でだけ見る。 */
    fresh();
    sh_status_set(5);
    check(st("> /tmp/out") == 0, "S13 リダイレクトだけの行は 0");
    check(redir_opened("/tmp/out"), "S13 リダイレクトは実際に張られた");
#endif

    fresh();
    sh_status_set(5);
    check(st("cat -h") == 0, "S13 help の短絡は 0");

    fresh();
    sh_status_set(5);
    file_add("/empty.sh", "");
    check(st("source /empty.sh") == 0, "S13 実行行が無い source は 0");

    fresh();
    sh_status_set(5);
    s_begin(0);
    s_add("# comment only\n");
    file_add("/c.sh", s_body(0));
    check(st("source /c.sh") == 0, "S13 コメントだけの source も 0");

    /* 誤発火の裏: 開けない source は 2 */
    fresh();
    check(st("source /nosuch.sh") == SH_STATUS_USAGE, "S13 裏 開けない source は 2");
}

/* ========================================================================
 *  S14 / S15 — 入れ子と、記録の混ざり
 * ======================================================================== */
static void case_s14_s15_records(void)
{
    report("S14 入れ子 exec と前の記録\n");

#ifndef SHELL_AS_APP
    fresh();
    prog_add("a.bin", EXEC_KIND_EXITED, 3);
    prog_add("b.bin", EXEC_KIND_EXITED, 8);
    check(st("exec a.bin") == 3, "S14 exec の値は子の値");
    check(out_has("exited with"), "S14 exec は種別で印字を分ける");
    check(st("exec b.bin") == 8, "S14 次の子の値は別");
    check(st("exec nosuch.bin") == SH_STATUS_NOTFOUND,
          "S14 その後の起動失敗は 127 (前の記録を読まない)");
    check(out_has("file not found"), "S14 起動失敗の文言");

    report("S15 記録が無いときに前の値を返さない\n");

    fresh();
    prog_add("t1.bin", EXEC_KIND_EXITED, 5);
    check(st("t1") == 5, "S15 下ごしらえ: 5");
    /* GUI の子が走って記録が残らなかった場合を演じる */
    g_rec_suppress = 1;
    check(st("t1") == SH_STATUS_NOEXEC,
          "S15 記録なしは 126 (前の 5 を返さない)");
    g_rec_suppress = 0;
    check(st("t1") == 5, "S15 記録が戻れば元どおり");
#else
    fresh();
    prog_add("a.bin", EXEC_KIND_EXITED, 3);
    check(st("exec a.bin") == 0, "S14 sh.bin: DONE は 0");
    check(st("exec nosuch.bin") == SH_STATUS_NOTFOUND,
          "S14 sh.bin: 起動失敗は 127");
#endif
}

/* ========================================================================
 *  R2 — 実物の登録表を通っていることの確認
 * ======================================================================== */
static void case_r2_real_table(void)
{
    int n = 0;
    const ShellCmd *cmds = shell_get_cmds(&n);

    report("R2 実物の登録表を通す\n");
    check(n > 20, "R2 登録表に実物の件数が入っている");
    {
        int i, found_exit = 0, found_set = 0, found_source = 0;
        for (i = 0; i < n; i++) {
            if (str_eq(cmds[i].name, "exit")) found_exit = 1;
            if (str_eq(cmds[i].name, "set")) found_set = 1;
            if (str_eq(cmds[i].name, "source")) found_source = 1;
        }
        check(found_exit, "R2 `exit` が表に 1 本だけ載っている");
        check(found_set, "R2 `set` は 1 本だけ (二重登録しない)");
        check(found_source, "R2 `source` が載っている");
        /* 二重登録の検出: 同じ名前が 2 つあると先勝ちで混ざる */
        {
            int j, dup = 0;
            for (i = 0; i < n; i++)
                for (j = i + 1; j < n; j++)
                    if (str_eq(cmds[i].name, cmds[j].name)) dup = 1;
            check(!dup, "R2 同じ名前を 2 回登録していない");
        }
    }
}

/* sh_truncation_host.c から引き継いだ窓のうち、この試験ではまだ使っていない
 * ものをここで 1 か所触っておく (未使用警告を出さないため。消してしまうと
 * 次の段で反例を足すときに作り直すことになる)。 */
static void keep_windows(void)
{
    if (0) {
        (void)out_count("");
        keys_set_eof(0x0D);
        key_push_str("");
        key_push_run('a', 0);
        (void)ser_count(0);
        (void)opened_path("");
        (void)redir_opened("");
        (void)note2("", 0);
        dir_set("/");
        dir_add("x");
        (void)wq_len();
        (void)wq_takes();
        (void)wq_at(0);
    }
}

/* ======================================================================== */
/*  エントリ                                                                */
/* ======================================================================== */
void _start(void)
{
    keep_windows();
    build_api();
    sh_boot();

    case_r2_real_table();
    case_s1_exit_codes();
    case_s1b_fault_abort();
    case_s2_path_scan();
    case_s2c_s8_invalid();
    case_s3_builtins();
    case_s3b_composites();
    case_s4_unreached();
    case_s5_errexit();
    case_s6_exit();
    case_s7_expand();
    case_s9_exit_in_pipe();
    case_s10_s12_refused();
    case_profile_status();
    case_s13_zero_rows();
    case_s14_s15_records();

    if (failures) {
        report("SOME FAIL\n");
        die(1);
    }
    report("ALL PASS\n");
    die(0);
}
