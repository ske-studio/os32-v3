/* ========================================================================
 *  sh_shell_host.c — sh.bin の行再描画とスクリプトの exit を **実物のソース
 *                    で** 確かめる
 *
 *  対象票: docs/archive/gui_v13/TASK_T9_sh.md §1 D2(d) と実装レビュー
 *          (往復 1/3) の blocker 1 / 2
 *  実行:   python3 -B tools/tests/test_sh_shell.py
 *  記録:   tools/tests/t9_tdd.md
 *
 *  1 行も写さずそのまま #include する実物は 2 つ:
 *    - userland/shell/sh_redraw.inc  (SHELL_AS_APP の redraw_line)
 *    - userland/shell/cmd_script.c   (script_load / script_exec / goto / source)
 *  カーネルの代わりに置くのは KernelAPI 表と、シェルの他モジュールが出す
 *  数本 (execute_command / env_* / shell_register_cmds) だけ。
 *
 *  tools/tests/launch_host.c と同じ様式 — ホスト ILP32 GNU89、libc 無し
 *  (-nostdlib、Linux の int 0x80 で write/exit)。<string.h> は python 側が
 *  一時ディレクトリに置く薄いシム。
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

char *strncpy(char *d, const char *s, unsigned long n)
{
    unsigned long i = 0;
    while (i < n && s[i]) { d[i] = s[i]; i++; }
    while (i < n) d[i++] = '\0';
    return d;
}

int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return neg ? -v : v;
}

char *strncat(char *d, const char *s, unsigned long n)
{
    unsigned long i = 0, j = 0;
    while (d[i]) i++;
    while (j < n && s[j]) { d[i + j] = s[j]; j++; }
    d[i + j] = '\0';
    return d;
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

#define OUT_CAP  1024

static char g_out[OUT_CAP];
static int  g_out_len;

static void out_reset(void) { g_out_len = 0; g_out[0] = '\0'; }

static void out_byte(char c)
{
    if (g_out_len + 1 < OUT_CAP) g_out[g_out_len++] = c;
    g_out[g_out_len] = '\0';
}

static void out_str(const char *s) { while (*s) out_byte(*s++); }

/* 目に見えない制御文字を読める形に直してから比べる ('\n' -> "\\n" 等) */
static void out_escaped(char *dst, int cap)
{
    int i, n = 0;
    for (i = 0; i < g_out_len && n + 3 < cap; i++) {
        char c = g_out[i];
        if (c == '\n')        { dst[n++] = '\\'; dst[n++] = 'n'; }
        else if (c == '\b')   { dst[n++] = '\\'; dst[n++] = 'b'; }
        else                   dst[n++] = c;
    }
    dst[n] = '\0';
}

static int out_is(const char *want)
{
    char shown[OUT_CAP];
    out_escaped(shown, OUT_CAP);
    if (strcmp(shown, want) == 0) return 1;
    report("       got \"");
    report(shown);
    report("\" want \"");
    report(want);
    report("\"\n");
    return 0;
}

/* cmd_file.c が使う printf。書式は %s / %d / %u だけ扱えれば足りる。 */
/* 書式を 1 つ読んで可変引数を**必ず 1 つ**消費する。%u / %X / %c を読み
 * 飛ばしていたころは引数の並びがずれ、後続の %s が数値をポインタとして
 * 参照して落ちた (cmd_mnt.c の dd が "%u ... %s" を出す)。 */
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

/* 否定版 (期待どおり一致しないだけなので診断は出さない) */
static int out_is_not(const char *unwanted)
{
    char shown[OUT_CAP];
    out_escaped(shown, OUT_CAP);
    return strcmp(shown, unwanted) != 0;
}

/* ---- ごく小さなヒープと疑似ファイル ------------------------------------ */

#define POOL_SIZE  (128 * 1024)

static char g_pool[POOL_SIZE];
static unsigned long g_pool_used;

#define FILE_MAX  4

static struct { const char *path; const char *body; } g_files[FILE_MAX];
static int g_file_count;
static int g_open_fd;          /* いま開いている疑似ファイルの添字 + 1 */
static u32 g_last_alloc;       /* 直近の mem_alloc の要求サイズ (I3) */
static u32 g_blk_written;      /* dev_blk_read が書いたバイト数 (I3) */
static u32 g_blk_sector;       /* dev_blk_read が 1 セクタで書く長さ */
static int g_read_pos;         /* いま開いているファイルの読み位置 */
static int g_read_fail;        /* 1 = sys_read が失敗を返す (R6) */
static int g_write_fail;       /* 1 = sys_write が短く返す (R6) */
static int g_open_leak;        /* close されずに次の open が来たら 1 */

static void files_reset(void)
{
    g_file_count = 0;
    g_open_fd = 0;
    g_open_leak = 0;
    g_read_pos = 0;
    g_read_fail = 0;
    g_write_fail = 0;
    g_pool_used = 0;
}

static void file_add(const char *path, const char *body)
{
    if (g_file_count < FILE_MAX) {
        g_files[g_file_count].path = path;
        g_files[g_file_count].body = body;
        g_file_count++;
    }
}

/* ---- 差し替える KernelAPI の中身 --------------------------------------- */

/* B2: sys_ls のコールバック内で KAPI が呼ばれたら数える。生の int 0x80 の
 * 再入は CPL=3 で落ちるので、ホストでは「呼んだかどうか」だけを見る。 */
static int g_kapi_calls;

static void __cdecl h_kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list ap;
    const char *p = fmt;
    (void)attr;
    g_kapi_calls++;
    __builtin_va_start(ap, fmt);
    while (*p) {
        if (*p == '%') fmt_run(&p, &ap);
        else out_byte(*p++);
    }
    __builtin_va_end(ap);
}

static void __cdecl h_shell_putchar(char c, u8 attr)
{
    g_kapi_calls++;
    (void)attr;
    out_byte(c);
}
static void __cdecl h_shell_print_utf8(const char *s, u8 attr)
{
    g_kapi_calls++;
    (void)attr;
    out_str(s);
}

static void *__cdecl h_mem_alloc(u32 size)
{
    char *p;
    unsigned long n = (unsigned long)size;

    g_kapi_calls++;
    n = (n + 7UL) & ~7UL;
    if (g_pool_used + n > (unsigned long)POOL_SIZE) return (void *)0;
    p = g_pool + g_pool_used;
    g_pool_used += n;
    g_last_alloc = size;
    return (void *)p;
}

static void __cdecl h_mem_free(void *p) { g_kapi_calls++; (void)p; }

