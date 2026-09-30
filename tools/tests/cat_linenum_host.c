/* =========================================================================
 *  CAT_LINENUM_HOST.C — `cat -n` の行番号が「行の先頭」でだけ出るか
 *
 *  実行: python3 -B tools/tests/test_cat_linenum.py [--target] [--mutate]
 *  記録: tools/tests/cat_linenum_tdd.md
 *
 *  欠陥は 2 つあった (どちらも userland/shell/cmd_file.c の cat_with_linenum)。
 *
 *   (a) ループが `for (i = 0; i <= len; i++)` で回り、i == len でも必ず
 *       行番号を出していた。改行で終わるファイルでは最後の改行の後ろに
 *       **中身のない行がもう 1 行**出る (`printf 'a\nb\n' | cat -n` が 3 行)。
 *   (b) cmd_cat は sys_read 1 回ごとに cat_with_linenum を呼ぶ。行頭かどうかの
 *       状態を読み取りをまたいで持たないので、**ファイルが IO_BUF_SIZE を
 *       超えると切れ目ごとに (a) と同じことが起きる**。行の途中で切れても
 *       そこで 1 行終わったものとして扱われ、番号が 1 つ余分に増える。
 *
 *  正しい挙動 (POSIX の cat -n):
 *    - 行番号は行の先頭で出す。改行を見たら次の行の先頭で出す。
 *    - 最後が改行で終わるならそこで終わり。余分な行番号を出さない。
 *    - 最後が改行で終わらないなら、その行にも行番号を出して中身を出す。
 *    - 空ファイルは 1 行も出さない。
 *    - 書式は 6 桁右寄せ + 空白 2 つ (変えない)。
 *
 *  実物の userland/shell/cmd_fs_shared.c と cmd_file.c を 1 行も写さずに
 *  そのまま #include し、KernelAPI だけを贋物にする。判定は
 *  **sys_write(1, ...) に出た全バイト**を、この試験が別に書いた素朴な
 *  参照実装 (ref_cat_n) と 1 バイトずつ突き合わせる。参照実装は sprintf の
 *  "%6d  " を使うので、書式 (6 桁右寄せ + 空白 2) もここで固定される。
 *
 *  (b) は 2 通りで踏む:
 *    - catf_chunk … sys_read が要求より短く返す (実 FS と同じ) 小さい刻み
 *    - IO_BUF_SIZE ちょうど / その境界に行や改行を置いた大きいファイル
 *
 *  6 章は別の欠陥 (継承バグ台帳): **内蔵 `cat` が標準入力を読まない**。
 *  cmd_cat はファイル名の引数だけをループしていたので `echo a | cat` も
 *  `cat < file` も空だった。直しは「引数が 1 つも無ければ FD 0 を読む」で、
 *  ここでは次の 4 つを見る。
 *    - 引数なしの cat が FD 0 の中身をそのまま出す
 *    - 引数なしの cat -n が (5 章までと同じ規則で) 行番号を付ける
 *    - **FD 0 を sys_close しない** (シェルの FD を閉じない)
 *    - 引数があるときは FD 0 を読まない (誤発火の裏)
 *  端末 (sys_isatty(0) == 1) のままのときは、vfs_read_fd の TTY 経路に
 *  EOF が無いので読みに行かない — grep / hexdump と同じ断り方。
 *
 *  エミュレータ・実配備・make には一切触れない。
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "os32api.h"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    printf("  %s %s\n", cond ? "ok  " : "FAIL", name);
    if (!cond) failures++;
}

/* ------------------------------------------------------------------------ */
/*  贋ファイルシステム — ファイルは 1 本だけ、中身はヒープ                     */
/* ------------------------------------------------------------------------ */

#define CATF_PATH   "/f"
#define CATF_OUTCAP (4 * 1024 * 1024)

static u8  *catf_data;
static int  catf_size;
static int  catf_pos;
static int  catf_open;
static int  catf_chunk;     /* > 0 … sys_read はこのバイト数までしか返さない */

static u8  *catf_out;       /* sys_write(1, ...) に出たバイト列 */
static int  catf_outlen;
static int  catf_write_calls;

/* 標準入力 (FD 0) の観測。中身は同じ catf_data を流す。
 *   catf_tty0        … sys_isatty(0) がこれを返す (1 = 端末)
 *   catf_read0_calls … sys_read(0, ...) が呼ばれた回数
 *   catf_close0_calls… sys_close(0) が呼ばれた回数 (**0 でなければならない**) */
