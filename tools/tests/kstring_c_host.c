/* ========================================================================
 *  kstring_c_host.c — lib/kstring_asm.asm (x86) と lib/kstring_c.c (C 版) を
 *  **同じ実行ファイルにリンクして同じ入力で突き合わせる** 答え合わせの試験。
 *
 *  票:   移植準備 順序 4-b
 *  駆動: tools/tests/test_kstring_c.py
 *
 *  やり方:
 *    * 実物の lib/kstring_asm.asm を nasm -f elf32 で組み、objcopy で
 *      13 本のシンボルを a_* に改名する。
 *    * 実物の lib/kstring_c.c をホスト ILP32 GNU89 でコンパイルし、同じ
 *      13 本を c_* に改名する。
 *    * この翻訳単位が両方を呼び、**戻り値とバッファの全内容 (前後の番兵を
 *      含む)** が 1 バイトも違わないことを見る。
 *
 *  どちらの版も書き込み先は 4 バイト境界から始まる同じ大きさの領域で、
 *  作業域の前後に 0xA5 の番兵を置く。アセンブリ版は 4 バイト単位の
 *  バルク転送をするので、**重なりコピーは壊れ方まで一致していないといけない**
 *  (lib/kstring.h は重なりを保証しないと書いているが、C 版に差し替えた
 *   瞬間に壊れ方が変わると、たまたま動いていた呼び出しが黙って壊れる)。
 *
 *  libc は使わない (-nostdlib、Linux の int 0x80 で write/exit するだけ)。
 *  比較そのものに試験対象を使わないよう、h_* の素朴な実装を別に持つ。
 * ======================================================================== */

#include "types.h"

/* ======================================================================== */
/*  libc の代わり (-nostdlib)                                               */
/* ======================================================================== */

static int g_exit_code;

static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

static u32 h_strlen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void report(const char *text)
{
    u32 len = h_strlen(text);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static void report_i(long v)
{
    char buf[24];
    int i = 23;
    unsigned long u;
    buf[i] = '\0';
    if (v < 0) u = (unsigned long)(-v); else u = (unsigned long)v;
    if (u == 0) { buf[--i] = '0'; }
    while (u > 0) { buf[--i] = (char)('0' + (u % 10)); u /= 10; }
    if (v < 0) buf[--i] = '-';
    report(&buf[i]);
}

static void report_hex(unsigned long u)
{
    static const char digits[] = "0123456789ABCDEF";
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (u == 0) { buf[--i] = '0'; }
    while (u > 0) { buf[--i] = digits[u & 0xFUL]; u >>= 4; }
    report("0x");
    report(&buf[i]);
}

static int g_checks;
static int g_failures;

static void fail_head(const char *what)
{
    g_failures++;
    report("  FAIL ");
    report(what);
}

/* ======================================================================== */
/*  試験対象 — アセンブリ版 (a_*) と C 版 (c_*)                             */
/*  改名は tools/tests/test_kstring_c.py が objcopy --redefine-syms で行う。 */
/* ======================================================================== */

extern void *a_kmemcpy(void *dst, const void *src, u32 n);
extern void *a_memcpy(void *dst, const void *src, u32 n);
extern void *a_kmemset(void *dst, int val, u32 n);
extern void *a_memset(void *dst, int val, u32 n);
extern u32   a_kstrlen(const char *s);
extern u32   a_strlen(const char *s);
extern int   a_kstrcmp(const char *a, const char *b);
extern int   a_strcmp(const char *a, const char *b);
extern int   a_kstrncmp(const char *a, const char *b, u32 n);
extern int   a_strncmp(const char *a, const char *b, u32 n);
extern char *a_kstrcpy(char *dst, const char *src);
extern char *a_kstrncpy(char *dst, const char *src, u32 n);
extern int   a_memcmp(const void *a, const void *b, u32 n);

extern void *c_kmemcpy(void *dst, const void *src, u32 n);
extern void *c_memcpy(void *dst, const void *src, u32 n);
extern void *c_kmemset(void *dst, int val, u32 n);
extern void *c_memset(void *dst, int val, u32 n);
extern u32   c_kstrlen(const char *s);
extern u32   c_strlen(const char *s);
extern int   c_kstrcmp(const char *a, const char *b);
extern int   c_strcmp(const char *a, const char *b);
extern int   c_kstrncmp(const char *a, const char *b, u32 n);
extern int   c_strncmp(const char *a, const char *b, u32 n);
extern char *c_kstrcpy(char *dst, const char *src);
extern char *c_kstrncpy(char *dst, const char *src, u32 n);
extern int   c_memcmp(const void *a, const void *b, u32 n);

typedef struct {
    const char *name;
    void *(*f_kmemcpy)(void *, const void *, u32);
    void *(*f_memcpy)(void *, const void *, u32);
    void *(*f_kmemset)(void *, int, u32);
    void *(*f_memset)(void *, int, u32);
    u32   (*f_kstrlen)(const char *);
    u32   (*f_strlen)(const char *);
    int   (*f_kstrcmp)(const char *, const char *);
    int   (*f_strcmp)(const char *, const char *);
    int   (*f_kstrncmp)(const char *, const char *, u32);
    int   (*f_strncmp)(const char *, const char *, u32);
    char *(*f_kstrcpy)(char *, const char *);
    char *(*f_kstrncpy)(char *, const char *, u32);
    int   (*f_memcmp)(const void *, const void *, u32);
} Impl;

static const Impl IMPL_ASM = {
    "asm",
    a_kmemcpy, a_memcpy, a_kmemset, a_memset,
    a_kstrlen, a_strlen, a_kstrcmp, a_strcmp,
    a_kstrncmp, a_strncmp, a_kstrcpy, a_kstrncpy, a_memcmp
};

static const Impl IMPL_C = {
    "c",
    c_kmemcpy, c_memcpy, c_kmemset, c_memset,
    c_kstrlen, c_strlen, c_kstrcmp, c_strcmp,
    c_kstrncmp, c_strncmp, c_kstrcpy, c_kstrncpy, c_memcmp
};

/* ======================================================================== */
/*  作業域 — 前後に番兵、作業域そのものは 4 バイト境界から始まる            */
/* ======================================================================== */

#define GUARD_BYTES  16
#define WORK_BYTES   192
#define TOTAL_BYTES  (GUARD_BYTES + WORK_BYTES + GUARD_BYTES)
#define GUARD_FILL   0xA5

static u32 rawA[TOTAL_BYTES / 4];
static u32 rawB[TOTAL_BYTES / 4];

#define BYTES_A ((u8 *)rawA)
#define BYTES_B ((u8 *)rawB)
#define WORK_A  (BYTES_A + GUARD_BYTES)
#define WORK_B  (BYTES_B + GUARD_BYTES)

/* 比較に試験対象を使わないための素朴な実装 */
static int h_same(const u8 *x, const u8 *y, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) if (x[i] != y[i]) return 0;
    return 1;
}