static int __cdecl h_sys_open(const char *path, int flags)
{
    int i;
    (void)flags;
    if (g_open_fd) g_open_leak = 1;
    g_read_pos = 0;
    for (i = 0; i < g_file_count; i++) {
        if (strcmp(g_files[i].path, path) == 0) {
            g_open_fd = i + 1;
            return i + 1;
        }
    }
    /* 未知のパスは「新規作成できた」ことにする (cp / mv の宛先) */
    if (flags != KAPI_O_RDONLY) {
        file_add(path, "");
        g_open_fd = g_file_count;
        return g_file_count;
    }
    return -1;
}

static int __cdecl h_sys_write(int fd, const void *buf, u32 size)
{
    (void)fd; (void)buf;
    if (g_write_fail) return 0;      /* 短く書けた = 失敗 */
    return (int)size;
}

static int __cdecl h_sys_read(int fd, void *buf, u32 size)
{
    const char *src;
    char *dst = (char *)buf;
    int n = 0;
    if (g_read_fail) return -1;
    if (fd <= 0 || fd > g_file_count) return -1;
    src = g_files[fd - 1].body + g_read_pos;
    while (src[n] && (u32)n < size) { dst[n] = src[n]; n++; }
    g_read_pos += n;       /* 次の read は EOF (0) — コピーのループが終わる */
    return n;
}

static void __cdecl h_sys_close(int fd) { (void)fd; g_open_fd = 0; }

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
static int __cdecl h_sys_ls(const char *path, void *cb, void *ctx);
static int __cdecl h_sys_isatty(int fd) { g_kapi_calls++; (void)fd; return 1; }
static const char *__cdecl h_sys_getcwd(void) { return "/cwd"; }

/* I3: ATAPI と同じく 1 セクタ g_blk_sector バイトを書く。確保が足りなければ
 * プールの隣を汚すので、直後の番人バイトで溢れを見る。 */
static int __cdecl h_dev_blk_read(const char *dev, u32 lba, int count, void *buf)
{
    u32 i, n;
    (void)dev; (void)lba;
    n = (u32)count * g_blk_sector;
    for (i = 0; i < n; i++) ((u8 *)buf)[i] = (u8)0xCC;
    g_blk_written = n;
    return 0;
}

/* sh_launch がパイプ判定を抜けた先で NULL を踏まないための最小の受け皿。
 * GUI 外を模して INVAL を返す (この試験では起動そのものは見ない —
 * 起動の 4 経路は tools/tests/sh_launch_host.c の担当)。 */
static i32 __cdecl h_launch_req(const char *cmdline)
{
    (void)cmdline;
    g_kapi_calls++;
    return OS32_ERR_INVAL;
}
static i32 __cdecl h_launch_poll(i32 token, i32 *status)
{
    (void)token;
    if (status) *status = LAUNCH_ST_DONE;
    return 0;
}
static i32 __cdecl h_sys_yield(void) { return 0; }
/* script_exec の行ごとの譲り (cmd_script.c) が間引きに引く tick。呼ぶたびに
 * 進める — 止まったままだと「同じ tick では譲らない」で 1 回しか出ない。
 * ここに譲りの窓は置かない (回数の試験は sh_truncation_host.c 側の 30)。 */
static u32 g_tick;
static u32 __cdecl h_get_tick(void) { return g_tick++; }
static int __cdecl h_kbd_trygetkey(void) { return -1; }
/* 覗くだけ (KAPI v54)。script_exec の毎行の ESC 監視がこれを引く。
 * ここに打鍵の模型は要らない (打ち切りの試験は sh_truncation_host.c 側)。 */
static int __cdecl h_kbd_peekkey(void) { return -1; }
static int __cdecl h_kbd_getchar(void)   { return 0x0D; }

/* blocker 1 の肝: GUI 中は座標が動かない (K6C-2) ので、行の再描画が
 * console_* を 1 度でも引いたら落とす。 */
static int g_cursor_calls;

static int  __cdecl h_console_get_cursor_x(void)   { g_cursor_calls++; return 0; }
static int  __cdecl h_console_get_cursor_y(void)   { g_cursor_calls++; return 0; }
static void __cdecl h_console_set_cursor(int x, int y)
{
    (void)x; (void)y;
    g_cursor_calls++;
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
    g_fake.dev_blk_read = h_dev_blk_read;
    g_fake.launch_req = h_launch_req;
    g_fake.launch_poll = h_launch_poll;
    g_fake.sys_yield = h_sys_yield;
    g_fake.get_tick = h_get_tick;
    g_fake.kbd_trygetkey = h_kbd_trygetkey;
    g_fake.kbd_peekkey = h_kbd_peekkey;
    g_fake.kbd_getchar = h_kbd_getchar;
    g_fake.console_get_cursor_x = h_console_get_cursor_x;
    g_fake.console_get_cursor_y = h_console_get_cursor_y;
    g_fake.console_set_cursor = h_console_set_cursor;
    g_api = &g_fake;
}

/* ---- ui.c の周辺 (sh_redraw.inc が引くもの) ---------------------------- */

int sh_exit_flag = 0;
/* 票 TASK_EXIT_STATUS: 要求と値は別の変数。`$?` の実体もここに置く
 * (main.c を取り込まない試験なので、実体だけ同じ形で用意する)。 */
int sh_exit_code = 0;
static int g_stub_status = 0;
int sh_status_get(void) { return g_stub_status; }
void sh_status_set(int status) { g_stub_status = status; }
int sh_status_from_kind(int kind, int code)
{
    switch (kind) {
    case EXEC_KIND_EXITED:    return code & 0xFF;
    case EXEC_KIND_FAULT:     return SH_STATUS_FAULT;
    case EXEC_KIND_ABORTED:   return SH_STATUS_ABORTED;
    case EXEC_KIND_NOT_FOUND: return SH_STATUS_NOTFOUND;
    default:                  return SH_STATUS_NOEXEC;
    }
}
int sh_exit_arg(int argc, char **argv, int *code)
{
    const char *s;
    int v = 0, digits = 0;
    if (argc < 2) { *code = sh_status_get(); return 0; }
    if (argc > 2) return -1;
    for (s = argv[1]; *s; s++) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        digits++;
        if (v > 255) return -1;
    }
    if (digits == 0) return -1;
    *code = v;
    return 0;
}
static int prev_draw_len = 0;

/* main.c の「断った印」(票 TASK_SH_TRUNCATION §2-1)。この試験は
 * execute_command をスタブにしていて切り詰めの経路を通らないので、実体だけ
 * 置く (中身は main.c と同じ)。印を読むのは cmd_script.c の script_exec。 */
int sh_refused_flag = 0;
void sh_refuse_mark(void) { sh_refused_flag = 1; }
void sh_refuse(const char *what, int limit)
{
    g_api->kprintf(ATTR_RED, "%s too long (max %d)\n", what, limit);
    sh_refused_flag = 1;
}
int sh_refused_take(void)
{
    int r = sh_refused_flag;
    sh_refused_flag = 0;
    return r;
}
int sh_refused_peek(void) { return sh_refused_flag; }