static int  catf_tty0;
static int  catf_read0_calls;
static int  catf_close0_calls;
static int  catf_close_calls;

static char catf_log[4096];
static u32  catf_log_len;

static void catf_set(const u8 *data, int len, int chunk)
{
    catf_data = (u8 *)realloc(catf_data, (size_t)(len + 1));
    if (!catf_data) { printf("  (harness) out of memory\n"); exit(2); }
    if (len > 0) memcpy(catf_data, data, (size_t)len);
    catf_data[len] = 0;
    catf_size = len;
    catf_pos = 0;
    catf_open = 0;
    catf_chunk = chunk;
    catf_outlen = 0;
    catf_write_calls = 0;
    catf_tty0 = 0;
    catf_read0_calls = 0;
    catf_close0_calls = 0;
    catf_close_calls = 0;
    catf_log_len = 0;
    catf_log[0] = '\0';
}

static void *fk_mem_alloc(u32 n) { return malloc(n); }
static void  fk_mem_free(void *p) { free(p); }

static void fk_kprintf(u8 attr, const char *fmt, ...)
{
    va_list ap;
    char line[1024];
    int n;
    (void)attr;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((u32)n >= sizeof(line)) n = (int)sizeof(line) - 1;
    if (catf_log_len + (u32)n + 1 < sizeof(catf_log)) {
        memcpy(catf_log + catf_log_len, line, (size_t)n);
        catf_log_len += (u32)n;
        catf_log[catf_log_len] = '\0';
    }
}

static int fk_sys_open(const char *path, int mode)
{
    (void)mode;
    if (strcmp(path, CATF_PATH) != 0) return OS32_ERR_NOTFOUND;
    catf_open = 1;
    catf_pos = 0;
    return 3;
}

static void fk_sys_close(int fd)
{
    catf_close_calls++;
    if (fd == 0) catf_close0_calls++;
    if (fd == 3) catf_open = 0;
}

static int fk_sys_read(int fd, void *buf, u32 size)
{
    int avail;
    /* FD 0 は同じ中身を流す (パイプ / リダイレクト先の代わり)。
     * シェルの fd_redirect が差し替えたところをそのまま読む形。 */
    if (fd == 0) catf_read0_calls++;
    else if (fd != 3 || !catf_open) return OS32_ERR_IO;
    avail = catf_size - catf_pos;
    if (avail <= 0) return 0;
    if ((u32)avail > size) avail = (int)size;
    if (catf_chunk > 0 && avail > catf_chunk) avail = catf_chunk;
    memcpy(buf, catf_data + catf_pos, (size_t)avail);
    catf_pos += avail;
    return avail;
}

static int fk_sys_write(int fd, const void *buf, u32 size)
{
    if (fd != 1) return OS32_ERR_IO;
    catf_write_calls++;
    if (catf_outlen + (int)size > CATF_OUTCAP) {
        printf("  (harness) output buffer overflow\n");
        exit(2);
    }
    memcpy(catf_out + catf_outlen, buf, (size_t)size);
    catf_outlen += (int)size;
    return (int)size;
}

static int fk_sys_stat(const char *path, OS32_Stat *buf)
{
    memset(buf, 0, sizeof(OS32_Stat));
    if (strcmp(path, CATF_PATH) != 0) return OS32_ERR_NOTFOUND;
    buf->st_dev = 1;
    buf->st_ino = 100;
    buf->st_nlink = 1;
    buf->st_mode = (u16)(OS_S_IFREG | 0644);
    buf->st_size = (u32)catf_size;
    return 0;
}

static int fk_sys_ls(const char *path, void *cb, void *ctx)
{
    (void)path; (void)cb; (void)ctx;
    return OS32_ERR_NOTDIR;
}

static const char *fk_sys_getcwd(void) { return "/"; }
static int fk_sys_isatty(int fd) { return fd == 0 ? catf_tty0 : 0; }

static KernelAPI g_fake;
KernelAPI *g_api = &g_fake;

static void fake_api_init(void)
{
    memset(&g_fake, 0, sizeof(g_fake));
    g_fake.mem_alloc = fk_mem_alloc;
    g_fake.mem_free = fk_mem_free;
    g_fake.kprintf = fk_kprintf;
    g_fake.sys_open = fk_sys_open;
    g_fake.sys_close = fk_sys_close;
    g_fake.sys_read = fk_sys_read;
    g_fake.sys_write = fk_sys_write;
    g_fake.sys_stat = fk_sys_stat;
    g_fake.sys_ls = fk_sys_ls;
    g_fake.sys_getcwd = fk_sys_getcwd;
    g_fake.sys_isatty = fk_sys_isatty;
}