static u32 h_first_diff(const u8 *x, const u8 *y, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) if (x[i] != y[i]) return i;
    return n;
}

/* 作業域を決まった模様で埋める。0x80 以上のバイトも 0 も含める。 */
static u8 pattern_byte(u32 i)
{
    return (u8)(((i * 37u) + (i >> 3)) ^ 0x80u);
}

static void fill_both(void)
{
    u32 i;
    for (i = 0; i < TOTAL_BYTES; i++) {
        BYTES_A[i] = GUARD_FILL;
        BYTES_B[i] = GUARD_FILL;
    }
    for (i = 0; i < WORK_BYTES; i++) {
        WORK_A[i] = pattern_byte(i);
        WORK_B[i] = pattern_byte(i);
    }
}

/* 両版の作業域 (番兵込み) が一致し、番兵が無傷であることを見る。 */
static int areas_agree(const char *what, u32 a1, u32 a2, u32 a3)
{
    u32 i;
    g_checks++;
    if (!h_same(BYTES_A, BYTES_B, TOTAL_BYTES)) {
        i = h_first_diff(BYTES_A, BYTES_B, TOTAL_BYTES);
        fail_head(what);
        report(" (");   report_i((long)a1);
        report(",");    report_i((long)a2);
        report(",");    report_i((long)a3);
        report(") バイト ");  report_i((long)i - GUARD_BYTES);
        report(" asm="); report_hex(BYTES_A[i]);
        report(" c=");   report_hex(BYTES_B[i]);
        report("\n");
        return 0;
    }
    for (i = 0; i < GUARD_BYTES; i++) {
        if (BYTES_A[i] != GUARD_FILL ||
            BYTES_A[GUARD_BYTES + WORK_BYTES + i] != GUARD_FILL) {
            fail_head(what);
            report(" 番兵が壊れた (両版とも同じように)\n");
            return 0;
        }
    }
    return 1;
}