static void show_prompt(void) { out_str("sh> "); }

#include "../../userland/shell/sh_redraw.inc"
#include "../../userland/shell/sh_pipe.inc"
#include "../../userland/shell/sh_ls.inc"
#include "../../userland/shell/sh_launch.inc"
#include "../../userland/shell/sh_args.inc"

/* ---- 実物のスクリプトエンジン ------------------------------------------ */

#include "../../userland/shell/cmd_script.c"
#include "../../userland/shell/cmd_fs_shared.c"
#include "../../userland/shell/cmd_file.c"
#include "../../userland/shell/cmd_mnt.c"
#include "../../userland/shell/cmd_env.c"
#include "../../userland/shell/cmd_sys.c"
#include "../../userland/shell/kbd_watch.c"   /* kbdstat -w の行 (票 TASK_KBD_NAV §3) */

/* ---- シェルの他モジュールの代わり -------------------------------------- */

void shell_register_cmds(const ShellCmd *cmds) { (void)cmds; }
void shell_print_help(const char *cmd_name) { (void)cmd_name; }

/* 実行した行の記録。source / goto / exit だけ意味を持たせ、他は記録のみ。 */
#define TRACE_MAX   16
#define TRACE_LINE  64

static char g_trace[TRACE_MAX][TRACE_LINE];
static int  g_trace_n;
static int  g_trace_over;          /* 走りすぎ (goto の無限ループ) の印 */

static void trace_reset(void) { g_trace_n = 0; g_trace_over = 0; }

static int word_is(const char *line, const char *word)
{
    int i = 0;
    while (word[i] && line[i] == word[i]) i++;
    if (word[i]) return 0;
    return line[i] == '\0' || line[i] == ' ';
}

int execute_command(const char *cmd)
{
    int i;

    if (g_trace_n >= TRACE_MAX) { g_trace_over = 1; return 0; }
    for (i = 0; cmd[i] && i < TRACE_LINE - 1; i++) g_trace[g_trace_n][i] = cmd[i];
    g_trace[g_trace_n][i] = '\0';
    g_trace_n++;

    if (word_is(cmd, "exit")) {
        /* 票 TASK_EXIT_STATUS: 値を先に、要求をあとで。 */
        sh_exit_code = 0;
        sh_status_set(0);
        sh_exit_flag = 1;
        return 0;
    }
    if (word_is(cmd, "source")) {
        int r = script_source_file(cmd + 7);
        return (r < 0) ? SH_STATUS_USAGE : r;
    }
    if (word_is(cmd, "goto")) {
        char label[TRACE_LINE];
        char *argv[3];
        int n = 0;
        /* "goto " は 5 バイト ("source " が 7)。7 のままだと `goto loop` を
         * `op` と読み、ラベルが見つからず script_abort_flag で止まってしまう
         * ので、巻き戻しからの exit 脱出を検査できていなかった。 */
        while (cmd[5 + n] && n < TRACE_LINE - 1) { label[n] = cmd[5 + n]; n++; }
        label[n] = '\0';
        argv[0] = (char *)"goto";
        argv[1] = label;
        argv[2] = (char *)0;
        return cmd_goto(2, argv);
    }
    return 0;
}

static int trace_is(const char *joined)
{
    char got[TRACE_MAX * TRACE_LINE];
    int n = 0, i, j;
    for (i = 0; i < g_trace_n; i++) {
        if (i > 0) got[n++] = '|';
        for (j = 0; g_trace[i][j]; j++) got[n++] = g_trace[i][j];
    }
    got[n] = '\0';
    if (!g_trace_over && strcmp(got, joined) == 0) return 1;
    report("       got \"");
    report(got);
    report(g_trace_over ? "\" (走りすぎ) want \"" : "\" want \"");
    report(joined);
    report("\"\n");
    return 0;
}

/* ========================================================================
 *  1. redraw_line — 延長は差分だけ、それ以外は行の作り直し (blocker 1)
 * ======================================================================== */
static void case_redraw_extend(void)
{
    report("1 redraw_line: 純粋な延長は差分バイトだけ (TAB 補完)\n");

    /* いま画面に "sh> l" が出ている状態から "ls" へ補完された */
    sh_set_drawn("l", 1, 1);
    out_reset();
    g_cursor_calls = 0;
    redraw_line("ls", 2, 2);
    check(out_is("s"), "1a 増えた 1 バイトだけを印字する");
    check(sh_drawn_len == 2, "1b 写しが伸びる");

    /* 何も増えなければ何も出さない */
    out_reset();
    redraw_line("ls", 2, 2);
    check(out_is(""), "1c 変化なしなら無音");

    check(g_cursor_calls == 0, "1d console_set/get_cursor を 1 度も引かない");
}

static void case_redraw_rebuild(void)
{
    report("2 redraw_line: 延長でなければ行を作り直す\n");

    /* 履歴で丸ごと別の行に差し替わった */
    sh_set_drawn("ls -la", 6, 6);
    out_reset();
    g_cursor_calls = 0;
    redraw_line("cat", 3, 3);
    check(out_is("\\nsh> cat"), "2a 改行 + プロンプト + 行全体");
    check(g_cursor_calls == 0, "2a' 作り直しも座標を引かない");

    /* 短くなる向き (BS) も作り直し */
    sh_set_drawn("cat", 3, 3);
    out_reset();
    redraw_line("ca", 2, 2);
    check(out_is("\\nsh> ca"), "2b 縮む側も作り直す");

    /* 候補一覧を挟んだ後は延長でも作り直す */
    sh_set_drawn("ls", 2, 2);
    /* ui.c の sh_drop_drawn() の中身 (行が流れた印) */
    sh_set_drawn((const char *)0, -1, -1);
    out_reset();
    redraw_line("ls", 2, 2);
    check(out_is("\\nsh> ls"), "2c 行が流れた印のあとは必ず作り直す");

    /* カーソルが行末より前なら BS で戻す (端末の BS は消さずに左へ) */
    sh_set_drawn("abc", 3, 3);
    out_reset();
    redraw_line("abd", 3, 1);
    check(out_is("\\nsh> abd\\b\\b"), "2d 行末より前は BS で戻す");
}

/* ========================================================================
 *  3. 画面カーソルが行末に無いときは延長しない (再レビュー blocker 2)
 *
 *  CUI 直起動の `hel` -> LEFT -> TAB。LEFT は内容を変えないので写しは `hel`
 *  のままだが、画面カーソルは 1 つ左にある。内容の前方一致だけで「延長」と
 *  判定して差分 `p ` を出すと、画面は `hep ` (CUI の BS は消す) でバッファは
 *  `help ` という食い違いになる。
 * ======================================================================== */