#include "../../userland/shell/cmd_fs_shared.c"
#include "../../userland/shell/cmd_file.c"

void shell_print_help(const char *cmd) { (void)cmd; }
void shell_register_cmds(const ShellCmd *cmds) { (void)cmds; }

/* ------------------------------------------------------------------------ */
/*  参照実装 — cmd_file.c を一切見ない素朴な cat -n                           */
/* ------------------------------------------------------------------------ */

static u8 *ref_buf;

/* 行の区切りは改行。最後の行が改行で終わらなくても番号を出し、出力側の行末には
 * 改行を足す (cmd_cat の今の挙動に合わせる — TDD 記録 §4 参照)。 */
static int ref_cat_n(const u8 *in, int len, u8 *out)
{
    int i = 0;
    int line = 1;
    int o = 0;

    while (i < len) {
        int start = i;
        char num[16];
        int nl;
        while (i < len && in[i] != '\n') i++;
        nl = sprintf(num, "%6d  ", line);
        memcpy(out + o, num, (size_t)nl);
        o += nl;
        if (i > start) {
            memcpy(out + o, in + start, (size_t)(i - start));
            o += i - start;
        }
        out[o++] = '\n';
        if (i < len) i++;
        line++;
    }
    return o;
}

/* ------------------------------------------------------------------------ */

static void run_cat(int with_n)
{
    char *av[4];
    int n = 0;
    av[n++] = (char *)"cat";
    if (with_n) av[n++] = (char *)"-n";
    av[n++] = (char *)CATF_PATH;
    av[n] = 0;
    cmd_cat(n, av);
}

/* ファイル名を渡さない cat (= 標準入力) */
static void run_cat_stdin(int with_n)
{
    char *av[3];
    int n = 0;
    av[n++] = (char *)"cat";
    if (with_n) av[n++] = (char *)"-n";
    av[n] = 0;
    cmd_cat(n, av);
}

static int out_matches(const u8 *want, int wantlen)
{
    return catf_outlen == wantlen &&
           (wantlen == 0 || memcmp(catf_out, want, (size_t)wantlen) == 0);
}

/* 行番号が何個出たかを数える (期待と食い違ったときの説明用) */
static int count_numbers(const u8 *buf, int len)
{
    int i, n = 0;
    for (i = 0; i + 8 <= len; i++) {
        int j, digits = 0, spaces = 0;
        if (i != 0 && buf[i - 1] != '\n') continue;
        for (j = 0; j < 6; j++) {
            if (buf[i + j] == ' ' && digits == 0) spaces++;
            else if (buf[i + j] >= '0' && buf[i + j] <= '9') digits++;
            else break;
        }
        if (j == 6 && digits > 0 && spaces + digits == 6 &&
            buf[i + 6] == ' ' && buf[i + 7] == ' ') n++;
    }
    return n;
}

static void show_diff(const u8 *want, int wantlen)
{
    int i;
    printf("       出た %d バイト / 期待 %d バイト\n", catf_outlen, wantlen);
    for (i = 0; i < catf_outlen && i < wantlen; i++)
        if (catf_out[i] != want[i]) break;
    printf("       最初のずれ = %d バイト目\n", i);
    printf("       行番号の個数 出た=%d 期待=%d\n",
           count_numbers(catf_out, catf_outlen),
           count_numbers(want, wantlen));
    if (catf_outlen <= 120 && wantlen <= 120) {
        printf("       出た   [");
        for (i = 0; i < catf_outlen; i++)
            putchar(catf_out[i] == '\n' ? '|' : catf_out[i]);
        printf("]\n       期待   [");
        for (i = 0; i < wantlen; i++)
            putchar(want[i] == '\n' ? '|' : want[i]);
        printf("]\n");
    }
}

static void case_n(const char *name, const u8 *data, int len, int chunk)
{
    int wantlen;
    int ok;
    catf_set(data, len, chunk);
    wantlen = ref_cat_n(data, len, ref_buf);
    run_cat(1);
    ok = out_matches(ref_buf, wantlen);
    check(ok, name);
    if (!ok) show_diff(ref_buf, wantlen);
}