static int values_agree(const char *what, long va, long vc,
                        u32 a1, u32 a2, u32 a3)
{
    g_checks++;
    if (va == vc) return 1;
    fail_head(what);
    report(" (");   report_i((long)a1);
    report(",");    report_i((long)a2);
    report(",");    report_i((long)a3);
    report(") asm="); report_i(va);
    report(" c=");    report_i(vc);
    report("\n");
    return 0;
}

/* ======================================================================== */
/*  入力の並び                                                              */
/* ======================================================================== */

/* 0..7 は 4 バイト境界まわり、16/17 は 16 の前後、64/65 は離れた位置。 */
static const u32 OFFS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 15, 16, 17, 64, 65 };
#define N_OFFS (sizeof(OFFS) / sizeof(OFFS[0]))

/* 空・1 バイト・4/8/16 の前後・端数を持つ長さ */
static const u32 LENS[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                            15, 16, 17, 31, 32, 33, 63, 64, 65 };
#define N_LENS (sizeof(LENS) / sizeof(LENS[0]))

/* 比較・コピーの元になる文字列。0x80 以上のバイト = 日本語 UTF-8 を含む。 */
static const char *const STRS[] = {
    "",
    "a",
    "ab",
    "abc",
    "abcd",
    "abcde",
    "abcdefg",
    "abcdefgh",
    "abcdefghi",
    "0123456789abcde",
    "0123456789abcdef",
    "0123456789abcdefg",
    "\x80",
    "\x01",
    "\xff",
    "\x7f",
    "\x80\x80\x80",
    "\x7f\x7f\x7f",
    "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",          /* 日本語 */
    "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt",      /* 日本語.txt */
    "\xe3\x81\x82",                                   /* あ */
    "\xe3\x81\x84",                                   /* い */
    "abc\x80""def",
    "abc\x01""def",
    "abcdefghijklmnopqrstuvwxyz0123456789"
};
#define N_STRS (sizeof(STRS) / sizeof(STRS[0]))

/* ======================================================================== */
/*  1. kmemcpy / memcpy — 重なりを含む                                      */
/* ======================================================================== */

static void case_memcpy(int use_alias)
{
    u32 di, si, li;
    u32 doff, soff, n;
    void *ra;
    void *rc;
    const char *tag = use_alias ? "memcpy" : "kmemcpy";

    for (di = 0; di < N_OFFS; di++) {
        for (si = 0; si < N_OFFS; si++) {
            for (li = 0; li < N_LENS; li++) {
                doff = OFFS[di];
                soff = OFFS[si];
                n    = LENS[li];
                if (doff + n > WORK_BYTES) continue;
                if (soff + n > WORK_BYTES) continue;

                fill_both();
                if (use_alias) {
                    ra = IMPL_ASM.f_memcpy(WORK_A + doff, WORK_A + soff, n);
                    rc = IMPL_C.f_memcpy(WORK_B + doff, WORK_B + soff, n);
                } else {
                    ra = IMPL_ASM.f_kmemcpy(WORK_A + doff, WORK_A + soff, n);
                    rc = IMPL_C.f_kmemcpy(WORK_B + doff, WORK_B + soff, n);
                }
                values_agree(tag, (long)((u8 *)ra - WORK_A),
                             (long)((u8 *)rc - WORK_B), doff, soff, n);
                areas_agree(tag, doff, soff, n);
            }
        }
    }
}

/* ======================================================================== */
/*  2. kmemset / memset                                                     */
/* ======================================================================== */

/* val は下位 8 ビットだけ使われる契約。範囲外・負値も入れる。 */
static const int VALS[] = { 0, 1, 0x41, 0x7F, 0x80, 0xFE, 0xFF,
                            0x100, 0x1FF, 0x12345, -1, -128 };
#define N_VALS (sizeof(VALS) / sizeof(VALS[0]))