static void case_redraw_cursor_not_at_end(void)
{
    report("3 画面カーソルが行末に無ければ延長しない (hel -> LEFT -> TAB)\n");

    /* 補完後に redraw_line が写しへ残したカーソル位置をそのまま使う経路 */
    sh_set_drawn("hel", 3, 2);              /* LEFT で 1 つ左に居る */
    out_reset();
    g_cursor_calls = 0;
    redraw_line("help ", 5, 5);             /* TAB 補完の結果 */
    check(out_is("\\nsh> help "), "3a 差分ではなく行を作り直す");
    check(g_cursor_calls == 0,   "3b 作り直しも座標を引かない");
    check(sh_drawn_pos == 5,     "3c 写しのカーソルは新しい行末へ");

    /* 行末より前で redraw した直後の TAB も同じ (BS で戻した状態が残る) */
    sh_set_drawn("abc", 3, 3);
    out_reset();
    redraw_line("abc", 3, 1);               /* 行末より前へ戻す */
    check(sh_drawn_pos == 1,     "3d redraw はカーソル位置も写しへ残す");
    out_reset();
    redraw_line("abcd", 4, 4);              /* ここで延長したら壊れる */
    check(out_is("\\nsh> abcd"), "3e その次も延長せず作り直す");

    /* ui.c の LEFT / RIGHT / HOME が呼ぶ sh_drop_drawn() の効き目 */
    sh_set_drawn("hel", 3, 3);
    sh_set_drawn((const char *)0, -1, -1);
    out_reset();
    redraw_line("help ", 5, 5);
    check(out_is("\\nsh> help "), "3f 写しを捨てた後も作り直す");
}

/* ========================================================================
 *  4. 行末 BS は画面からも消す (往復 3 の blocker)
 *
 *  端末の BS はカーソルを 1 セル左へ動かすだけでセルを消さない
 *  (libos32term の model.rs)。BS だけを出して短縮後の写しを確定すると、
 *  `echo abc` -> BS BS -> `x` で画面 `echo axc` / バッファ `echo ax` と
 *  食い違う。BS + 空白 + BS で消してから写しを確定する。
 * ======================================================================== */
static void case_backspace_erases(void)
{
    char line[8];

    report("4 行末 BS は BS + 空白 + BS で画面からも消す\n");

    /* abc -> BS -> BS -> x */
    line[0] = 'a'; line[1] = 'b'; line[2] = 'c'; line[3] = '\0';
    sh_set_drawn(line, 3, 3);
    out_reset();
    g_cursor_calls = 0;

    sh_backspace_tail(line, 2);             /* 'c' を消す */
    check(sh_drawn_len == 2 && sh_drawn_pos == 2, "4a 写しは縮んで末尾に居る");
    sh_backspace_tail(line, 1);             /* 'b' を消す */

    /* ここから先は ui.c の直接印字 (行末への 1 文字追加) と同じ */
    line[1] = 'x'; line[2] = '\0';
    g_api->shell_putchar('x', ATTR_WHITE);
    sh_set_drawn(line, 2, 2);

    check(out_is("\\b \\b\\b \\bx"), "4b 出力は BS 空白 BS の 2 回 + 'x'");
    check(sh_drawn_len == 2 && sh_drawn[0] == 'a' && sh_drawn[1] == 'x',
                                            "4c 写しは ax");
    check(g_cursor_calls == 0,              "4d 座標 KAPI を引かない");

    /* 3 バイト列の先頭 (全角) は 2 セルぶん */
    line[0] = (char)0xE3; line[1] = '\0';
    sh_set_drawn(line, 1, 1);
    out_reset();
    sh_backspace_tail(line, 0);
    check(out_is("\\b\\b  \\b\\b"),   "4e 全角の先頭は 2 セルぶん消す");
}

/* ========================================================================
 *  5. パイプバッファは sh 自身の .bss から配る (往復 5 の blocker)
 *
 *  `sys_pipe_get_buf()` が返すのはカーネル帯 (fs/pipe_buffer.c の kmalloc)
 *  で、それを `sys_redirect_fd_buf()` へ渡すと `ring3_ptr_ok` に落ちて
 *  CPL=3 の sh が畳まれる (`sh> echo a | cat`)。返るポインタが sh の静的
 *  配列の中にあること、解放後に再確保できること、上限を超えたら失敗する
 *  ことを見る。
 * ======================================================================== */
static void case_pipe_buffers(void)
{
    int a, b, c;
    u8 *pa, *pb;
    u8 *pool_lo = &sh_pipe_pool[0][0];
    u8 *pool_hi = &sh_pipe_pool[SH_PIPE_SLOTS - 1][PIPE_BUF_SIZE - 1];

    report("5 パイプバッファは sh の .bss から (カーネル帯を渡さない)\n");

    a = sh_pipe_alloc();
    b = sh_pipe_alloc();
    check(a >= 0 && b >= 0 && a != b, "5a 2 本を別々に確保できる");

    pa = sh_pipe_get_buf(a);
    pb = sh_pipe_get_buf(b);
    check(pa >= pool_lo && pa <= pool_hi,
                                      "5b 返るのは sh の静的配列の中");
    check(pb >= pool_lo && pb <= pool_hi && pb != pa,
                                      "5c 2 本目も配列内で別の番地");

    /* 上限超過 */
    c = sh_pipe_alloc();
    check(c < 0,                      "5d 上限を超えたら負を返す");
    check(sh_pipe_get_buf(c) == (u8 *)0,
                                      "5e 無効なスロットは NULL");

    /* 解放後に再確保できる */
    sh_pipe_free(a);
    check(sh_pipe_get_buf(a) == (u8 *)0, "5f 解放した枠は NULL になる");
    c = sh_pipe_alloc();
    check(c == a && sh_pipe_get_buf(c) == pa,
                                      "5g 解放後に同じ枠を再確保できる");

    sh_pipe_free(b);
    sh_pipe_free(c);
    check(sh_pipe_alloc() >= 0,       "5h 全部返せばまた確保できる");
    sh_pipe_free(0);
    sh_pipe_free(1);
}

/* ========================================================================
 *  6. sys_ls のコールバックは KAPI を呼ばない (往復 6 の B2)
 *
 *  CPL=3 で sys_ls のコールバックから int 0x80 を再入すると落ちる
 *  (実機で `find /etc -name filetypes` が [Process crashed])。sh の内蔵 ls と
 *  glob 展開はコールバックを写し取りだけにして、戻ってから処理する。
 * ======================================================================== */