/* 引数なし (標準入力) の cat -n を参照実装と突き合わせる */
static void case_n_stdin(const char *name, const u8 *data, int len, int chunk)
{
    int wantlen;
    int ok;
    catf_set(data, len, chunk);
    wantlen = ref_cat_n(data, len, ref_buf);
    run_cat_stdin(1);
    ok = out_matches(ref_buf, wantlen);
    check(ok, name);
    if (!ok) show_diff(ref_buf, wantlen);
}

/* len バイト、period バイトごとに改行 (period <= 0 なら改行なし) */
static u8 *mk_fill(int len, int period)
{
    u8 *p = (u8 *)malloc((size_t)len + 1);
    int i;
    if (!p) { printf("  (harness) out of memory\n"); exit(2); }
    for (i = 0; i < len; i++)
        p[i] = (u8)((period > 0 && (i + 1) % period == 0)
                    ? '\n' : ('a' + (i % 26)));
    p[len] = 0;
    return p;
}

int main(void)
{
    static const u8 s_empty[1] = { 0 };
    int i;
    int bad_before;

    fake_api_init();
    catf_out = (u8 *)malloc(CATF_OUTCAP);
    ref_buf = (u8 *)malloc(CATF_OUTCAP);
    if (!catf_out || !ref_buf) { printf("  (harness) out of memory\n"); return 2; }

    printf("=== cat -n の行番号は行の先頭でだけ出る ===\n");

    /* ---- 1. 小さいファイル (欠陥 a) ------------------------------------ */
    printf("== 1. 小さいファイル — 末尾の改行で行番号を増やさない ==\n");
    case_n("空ファイル: 1 バイトも出さない", s_empty, 0, 0);
    check(catf_outlen == 0 && catf_write_calls == 0,
          "空ファイル: sys_write を 1 回も呼ばない");

    case_n("'a\\nb\\n': 行番号は 2 つ (3 つ目を出さない)",
           (const u8 *)"a\nb\n", 4, 0);
    catf_set((const u8 *)"a\nb\n", 4, 0);
    run_cat(1);
    check(count_numbers(catf_out, catf_outlen) == 2,
          "'a\\nb\\n': 数えても行番号は 2 つ");
    check(out_matches((const u8 *)"     1  a\n     2  b\n", 20),
          "'a\\nb\\n': 書式は 6 桁右寄せ + 空白 2 つのまま");

    case_n("'a\\nb': 改行で終わらない最後の行にも行番号",
           (const u8 *)"a\nb", 3, 0);
    case_n("'a\\n': 1 行だけ", (const u8 *)"a\n", 2, 0);
    case_n("'a': 1 行だけ・改行なし", (const u8 *)"a", 1, 0);
    case_n("'\\n': 空行 1 つ", (const u8 *)"\n", 1, 0);
    case_n("'\\n\\n\\n': 空行が続く", (const u8 *)"\n\n\n", 3, 0);
    case_n("'\\n\\na': 空行の後に改行なしの行", (const u8 *)"\n\na", 3, 0);

    /* ---- 2. 読み取りが短く返る (欠陥 b の芯) --------------------------- */
    printf("== 2. sys_read が要求より短く返す — 行頭の状態を読み取りをまたいで持つ ==\n");
    {
        static const char *bodies[] = { "one\ntwo\nthree\n", "one\ntwo\nthree",
                                        "\n\n\n\n", "abc", "a\n\nb\n" };
        static const int chunks[] = { 1, 2, 3, 4, 5, 7 };
        unsigned int b, c;
        char name[128];
        for (b = 0; b < sizeof(bodies) / sizeof(bodies[0]); b++) {
            for (c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
                sprintf(name, "読み取り %d バイト刻み: 本文 %u 番",
                        chunks[c], b + 1);
                case_n(name, (const u8 *)bodies[b], (int)strlen(bodies[b]),
                       chunks[c]);
            }
        }
    }

    /* ---- 3. IO_BUF_SIZE の境界 (欠陥 b の本体) ------------------------- */
    printf("== 3. IO_BUF_SIZE (%d) の境界 ==\n", IO_BUF_SIZE);
    bad_before = failures;
    {
        u8 *p;

        /* (3-1) ちょうど IO_BUF_SIZE で、最後のバイトが改行 */
        p = mk_fill(IO_BUF_SIZE, 64);
        case_n("ちょうど IO_BUF_SIZE・末尾が改行", p, IO_BUF_SIZE, 0);
        free(p);

        /* (3-2) ちょうど IO_BUF_SIZE で、改行が 1 つも無い */
        p = mk_fill(IO_BUF_SIZE, 0);
        case_n("ちょうど IO_BUF_SIZE・改行が 1 つも無い", p, IO_BUF_SIZE, 0);
        free(p);

        /* (3-3) 境界で行が切れる: 改行は境界をまたいだ先にしか無い */
        p = mk_fill(IO_BUF_SIZE + 100, 0);
        p[IO_BUF_SIZE + 50] = '\n';
        p[IO_BUF_SIZE + 99] = '\n';
        case_n("境界で行の途中が切れる (改行は境界の先)",
               p, IO_BUF_SIZE + 100, 0);
        free(p);

        /* (3-4) 境界が改行と重なる: 1 回目の最後の 1 バイトがちょうど改行 */
        p = mk_fill(2 * IO_BUF_SIZE, 0);
        p[IO_BUF_SIZE - 1] = '\n';
        p[2 * IO_BUF_SIZE - 1] = '\n';
        case_n("境界が改行と重なる (buf 末尾が改行)", p, 2 * IO_BUF_SIZE, 0);
        free(p);

        /* (3-5) 境界の直後が改行: 2 回目の読み取りの先頭が改行 */
        p = mk_fill(2 * IO_BUF_SIZE, 0);
        p[IO_BUF_SIZE] = '\n';
        p[2 * IO_BUF_SIZE - 1] = '\n';
        case_n("境界の直後が改行 (2 回目の先頭が改行)", p, 2 * IO_BUF_SIZE, 0);
        free(p);

        /* (3-6) 3 回読む長さ・どの区切りも行の途中 */
        p = mk_fill(3 * IO_BUF_SIZE - 7, 1000);
        case_n("3 回読む長さ・1000 バイトごとに改行",
               p, 3 * IO_BUF_SIZE - 7, 0);
        free(p);

        /* (3-7) 大きいファイルで行番号が桁上がりしても書式が崩れない */
        p = mk_fill(IO_BUF_SIZE + 1000, 1);
        case_n("改行だけ IO_BUF_SIZE+1000 行 (行番号が 5 桁になる)",
               p, IO_BUF_SIZE + 1000, 0);
        free(p);
    }
    printf("   (境界の 7 件中 %d 件が FAIL)\n", failures - bad_before);

    /* ---- 4. -n 無しは 1 バイトも変えない (退行の裏) -------------------- */
    printf("== 4. -n 無しの cat は素通し ==\n");
    {
        u8 *p = mk_fill(2 * IO_BUF_SIZE + 33, 97);
        for (i = 0; i < 16; i++) p[i * 7] = (u8)i;   /* NUL を含む生バイト */
        catf_set(p, 2 * IO_BUF_SIZE + 33, 0);
        run_cat(0);
        check(catf_outlen == 2 * IO_BUF_SIZE + 33 &&
              memcmp(catf_out, p, (size_t)(2 * IO_BUF_SIZE + 33)) == 0,
              "-n 無し: 2*IO_BUF_SIZE+33 バイトを 1 バイトも変えずに出す");
        catf_set(p, 2 * IO_BUF_SIZE + 33, 3);
        run_cat(0);
        check(catf_outlen == 2 * IO_BUF_SIZE + 33 &&
              memcmp(catf_out, p, (size_t)(2 * IO_BUF_SIZE + 33)) == 0,
              "-n 無し: 3 バイト刻みの読み取りでも素通し");
        free(p);

        catf_set(s_empty, 0, 0);
        run_cat(0);
        check(catf_outlen == 0, "-n 無し: 空ファイルは何も出さない");
    }

    /* ---- 5. 参照実装そのものの目 -------------------------------------- */
    printf("== 5. 参照実装の自己点検 ==\n");
    check(ref_cat_n((const u8 *)"a\nb\n", 4, ref_buf) == 20,
          "参照実装: 'a\\nb\\n' は 20 バイト (2 行)");
    check(ref_cat_n((const u8 *)"", 0, ref_buf) == 0,
          "参照実装: 空は 0 バイト");

    /* ---- 6. 引数なしの cat は標準入力 (FD 0) を読む -------------------- */
    printf("== 6. 引数なしの cat は FD 0 を読む ==\n");

    /* (6-1) 素通し */
    {
        static const char *bodies[] = { "one\ntwo\nthree\n", "one\ntwo\nthree",
                                        "\n", "abc", "" };
        static const int chunks[] = { 0, 1, 3, 7 };
        unsigned int b, c;
        char name[128];
        for (b = 0; b < sizeof(bodies) / sizeof(bodies[0]); b++) {
            for (c = 0; c < sizeof(chunks) / sizeof(chunks[0]); c++) {
                int len = (int)strlen(bodies[b]);
                catf_set((const u8 *)bodies[b], len, chunks[c]);
                run_cat_stdin(0);
                sprintf(name, "引数なし: 本文 %u 番を %d バイト刻みで素通し",
                        b + 1, chunks[c]);
                check(catf_outlen == len &&
                      (len == 0 || memcmp(catf_out, bodies[b], (size_t)len) == 0),
                      name);
            }
        }
    }

    /* (6-2) -n の行番号は 5 章までと同じ規則 */
    case_n_stdin("引数なし -n: 'a\\nb\\n' は行番号 2 つ",
                 (const u8 *)"a\nb\n", 4, 0);
    case_n_stdin("引数なし -n: 'a\\nb' は最後の行にも番号",
                 (const u8 *)"a\nb", 3, 0);
    case_n_stdin("引数なし -n: 空入力は 1 バイトも出さない",
                 (const u8 *)"", 0, 0);
    case_n_stdin("引数なし -n: 3 バイト刻みの読み取り",
                 (const u8 *)"one\ntwo\nthree\n", 14, 3);
    catf_set((const u8 *)"a\nb\n", 4, 0);
    run_cat_stdin(1);
    check(count_numbers(catf_out, catf_outlen) == 2,
          "引数なし -n: 末尾の余分な行番号を出さない");
    check(out_matches((const u8 *)"     1  a\n     2  b\n", 20),
          "引数なし -n: 書式は 6 桁右寄せ + 空白 2 つのまま");
    {
        u8 *p = mk_fill(2 * IO_BUF_SIZE + 123, 0);   /* 改行なしで境界をまたぐ */
        p[IO_BUF_SIZE] = '\n';
        p[2 * IO_BUF_SIZE + 122] = '\n';
        case_n_stdin("引数なし -n: IO_BUF_SIZE の切れ目をまたぐ",
                     p, 2 * IO_BUF_SIZE + 123, 0);
        free(p);
    }

    /* (6-3) FD 0 を閉じない */
    catf_set((const u8 *)"a\nb\n", 4, 0);
    run_cat_stdin(0);
    check(catf_close0_calls == 0, "引数なし: **sys_close(0) を呼ばない**");
    catf_set((const u8 *)"a\nb\n", 4, 3);
    run_cat_stdin(1);
    check(catf_close0_calls == 0, "引数なし -n: sys_close(0) を呼ばない");
    catf_set((const u8 *)"", 0, 0);
    run_cat_stdin(0);
    check(catf_close0_calls == 0, "引数なし・空入力: sys_close(0) を呼ばない");

    /* (6-4) 引数があるときは FD 0 を読まない (誤発火の裏) */
    catf_set((const u8 *)"a\nb\n", 4, 0);
    run_cat(0);
    check(catf_read0_calls == 0, "ファイル指定: FD 0 を読まない");
    check(catf_close_calls == 1 && catf_close0_calls == 0,
          "ファイル指定: 閉じるのは開いた FD だけ");
    catf_set((const u8 *)"a\nb\n", 4, 0);
    run_cat(1);
    check(catf_read0_calls == 0, "ファイル指定 -n: FD 0 を読まない");
    catf_set((const u8 *)"a\nb\n", 4, 0);
    {
        char *av[3];
        av[0] = (char *)"cat";
        av[1] = (char *)"/nope";
        av[2] = 0;
        cmd_cat(2, av);
    }
    check(catf_read0_calls == 0 && catf_outlen == 0,
          "無いファイルを指定: FD 0 へ落ちない (断って終わる)");

    /* (6-5) 端末のままなら読みに行かない */
    catf_set((const u8 *)"a\nb\n", 4, 0);
    catf_tty0 = 1;
    run_cat_stdin(0);
    check(catf_read0_calls == 0 && catf_outlen == 0,
          "FD 0 が端末: 読みに行かない (戻れなくなる経路へ入らない)");
    catf_set((const u8 *)"a\nb\n", 4, 0);
    catf_tty0 = 1;
    run_cat_stdin(1);
    check(catf_read0_calls == 0 && catf_outlen == 0,
          "FD 0 が端末 -n: 読みに行かない");
    catf_tty0 = 0;

    printf("\n=== %d 件中 %d 件 FAIL ===\n", checks, failures);
    return failures ? 1 : 0;
}