static void case_memset(int use_alias)
{
    u32 di, li, vi;
    u32 doff, n;
    void *ra;
    void *rc;
    const char *tag = use_alias ? "memset" : "kmemset";

    for (di = 0; di < N_OFFS; di++) {
        for (li = 0; li < N_LENS; li++) {
            for (vi = 0; vi < N_VALS; vi++) {
                doff = OFFS[di];
                n    = LENS[li];
                if (doff + n > WORK_BYTES) continue;

                fill_both();
                if (use_alias) {
                    ra = IMPL_ASM.f_memset(WORK_A + doff, VALS[vi], n);
                    rc = IMPL_C.f_memset(WORK_B + doff, VALS[vi], n);
                } else {
                    ra = IMPL_ASM.f_kmemset(WORK_A + doff, VALS[vi], n);
                    rc = IMPL_C.f_kmemset(WORK_B + doff, VALS[vi], n);
                }
                values_agree(tag, (long)((u8 *)ra - WORK_A),
                             (long)((u8 *)rc - WORK_B), doff, n,
                             (u32)vi);
                areas_agree(tag, doff, n, (u32)vi);
            }
        }
    }
}

/* ======================================================================== */
/*  3. kstrlen / strlen                                                     */
/* ======================================================================== */

static void case_strlen(void)
{
    u32 si, oi, off, i, len;
    char *pa;
    char *pb;

    for (si = 0; si < N_STRS; si++) {
        for (oi = 0; oi < N_OFFS; oi++) {
            off = OFFS[oi];
            len = h_strlen(STRS[si]);
            if (off + len + 1 > WORK_BYTES) continue;

            fill_both();
            pa = (char *)WORK_A + off;
            pb = (char *)WORK_B + off;
            for (i = 0; i <= len; i++) { pa[i] = STRS[si][i]; pb[i] = STRS[si][i]; }

            values_agree("kstrlen", (long)IMPL_ASM.f_kstrlen(pa),
                         (long)IMPL_C.f_kstrlen(pb), si, off, len);
            values_agree("strlen", (long)IMPL_ASM.f_strlen(pa),
                         (long)IMPL_C.f_strlen(pb), si, off, len);
            areas_agree("kstrlen", si, off, len);
        }
    }
}

/* ======================================================================== */
/*  4. kstrcmp / strcmp — 0x80 以上のバイトの並び順が要                     */
/* ======================================================================== */

static void case_strcmp(void)
{
    u32 ia, ib;

    for (ia = 0; ia < N_STRS; ia++) {
        for (ib = 0; ib < N_STRS; ib++) {
            values_agree("kstrcmp",
                         (long)IMPL_ASM.f_kstrcmp(STRS[ia], STRS[ib]),
                         (long)IMPL_C.f_kstrcmp(STRS[ia], STRS[ib]),
                         ia, ib, 0);
            values_agree("strcmp",
                         (long)IMPL_ASM.f_strcmp(STRS[ia], STRS[ib]),
                         (long)IMPL_C.f_strcmp(STRS[ia], STRS[ib]),
                         ia, ib, 0);
        }
    }
}

/* ======================================================================== */
/*  5. kstrncmp / strncmp — n が長さより長い / 短い / 0                     */
/* ======================================================================== */

static void case_strncmp(void)
{
    u32 ia, ib, li;

    for (ia = 0; ia < N_STRS; ia++) {
        for (ib = 0; ib < N_STRS; ib++) {
            for (li = 0; li < N_LENS; li++) {
                values_agree("kstrncmp",
                             (long)IMPL_ASM.f_kstrncmp(STRS[ia], STRS[ib],
                                                       LENS[li]),
                             (long)IMPL_C.f_kstrncmp(STRS[ia], STRS[ib],
                                                     LENS[li]),
                             ia, ib, LENS[li]);
                values_agree("strncmp",
                             (long)IMPL_ASM.f_strncmp(STRS[ia], STRS[ib],
                                                      LENS[li]),
                             (long)IMPL_C.f_strncmp(STRS[ia], STRS[ib],
                                                    LENS[li]),
                             ia, ib, LENS[li]);
            }
        }
    }
}

/* ======================================================================== */
/*  6. kstrcpy                                                              */
/* ======================================================================== */

static void case_strcpy(void)
{
    u32 si, oi, off, len;
    char *ra;
    char *rc;

    for (si = 0; si < N_STRS; si++) {
        for (oi = 0; oi < N_OFFS; oi++) {
            off = OFFS[oi];
            len = h_strlen(STRS[si]);
            if (off + len + 1 > WORK_BYTES) continue;

            fill_both();
            ra = IMPL_ASM.f_kstrcpy((char *)WORK_A + off, STRS[si]);
            rc = IMPL_C.f_kstrcpy((char *)WORK_B + off, STRS[si]);
            values_agree("kstrcpy", (long)((u8 *)ra - WORK_A),
                         (long)((u8 *)rc - WORK_B), si, off, len);
            areas_agree("kstrcpy", si, off, len);
        }
    }
}