static void case_ls_callback_no_kapi(void)
{
    DirEntry_Ext e;
    DirEntry_Ext got;
    int i;

    report("6 sys_ls のコールバックは KAPI を呼ばない\n");
    sh_ls_reset();
    g_kapi_calls = 0;

    /* SH_LS_MAX を 2 つ超える件数を流す */
    for (i = 0; i < SH_LS_MAX + 2; i++) {
        int n = 0;
        e.name[n++] = 'f';
        e.name[n++] = (char)('0' + (i % 10));
        e.name[n] = '\0';
        e.size = (u32)i;
        e.type = (i == 0) ? OS32_FILE_TYPE_DIR : OS32_FILE_TYPE_FILE;
        sh_ls_collect_cb(&e, (void *)0);
    }

    check(g_kapi_calls == 0,          "6a コールバック内の KAPI 呼び出しは 0 回");
    check(sh_ls_count_get() == SH_LS_MAX, "6b 上限まで写す");
    check(sh_ls_dropped() == 2,       "6c 溢れた分は数だけ数える");

    /* 写しから元の形に戻せる (呼び手は既存のコールバックへ流せる) */
    got.name[0] = '\0';
    got.size = 0xFFFFFFFFu;
    got.type = 0;
    sh_ls_fill(1, &got);
    check(got.name[0] == 'f' && got.name[1] == '1' && got.name[2] == '\0' &&
          got.size == 1 && got.type == OS32_FILE_TYPE_FILE,
                                      "6d 名前 / サイズ / 種別を写している");

    sh_ls_reset();
    check(sh_ls_count_get() == 0 && sh_ls_dropped() == 0,
                                      "6e reset で空になる");
}

/* ========================================================================
 *  7. パイプの中から外部プログラムは起動しない (往復 6 の B4)
 *
 *  先頭語だけの事前判定は `exec /bin/sh.bin | echo tail` を通してしまうので、
 *  最終起動口 (sh_launch) で確かめる。
 * ======================================================================== */
static void case_pipe_blocks_launch(void)
{
    int rc;

    report("7 パイプ実行中は sh_launch が断る\n");
    out_reset();
    sh_pipeline_enter();
    rc = sh_launch("/bin/kbd_echo.bin");
    check(rc < 0,                     "7a 負を返す");
    check(out_is("sh: pipe to external command is not supported\\n"),
                                      "7b 理由を出す");
    sh_pipeline_leave();

    /* 入れ子 (source 経由のパイプ) も数えているので、1 段抜けても残る */
    out_reset();
    sh_pipeline_enter();
    sh_pipeline_enter();
    sh_pipeline_leave();
    rc = sh_launch("/bin/kbd_echo.bin");
    check(rc < 0,                     "7c 入れ子は数えるので外側でもまだ断る");
    sh_pipeline_leave();
}

/* ========================================================================
 *  8. ask の行末 BS も画面から消す (往復 6 の B5)
 * ======================================================================== */
static void case_ask_backspace(void)
{
    report("8 ask の BS も BS + 空白 + BS\n");
    out_reset();
    sh_erase_cells('c');
    check(out_is("\\b \\b"),        "8a 半角は 1 セル");
    out_reset();
    sh_erase_cells((char)0xE3);
    check(out_is("\\b\\b  \\b\\b"), "8b 3 バイト列の先頭は 2 セル");
}

/* ========================================================================
 *  9. リダイレクトを張ったまま外部は起こさない (往復 7 の R2)
 *
 *  内蔵 `exec` / `if` / `time` / 外部行を含む `source` は、execute_single の
 *  事前判定を通った**後**にリダイレクトを張ってから sh_launch へ来る。
 * ======================================================================== */
static void case_redirect_blocks_launch(void)
{
    int rc;

    report("9 リダイレクト中は sh_launch が断る (exec cmd > file)\n");
    sh_redirect_clear();
    out_reset();
    rc = sh_launch("/bin/kbd_echo.bin");
    check(rc == EXEC_ERR_NOT_FOUND || rc == EXEC_ERR_GENERAL,
                                      "9a 印が下りていれば素通し");

    sh_redirect_mark();
    out_reset();
    rc = sh_launch("/bin/kbd_echo.bin");
    check(rc < 0,                     "9b 印が立っていれば負を返す");
    check(out_is("sh: redirect to external command is not supported\\n"),
                                      "9c 理由を出す");
    sh_redirect_clear();
    out_reset();
    rc = sh_launch("/bin/kbd_echo.bin");
    check(out_is_not("sh: redirect to external command is not supported\\n"),
                                      "9d sh_redirect_clear で印が下りる"
                                      " (reset_all_redirects が呼ぶ)");
}

/* ========================================================================
 *  10. 写しの名前は 255B まで切れない (往復 7 の R3)
 * ======================================================================== */
static void case_ls_long_name(void)
{
    DirEntry_Ext e;
    DirEntry_Ext got;
    int i, ok;

    report("10 写しは 255B の名前を切らない\n");
    for (i = 0; i < OS32_MAX_PATH - 1; i++) e.name[i] = 'n';
    e.name[OS32_MAX_PATH - 1] = '\0';
    e.size = 7;
    e.type = OS32_FILE_TYPE_FILE;

    sh_ls_reset();
    sh_ls_collect_cb(&e, (void *)0);
    got.name[0] = 'x';
    sh_ls_fill(0, &got);

    ok = 1;
    for (i = 0; i < OS32_MAX_PATH - 1; i++) if (got.name[i] != 'n') ok = 0;
    if (got.name[OS32_MAX_PATH - 1] != '\0') ok = 0;
    check(ok,                          "10a 255 バイトそのまま写る");
    check((int)strlen(got.name) == OS32_MAX_PATH - 1,
                                       "10b 長さも変わらない");
    sh_ls_reset();
}

/* ========================================================================
 *  11. glob の上限は「一致した数」に掛かる (往復 7 の R4)
 *
 *  不一致が先に並ぶディレクトリで列挙順の先頭 N 件を切ると、末尾の一致を
 *  取り逃がして `cat /tmp/d/target*` が未展開のまま渡ってしまう。
 * ======================================================================== */
static int g_dir_n;                    /* 偽 sys_ls が返すエントリ数 */
static int g_dir_match_tail;           /* 末尾の何件を "target*" にするか */

static int __cdecl h_sys_ls(const char *path, void *cb, void *ctx)
{
    DirEntry_Ext e;
    DirCallback f = (DirCallback)cb;
    int i, k;

    (void)path;
    for (i = 0; i < g_dir_n; i++) {
        const char *base = (i >= g_dir_n - g_dir_match_tail) ? "target" : "other";
        k = 0;
        while (base[k]) { e.name[k] = base[k]; k++; }
        e.name[k++] = (char)('0' + (i % 10));
        e.name[k++] = (char)('0' + ((i / 10) % 10));
        e.name[k] = '\0';
        e.size = (u32)i;
        e.type = OS32_FILE_TYPE_FILE;
        f(&e, ctx);
    }
    return 0;
}