/* ======================================================================== */
/*  7. kstrncpy — 埋め方 (残りを 0 で埋めない) と NUL 終端                  */
/* ======================================================================== */

static void case_strncpy(void)
{
    u32 si, oi, li, off, n;
    char *ra;
    char *rc;

    for (si = 0; si < N_STRS; si++) {
        for (oi = 0; oi < N_OFFS; oi++) {
            for (li = 0; li < N_LENS; li++) {
                off = OFFS[oi];
                n   = LENS[li];
                if (off + n > WORK_BYTES) continue;

                fill_both();
                ra = IMPL_ASM.f_kstrncpy((char *)WORK_A + off, STRS[si], n);
                rc = IMPL_C.f_kstrncpy((char *)WORK_B + off, STRS[si], n);
                values_agree("kstrncpy", (long)((u8 *)ra - WORK_A),
                             (long)((u8 *)rc - WORK_B), si, off, n);
                areas_agree("kstrncpy", si, off, n);
            }
        }
    }
}

/* ======================================================================== */
/*  8. memcmp — DWORD 単位の速い経路を含む                                  */
/* ======================================================================== */

static void case_memcmp(void)
{
    u32 oi, li, di;
    u32 off, n, dpos;
    u8 *pa;
    u8 *pb;
    u8 save_a, save_b;

    for (oi = 0; oi < N_OFFS; oi++) {
        for (li = 0; li < N_LENS; li++) {
            off = OFFS[oi];
            n   = LENS[li];
            /* 2 本並べるので作業域を半分ずつ使う */
            if (off + n > WORK_BYTES / 2) continue;

            /* 8-1. 全部同じ */
            fill_both();
            pa = WORK_A + off;
            pb = WORK_A + WORK_BYTES / 2 + off;
            for (dpos = 0; dpos < n; dpos++) pb[dpos] = pa[dpos];
            for (dpos = 0; dpos < n; dpos++) {
                (WORK_B + off)[dpos] = pa[dpos];
                (WORK_B + WORK_BYTES / 2 + off)[dpos] = pa[dpos];
            }
            values_agree("memcmp eq",
                         (long)IMPL_ASM.f_memcmp(WORK_A + off,
                                                 WORK_A + WORK_BYTES / 2 + off,
                                                 n),
                         (long)IMPL_C.f_memcmp(WORK_B + off,
                                               WORK_B + WORK_BYTES / 2 + off,
                                               n),
                         off, n, 0);

            /* 8-2. 1 バイトだけ違う位置を全部試す (DWORD 内 / 端数) */
            for (di = 0; di < n; di++) {
                save_a = pa[di];
                /* 0x80 以上に化けさせる向きと、その逆の両方 */
                save_b = (u8)(save_a ^ 0x80u);

                (WORK_A + WORK_BYTES / 2 + off)[di] = save_b;
                (WORK_B + WORK_BYTES / 2 + off)[di] = save_b;
                values_agree("memcmp diff-hi",
                             (long)IMPL_ASM.f_memcmp(
                                 WORK_A + off,
                                 WORK_A + WORK_BYTES / 2 + off, n),
                             (long)IMPL_C.f_memcmp(
                                 WORK_B + off,
                                 WORK_B + WORK_BYTES / 2 + off, n),
                             off, n, di);

                (WORK_A + off)[di] = save_b;
                (WORK_B + off)[di] = save_b;
                (WORK_A + WORK_BYTES / 2 + off)[di] = save_a;
                (WORK_B + WORK_BYTES / 2 + off)[di] = save_a;
                values_agree("memcmp diff-lo",
                             (long)IMPL_ASM.f_memcmp(
                                 WORK_A + off,
                                 WORK_A + WORK_BYTES / 2 + off, n),
                             (long)IMPL_C.f_memcmp(
                                 WORK_B + off,
                                 WORK_B + WORK_BYTES / 2 + off, n),
                             off, n, di);

                /* 元に戻す */
                (WORK_A + off)[di] = save_a;
                (WORK_B + off)[di] = save_a;
                (WORK_A + WORK_BYTES / 2 + off)[di] = save_a;
                (WORK_B + WORK_BYTES / 2 + off)[di] = save_a;
            }
            areas_agree("memcmp", off, n, 0);
        }
    }
}

/* ======================================================================== */
/*  9. kernel/kselftest.c の test_str と同じ既存ケース                      */
/*     (両版それぞれが **同じ表明**を満たすことを直接見る)                  */
/* ======================================================================== */