static void case_glob_matches_only(void)
{
    static char line[64];
    static char *argv[MAX_ARGS];
    static char *alloc[MAX_ARGS];
    int argc = 0, nalloc = 0, i;

    report("11 glob の上限は一致した数に掛かる\n");

    /* 先頭に不一致 200 件、末尾に一致 1 件 */
    g_dir_n = 201;
    g_dir_match_tail = 1;
    sh_glob_failed = 0;
    out_reset();
    { const char *src = "cat target*"; i = 0;
      while (src[i]) { line[i] = src[i]; i++; } line[i] = '\0'; }
    parse_args_and_glob(line, argv, &argc, MAX_ARGS, alloc, &nalloc);

    check(sh_glob_failed == 0,        "11a 諦めていない");
    check(argc == 2,                  "11b cat + 一致 1 件に展開される");
    {
        int star = 0;
        if (argc == 2) { for (i = 0; argv[1][i]; i++) if (argv[1][i] == '*') star = 1; }
        check(argc == 2 && !star && argv[1][0] == 't',
              "11c 未展開の target* ではなく実体名に化けている");
    }
    for (i = 0; i < nalloc; i++) g_api->mem_free(alloc[i]);

    /* 一致が写し取りの上限を超えたら行ごと捨てる */
    g_dir_n = SH_LS_MAX + 5;
    g_dir_match_tail = SH_LS_MAX + 5;
    sh_glob_failed = 0;
    argc = 0; nalloc = 0;
    out_reset();
    { const char *src = "cat target*"; i = 0;
      while (src[i]) { line[i] = src[i]; i++; } line[i] = '\0'; }
    parse_args_and_glob(line, argv, &argc, MAX_ARGS, alloc, &nalloc);

    check(sh_glob_failed == 1,        "11d 多すぎたら印を立てる");
    check(out_is("sh: glob: too many matches\\n"), "11e 理由を出す");
    for (i = 0; i < nalloc; i++) g_api->mem_free(alloc[i]);
    sh_glob_failed = 0;
    g_dir_n = 0;
    g_dir_match_tail = 0;
}

/* ========================================================================
 *  12. argv[] の 1 つ手前で止める (往復 7 の R7、常駐にも効く)
 * ======================================================================== */
static void case_argv_bound(void)
{
    static char line[MAX_ARGS * 4 + 16];
    static char *argv[MAX_ARGS];
    static char guard[16];
    int argc = 0, nalloc = 0;
    int i, n = 0, ok, rc;
    static char *alloc[MAX_ARGS];

    report("12 argv[] は max_args - 1 個までしか詰めない\n");

    for (i = 0; i < (int)sizeof(guard); i++) guard[i] = (char)0x5A;
    for (i = 0; i < MAX_ARGS; i++) argv[i] = (char *)0xDEADBEEF;

    { const char *c = "echo"; while (*c) line[n++] = *c++; }
    for (i = 0; i < MAX_ARGS + 8; i++) { line[n++] = ' '; line[n++] = 'a'; }
    line[n] = '\0';

    out_reset();
    rc = parse_args_and_glob(line, argv, &argc, MAX_ARGS, alloc, &nalloc);

    check(rc < 0,                     "12a 多すぎる行は負を返す (I1)");
    check(out_is("sh: too many arguments\\n"), "12b 理由を出す");
    /* 段 3 (PM 決裁 2026-09-16): 捨てるときは印も立てる。立てないと
     * スクリプトが次の行へ落ちる。印の寿命は 1 行ぶんなので、この試験
     * (execute_command がスタブで入口の掃除が無い) では明示的に下ろす。 */
    check(sh_refused_flag == 1,       "12b2 捨てるときは印を立てる (I1)");
    sh_refused_flag = 0;
    check(argc <= MAX_ARGS - 1,       "12c 格納は max_args - 1 個まで (R7)");
    check(argv[MAX_ARGS - 1] == (char *)0xDEADBEEF,
                                      "12d 最後の枠は NUL 終端用に空いている");
    ok = 1;
    for (i = 0; i < (int)sizeof(guard); i++) if (guard[i] != (char)0x5A) ok = 0;
    check(ok,                         "12e 隣の static を壊していない");
    for (i = 0; i < nalloc; i++) g_api->mem_free(alloc[i]);

    /* 上限に収まる行はそのまま通る */
    n = 0;
    { const char *c = "echo a b c"; while (*c) line[n++] = *c++; }
    line[n] = '\0';
    argc = 0; nalloc = 0;
    out_reset();
    rc = parse_args_and_glob(line, argv, &argc, MAX_ARGS, alloc, &nalloc);
    check(rc == 0 && argc == 4,       "12f 普通の行は 0 を返す");
    for (i = 0; i < nalloc; i++) g_api->mem_free(alloc[i]);

    /* 複数 glob の**合計**が上限を超えても捨てる */
    g_dir_n = SH_LS_MAX;
    g_dir_match_tail = SH_LS_MAX;
    n = 0;
    { const char *c = "cat target* target* target*"; while (*c) line[n++] = *c++; }
    line[n] = '\0';
    argc = 0; nalloc = 0;
    sh_glob_failed = 0;
    out_reset();
    rc = parse_args_and_glob(line, argv, &argc, MAX_ARGS, alloc, &nalloc);
    check(rc < 0,                     "12g glob の合計超過も行ごと捨てる");
    check(sh_refused_flag == 1,       "12h 合計超過でも印を立てる (I1)");
    for (i = 0; i < nalloc; i++) g_api->mem_free(alloc[i]);
    sh_refused_flag = 0;
    sh_glob_failed = 0;
    g_dir_n = 0;
    g_dir_match_tail = 0;
}

/* ========================================================================
 *  12b. 同一ファイル判定は綴りを畳んでから (往復 8 の I2)
 * ======================================================================== */
static void case_path_normalize(void)
{
    char out[PATH_MAX_LEN];

    report("12' 同一ファイル判定は . / .. / // を畳んでから\n");

    check(sh_path_normalize("/host/./a", out, PATH_MAX_LEN) == 0 &&
          strcmp(out, "/host/a") == 0,        "12'a /host/./a -> /host/a");
    check(sh_path_normalize("/a/../b", out, PATH_MAX_LEN) == 0 &&
          strcmp(out, "/b") == 0,             "12'b /a/../b -> /b");
    check(sh_path_normalize("//x///y//", out, PATH_MAX_LEN) == 0 &&
          strcmp(out, "/x/y") == 0,           "12'c 連続 / を畳む");
    check(sh_path_normalize("/../..", out, PATH_MAX_LEN) == 0 &&
          strcmp(out, "/") == 0,              "12'd ルートより上へは行かない");
    check(sh_path_normalize("rel/f", out, PATH_MAX_LEN) == 0 &&
          strcmp(out, "/cwd/rel/f") == 0,     "12'e 相対は cwd を前置");

    files_reset();
    check(fs_same_file("/host/a", "/host/./a") == 1,
                                              "12'f 畳めば同一と分かる");
    check(fs_same_file("/host/a", "/host/b") == 0,
                                              "12'g 別ファイルは非同一");
}

/* ========================================================================
 *  12c. dd のセクタ長 (往復 8 の I3) と wildcard の停止性 (I4)
 * ======================================================================== */
static void case_sector_and_wildcard(void)
{
    report("12'' dd のセクタ長と wildcard の停止性\n");

    /* 実物の cmd_dd を回して、確保した長さが ATAPI の 1 セクタに足りるか */
    {
        char *argv[6];
        argv[0] = (char *)"dd";
        argv[1] = (char *)"cd0";
        argv[2] = (char *)"lba=0";
        argv[3] = (char *)"count=1";
        argv[4] = (char *)"file=/dd.out";
        argv[5] = (char *)0;

        files_reset();
        out_reset();
        g_blk_sector = SYS_CDROM_SECTOR_SIZE;
        g_last_alloc = 0;
        g_blk_written = 0;
        cmd_dd(5, argv);
        check(g_last_alloc >= g_blk_written,
              "12''a cd0 は ATAPI の 1 セクタぶん確保する");
        check(g_last_alloc == SYS_CDROM_SECTOR_SIZE,
              "12''b 確保長は 2048B");

        argv[1] = (char *)"fd0";
        files_reset();
        out_reset();
        g_blk_sector = SYS_BLOCK_SECTOR_SIZE;
        g_last_alloc = 0;
        g_blk_written = 0;
        cmd_dd(5, argv);
        check(g_last_alloc == SYS_BLOCK_SECTOR_SIZE,
              "12''b' fd は 1024B のまま (hd は I-5 で 512B)");
    }

    /* I4: 病的パターン。再帰版はここで事実上停止した。 */
    {
        static char pat[64];
        static char name[64];
        int i, n = 0;
        for (i = 0; i < 20; i++) { pat[n++] = '*'; pat[n++] = 'a'; }
        pat[n++] = 'b';
        pat[n] = '\0';
        for (i = 0; i < 40; i++) name[i] = 'a';
        name[40] = '\0';
        check(wildcard_match(pat, name) == 0, "12''c 病的パターンが即座に不一致");
    }

    /* 既存の意味は変えていない */
    check(wildcard_match("*.bin", "ls.bin") == 1,   "12''d *.bin が当たる");
    check(wildcard_match("*.bin", "ls.txt") == 0,   "12''e 拡張子違いは外れる");
    check(wildcard_match("a*b", "ab") == 1,         "12''f * は 0 文字でもよい");
    check(wildcard_match("a?c", "abc") == 1,        "12''g ? は 1 文字");
    check(wildcard_match("a?c", "ac") == 0,         "12''h ? は 0 文字に当たらない");
    check(wildcard_match("*", "") == 1,             "12''i * は空にも当たる");
    check(wildcard_match("", "x") == 0,             "12''j 空パターンは空だけ");
    check(wildcard_match("t*t*t", "target_t") == 1, "12''k 複数の * が戻れる");
}

/* ========================================================================
 *  12'''. 往復 9 — C-1 (glob の '*' 優先) と I-1〜I-6
 * ======================================================================== */
static void case_star_precedence(void)
{
    report("12''' パターンの '*' は通常文字の比較より先に見る (C-1)\n");

    check(wildcard_match("*", "*dest") == 1,   "C1a * は * で始まる名前にも当たる");
    check(wildcard_match("*d*", "*dest") == 1, "C1b 途中に * があっても当たる");
    check(wildcard_match("a*", "*dest") == 0,  "C1c 先頭が違えば当たらない");
    check(wildcard_match("*t", "*dest") == 1,  "C1d 末尾一致");
    check(wildcard_match("**", "*") == 1,      "C1e 連続 * も当たる");

    /* 旧版と同じ 8 ケース (意味を変えていないこと) */
    check(wildcard_match("*.bin", "ls.bin") == 1,   "C1f *.bin が当たる");
    check(wildcard_match("*.bin", "ls.txt") == 0,   "C1g 拡張子違いは外れる");
    check(wildcard_match("a*b", "ab") == 1,         "C1h * は 0 文字でもよい");
    check(wildcard_match("a?c", "abc") == 1,        "C1i ? は 1 文字");
    check(wildcard_match("a?c", "ac") == 0,         "C1j ? は 0 文字に当たらない");
    check(wildcard_match("*", "") == 1,             "C1k * は空にも当たる");
    check(wildcard_match("", "x") == 0,             "C1l 空パターンは空だけ");
    check(wildcard_match("t*t*t", "target_t") == 1, "C1m 複数の * が戻れる");
}