static void selftest_cases(const Impl *im)
{
    static char dst[16];
    int ok;

    g_checks++;
    if (im->f_kstrlen("") != 0) { fail_head(im->name); report(" kstrlen empty\n"); }
    g_checks++;
    if (im->f_kstrlen("abcd") != 4) { fail_head(im->name); report(" kstrlen 4\n"); }

    g_checks++;
    if (im->f_kstrcmp("", "") != 0) { fail_head(im->name); report(" kstrcmp empty\n"); }
    g_checks++;
    if (!(im->f_kstrcmp("abc", "abd") < 0)) { fail_head(im->name); report(" kstrcmp lt\n"); }
    g_checks++;
    if (!(im->f_kstrcmp("abd", "abc") > 0)) { fail_head(im->name); report(" kstrcmp gt\n"); }
    /* 0x80 以上のバイトを含む比較。符号付きで比べていると大小が逆転する */
    g_checks++;
    if (!(im->f_kstrcmp("\x80", "\x01") > 0)) {
        fail_head(im->name); report(" kstrcmp high byte unsigned\n");
    }

    g_checks++;
    if (im->f_kstrncmp("abc", "abd", 0) != 0) { fail_head(im->name); report(" kstrncmp n=0\n"); }
    g_checks++;
    if (im->f_kstrncmp("abc", "abd", 2) != 0) { fail_head(im->name); report(" kstrncmp n=2\n"); }
    g_checks++;
    if (im->f_kstrncmp("abc", "abd", 3) == 0) { fail_head(im->name); report(" kstrncmp n=3\n"); }

    /* kstrncpy は strlcpy セマンティクス: n はバッファ全体サイズ、
     * 必ず NUL 終端し、**残りは 0 で埋めない** */
    im->f_kmemset(dst, 0x7F, sizeof(dst));
    im->f_kstrncpy(dst, "abcdefgh", 4);
    ok = (dst[0] == 'a' && dst[2] == 'c' && dst[3] == '\0');
    g_checks++;
    if (!ok) { fail_head(im->name); report(" kstrncpy truncates + NUL\n"); }
    g_checks++;
    if (dst[4] != 0x7F) {
        fail_head(im->name); report(" kstrncpy は残りを埋めないはず\n");
    }
}

/* ======================================================================== */
/*  10. 別名は **同一の実体** (同じ番地) であること                         */
/* ======================================================================== */

static void case_alias_identity(void)
{
    g_checks++;
    if ((void *)a_kmemcpy != (void *)a_memcpy ||
        (void *)a_kmemset != (void *)a_memset ||
        (void *)a_kstrlen != (void *)a_strlen ||
        (void *)a_kstrcmp != (void *)a_strcmp ||
        (void *)a_kstrncmp != (void *)a_strncmp) {
        fail_head("asm の別名が同一実体でない\n");
    }
    g_checks++;
    if ((void *)c_kmemcpy != (void *)c_memcpy ||
        (void *)c_kmemset != (void *)c_memset ||
        (void *)c_kstrlen != (void *)c_strlen ||
        (void *)c_kstrcmp != (void *)c_strcmp ||
        (void *)c_kstrncmp != (void *)c_strncmp) {
        fail_head("C 版の別名が同一実体でない (alias 属性が効いていない)\n");
    }
}

/* ======================================================================== */

static void run(void)
{
    report("== kstring: asm 版 と C 版 の答え合わせ ==\n");

    case_alias_identity();
    case_memcpy(0);
    case_memcpy(1);
    case_memset(0);
    case_memset(1);
    case_strlen();
    case_strcmp();
    case_strncmp();
    case_strcpy();
    case_strncpy();
    case_memcmp();
    selftest_cases(&IMPL_ASM);
    selftest_cases(&IMPL_C);

    report("CHECKS ");
    report_i((long)g_checks);
    report(" FAILURES ");
    report_i((long)g_failures);
    report("\n");
    g_exit_code = (g_failures == 0) ? 0 : 1;
}

void kstring_start_c(long *sp);
__asm__(".text\n"
        ".globl _start\n"
        "_start:\n"
        "  movl %esp, %eax\n"
        "  andl $-16, %esp\n"
        "  pushl %eax\n"
        "  call kstring_start_c\n"
        "  hlt\n");

void kstring_start_c(long *sp)
{
    (void)sp;
    run();
    die(g_exit_code);
}