static void case_inherited_9(void)
{
    static char line[8192];
    static char out[4096];
    char joined[PATH_MAX_LEN];
    char longname[PATH_MAX_LEN];
    DirEntry_Ext e;
    int i, n;

    report("12'''' I-1〜I-6\n");

    /* I-1: shell 側 IdeInfo はカーネル定義 (96B) と同じ大きさ */
    check(sizeof(IdeInfo) == 96, "I1 IdeInfo は 96B (phys_sector_size を含む)");

    /* I-2: 展開しきれない行は負。ENV_VALUE_MAX (256) いっぱいの値を
     * いくつも並べて、受け皿 (ここでは 512B) に収まらない形を作る。 */
    env_init();
    for (i = 0; i < 200; i++) line[i] = 'P';
    line[200] = '\0';
    env_set("PAD", line);

    n = 0;
    { const char *c = "echo "; while (*c) line[n++] = *c++; }
    for (i = 0; i < 8; i++) { const char *c = "${PAD}"; while (*c) line[n++] = *c++; }
    line[n] = '\0';
    check(env_expand(line, out, 512) < 0,
                                       "I2 展開が受け皿に収まらなければ負");

    /* 素の行が受け皿より長い場合も負 */
    for (i = 0; i < 600; i++) line[i] = 'q';
    line[600] = '\0';
    check(env_expand(line, out, 512) < 0,
                                       "I2' 展開なしでも長すぎれば負");
    check(env_expand("echo hi", out, 512) >= 0 && strcmp(out, "echo hi") == 0,
                                       "I2'' 収まる行は非負で中身も同じ");

    /* I-3: fs_join_path は溢れたら負 */
    for (i = 0; i < PATH_MAX_LEN - 2; i++) longname[i] = 'x';
    longname[PATH_MAX_LEN - 2] = '\0';
    check(fs_join_path(joined, "/d", longname) < 0,
                                       "I3 収まらない結合は負を返す");
    check(fs_join_path(joined, "/d", "abc") == 0 &&
          strcmp(joined, "/d/abc") == 0,
                                       "I3' 収まる結合は 0 と正しい綴り");

    /* I-4: 収集表は 32 文字以上の名前も写す */
    g_copy_count = 0;
    g_copy_over = 0;
    for (i = 0; i < 40; i++) e.name[i] = 'n';
    e.name[40] = '\0';
    e.size = 0;
    e.type = OS32_FILE_TYPE_FILE;
    collect_entries_cb(&e, (void *)0);
    check(g_copy_count == 1 && strlen(g_copy_entries[0].name) == 40,
                                       "I4 40 文字の名前が切れずに写る");
    g_copy_count = 0;

    /* I-5: dd のセクタ長はデバイス種別ごと */
    {
        char *argv[6];
        argv[0] = (char *)"dd";
        argv[2] = (char *)"lba=0";
        argv[3] = (char *)"count=1";
        argv[4] = (char *)0;

        argv[1] = (char *)"hd0";
        files_reset(); out_reset();
        g_blk_sector = SYS_HDD_SECTOR_SIZE; g_last_alloc = 0;
        cmd_dd(4, argv);
        check(g_last_alloc == SYS_HDD_SECTOR_SIZE, "I5 hd0 は 512B");

        argv[1] = (char *)"fd0";
        files_reset(); out_reset();
        g_blk_sector = SYS_BLOCK_SECTOR_SIZE; g_last_alloc = 0;
        cmd_dd(4, argv);
        check(g_last_alloc == SYS_BLOCK_SECTOR_SIZE, "I5' fd0 は 1024B");

        argv[1] = (char *)"cd0";
        files_reset(); out_reset();
        g_blk_sector = SYS_CDROM_SECTOR_SIZE; g_last_alloc = 0;
        cmd_dd(4, argv);
        check(g_last_alloc == SYS_CDROM_SECTOR_SIZE, "I5'' cd0 は 2048B");

        /* I-6: 書き込み失敗は中止して実書き込み量を出す */
        argv[1] = (char *)"hd0";
        argv[4] = (char *)"file=/dd.out";
        argv[5] = (char *)0;
        files_reset(); out_reset();
        g_blk_sector = SYS_HDD_SECTOR_SIZE;
        g_write_fail = 1;
        cmd_dd(5, argv);
        g_write_fail = 0;
        check(out_is("dd: write failed at sector # (wrote # bytes)\\n"),
                                       "I6 write 失敗で中止して報せる");
    }
}

/* ========================================================================
 *  13. コピーの失敗は負を返す (往復 7 の R6、常駐にも効く)
 * ======================================================================== */
static void case_copy_failure(void)
{
    int rc;

    report("13 read / write が失敗したら do_copy_file は負を返す\n");
    files_reset();
    file_add("/src.txt", "hello");
    out_reset();

    g_write_fail = 1;
    rc = do_copy_file("mv", "/src.txt", "/dst.txt");
    check(rc < 0,                     "13a write 失敗で負 (mv は原本を消さない)");
    g_write_fail = 0;

    g_read_fail = 1;
    rc = do_copy_file("cp", "/src.txt", "/dst.txt");
    check(rc < 0,                     "13b read 失敗でも負");
    g_read_fail = 0;

    rc = do_copy_file("cp", "/src.txt", "/dst.txt");
    check(rc == 0,                    "13c 成功なら 0");
}

/* ========================================================================
 *  14. source 中の exit (往復 1 の blocker 2 / D2(d))
 * ======================================================================== */
static void case_exit_stops_rest(void)
{
    report("14 source: exit の次の行は走らない\n");
    sh_exit_flag = 0;
    files_reset();
    trace_reset();
    out_reset();
    file_add("/a.sh", "echo 1\nexit\necho 2\n");

    check(script_source_file("/a.sh") == 0, "14a source は 0 で戻る");
    check(trace_is("echo 1|exit"),          "14b exit の後は実行しない");
    check(sh_exit_flag == 1,                "14c 印は立ったまま (shell_run の入口が見る)");
    check(g_open_leak == 0,                 "14d FD を開いたままにしない");
}

static void case_exit_breaks_goto_loop(void)
{
    report("15 source: goto の無限ループでも exit で抜ける\n");
    sh_exit_flag = 0;
    files_reset();
    trace_reset();
    out_reset();
    /* exit が無ければ :loop <- goto loop で永久に回る */
    file_add("/b.sh", "echo a\nexit\n:loop\ngoto loop\n");

    check(script_source_file("/b.sh") == 0, "15a source は戻ってくる");
    check(trace_is("echo a|exit"),          "15b ラベルも goto も走らない");
}

static void case_exit_unwinds_nested(void)
{
    report("16 source: ネストした source の外側も抜ける\n");
    sh_exit_flag = 0;
    files_reset();
    trace_reset();
    out_reset();
    file_add("/outer.sh", "source /inner.sh\necho outer2\n");
    file_add("/inner.sh", "exit\necho inner2\n");

    check(script_source_file("/outer.sh") == 0, "16a 外側の source も 0 で戻る");
    check(trace_is("source /inner.sh|exit"),    "16b 内側も外側も後続を止める");
    check(g_open_leak == 0,                     "16c どの段でも FD を残さない");
}

/* ---- entry ------------------------------------------------------------- */

void _start(void)
{
    build_api();
    case_redraw_extend();
    case_redraw_rebuild();
    case_redraw_cursor_not_at_end();
    case_backspace_erases();
    case_pipe_buffers();
    case_ls_callback_no_kapi();
    case_pipe_blocks_launch();
    case_ask_backspace();
    case_redirect_blocks_launch();
    case_ls_long_name();
    case_glob_matches_only();
    case_argv_bound();
    case_path_normalize();
    case_sector_and_wildcard();
    case_star_precedence();
    case_inherited_9();
    case_copy_failure();
    case_exit_stops_rest();
    case_exit_breaks_goto_loop();
    case_exit_unwinds_nested();
    report(failures ? "SOME FAIL\n" : "ALL PASS\n");
    die(failures ? 1 : 0);
}

/* Caller tests mock enumeration; real packet iteration is check-ls-client-host. */
int os32_ls(const char *path, DirCallback cb, void *ctx)
{
    return (g_api)->sys_ls(path, (void *)cb, ctx);
}
