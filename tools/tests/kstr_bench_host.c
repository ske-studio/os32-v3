/* ========================================================================= */
/*  KSTR_BENCH_HOST.C — kstr_bench の**計測の枠組み**をホストで回す           */
/*                                                                           */
/*  票:   docs/archive/portability/TASK_KSTRING_BENCH.md (受入 K1)             */
/*  記録: tools/tests/kstr_bench_tdd.md                                      */
/*  駆動: tools/tests/test_kstr_bench.py                                     */
/*                                                                           */
/*  実物の userland/tests/kstr_bench.c を 1 行も写さずそのまま #include し、  */
/*  main だけ改名する。差し替えるのは KernelAPI (get_tick / sys_write /       */
/*  sys_yield) と、測られる側の 13 本。2 通りにビルドする:                    */
/*                                                                           */
/*    -DKSTRB_FAKE  贋の kstring 13 本 × 2 版をここで定義する。get_tick は    */
/*                  **呼び出し回数 × KSTRB_TICK_PER_CALL** で進むので、       */
/*                  「30 ティック未満なら回数を倍」の規則が効いているかを      */
/*                  出力の <回数> で 1 の位まで確かめられる。                 */
/*                  -DKSTRB_MISMATCH_NAME="kstrlen" で c_ 版 1 本をわざと     */
/*                  壊し、MISMATCH が出てその関数の計測が飛ぶことを見る。     */
/*    -DKSTRB_REAL  実物の lib/kstring_asm.asm (nasm) と実物の                */
/*                  lib/kstring_c.c を a_* / c_* に改名して同じ実行ファイル   */
/*                  へリンクする (ゲストと同じ同居のしかた)。get_tick は 1 回 */
/*                  ごとに KSTRB_REAL_TICK_STEP 進むので倍化は起きず、        */
/*                  1 ケース 1MB で終わる。**時間の数字に意味は無い** —       */
/*                  ここで見るのは 13 本が実際に一致すること (票 K1) と、     */
/*                  出力が書式どおりのこと。                                  */
/*                                                                           */
/*  設定はすべて -D で渡す。libc は使わない (-nostdlib、Linux の int 0x80 で  */
/*  write / exit するだけ) — 32bit の libc ヘッダが要らないようにするため     */
/*  (tools/tests/kstring_c_host.c と同じ作法)。argv を読まないのはそのため。 */
/* ========================================================================= */

#include "os32api.h"

#if !defined(KSTRB_FAKE) && !defined(KSTRB_REAL)
#error "KSTRB_FAKE か KSTRB_REAL のどちらかを指定すること"
#endif

/* get_tick が 1 回呼ばれるたびに進む量。REAL では倍化を起こさせないため
 * KB_MIN_TICKS (30) より大きく取る。FAKE では 0 にして、贋の kstring 側が
 * 呼び出しごとに進める (回数の決め方を 1 の位まで再現するため)。 */
#define KSTRB_REAL_TICK_STEP  40UL

#ifndef KSTRB_TICK_PER_CALL
#define KSTRB_TICK_PER_CALL   0UL
#endif
#ifndef KSTRB_SHORT_WRITE
#define KSTRB_SHORT_WRITE     0
#endif
/* 壊す 1 本の名前。空 = 壊さない。 */
#ifndef KSTRB_MISMATCH_NAME
#define KSTRB_MISMATCH_NAME   ""
#endif

/* ---- libc の代わり (-nostdlib) ----------------------------------------- */
static void h_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) { }
}

static int h_write(int fd, const void *buf, u32 len)
{
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(4), "b"(fd), "c"(buf), "d"(len)
                     : "memory");
    return ret;
}

/* ---- 贋 KernelAPI ------------------------------------------------------ */
static KernelAPI g_api;
static u32 fk_tick;
static u32 fk_tick_step;        /* get_tick 1 回あたりの進み */

static u32 __cdecl fk_get_tick(void)
{
    u32 v = fk_tick;
    fk_tick += fk_tick_step;
    return v;
}

static int __cdecl fk_sys_write(int fd, const void *buf, u32 size)
{
    u32 n = size;

    if (fd != 1) return -1;
    if (n == 0UL) return 0;
#if KSTRB_SHORT_WRITE
    if (n > 1UL) n = 1UL;       /* short write の側: 1 バイトずつしか受けない */
#endif
    return h_write(1, buf, n);
}

static i32 __cdecl fk_sys_yield(void)
{
    return 0;
}

/* ========================================================================= */
/*  贋の kstring 13 本 × 2 版 (FAKE のみ)                                     */
/* ========================================================================= */
#ifdef KSTRB_FAKE

static u32 h_u32(char *buf, u32 v)
{
    char tmp[12];
    u32 n = 0UL;
    u32 i;
    if (v == 0UL) { buf[0] = '0'; return 1UL; }
    while (v > 0UL && n < 12UL) {
        tmp[n++] = (char)('0' + (int)(v % 10UL));
        v /= 10UL;
    }
    for (i = 0UL; i < n; i++) buf[i] = tmp[n - 1UL - i];
    return n;
}

static int h_streq(const char *a, const char *b)
{
    u32 i = 0UL;
    while (a[i] != '\0' && a[i] == b[i]) i++;
    return a[i] == b[i];
}

enum {
    FK_KMEMCPY = 0, FK_MEMCPY, FK_KMEMSET, FK_MEMSET,
    FK_KSTRLEN, FK_STRLEN, FK_KSTRCMP, FK_STRCMP,
    FK_KSTRNCMP, FK_STRNCMP, FK_KSTRCPY, FK_KSTRNCPY, FK_MEMCMP,
    FK_COUNT
};

static const char *fk_names[FK_COUNT] = {
    "kmemcpy", "memcpy", "kmemset", "memset",
    "kstrlen", "strlen", "kstrcmp", "strcmp",
    "kstrncmp", "strncmp", "kstrcpy", "kstrncpy", "memcmp"
};

static int fk_bad[FK_COUNT];    /* 1 = この関数の c_ 版をわざと壊す */

/* ---- 長さの目撃記録 ------------------------------------------------------
 * 「この関数に**実際に渡された長さ**」を 7 通りの表と突き合わせて覚える。
 * kstr_bench.c が入力を組み損ねると (例: 前のケースの NUL が kb_src に残って
 * 256KB の文字列が 4 バイトに化ける)、両版とも同じ壊れた入力を見るので
 * MISMATCH にはならず、表だけが静かに嘘になる。ここが唯一の受け手。
 * 終了時に stderr へ `WITNESS <名前> <7 通りのビット> <表に無い長さの回数>`。 */
#define FK_LEN_COUNT 7
static const u32 fk_lens[FK_LEN_COUNT] = {
    4UL, 16UL, 64UL, 256UL, 1024UL, 16384UL, 262144UL
};
static u32 fk_mask[FK_COUNT];
static u32 fk_odd[FK_COUNT];
static u32 fk_last[FK_COUNT];

static void fk_note(int ix, u32 len)
{
    int i;

    if (fk_last[ix] == len) return;     /* 同じ長さの繰り返しは 1 回だけ見る */
    fk_last[ix] = len;
    for (i = 0; i < FK_LEN_COUNT; i++) {
        if (fk_lens[i] == len) { fk_mask[ix] |= (1UL << i); return; }
    }
    fk_odd[ix]++;
}

static void fk_report_witness(void)
{
    char line[96];
    int j;
    u32 p;
    u32 k;

    for (j = 0; j < FK_COUNT; j++) {
        p = 0UL;
        line[p++] = 'W'; line[p++] = 'I'; line[p++] = 'T'; line[p++] = 'N';
        line[p++] = 'E'; line[p++] = 'S'; line[p++] = 'S'; line[p++] = ' ';
        for (k = 0UL; fk_names[j][k] != '\0'; k++) line[p++] = fk_names[j][k];
        line[p++] = ' ';
        p += h_u32(line + p, fk_mask[j]);
        line[p++] = ' ';
        p += h_u32(line + p, fk_odd[j]);
        line[p++] = '\n';
        h_write(2, line, p);
    }
}

static void fk_bump(void)
{
    fk_tick += (u32)KSTRB_TICK_PER_CALL;
}

/* ---- 契約どおりの素朴な実装 (asm 版 / 正しい C 版の両方がこれを使う) ---- */
static void *fk_do_memcpy(void *d, const void *s, u32 n)
{
    u8 *dd = (u8 *)d;
    const u8 *ss = (const u8 *)s;
    u32 i;
    for (i = 0UL; i < n; i++) dd[i] = ss[i];
    return d;
}

static void *fk_do_memset(void *d, int v, u32 n)
{
    u8 *dd = (u8 *)d;
    u32 i;
    for (i = 0UL; i < n; i++) dd[i] = (u8)(v & 0xFF);
    return d;
}

static u32 fk_do_strlen(const char *s)
{
    u32 n = 0UL;
    while (s[n] != '\0') n++;
    return n;
}

static int fk_do_strcmp(const char *a, const char *b)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    while (*pa != 0 && *pa == *pb) { pa++; pb++; }
    return (int)*pa - (int)*pb;
}

static int fk_do_strncmp(const char *a, const char *b, u32 n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    while (n-- != 0UL) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        if (*pa == 0) return 0;
        pa++; pb++;
    }
    return 0;
}

static int fk_do_memcmp(const void *a, const void *b, u32 n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    while (n-- != 0UL) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

static char *fk_do_strcpy(char *d, const char *s)
{
    u32 i = 0UL;
    while (s[i] != '\0') { d[i] = s[i]; i++; }
    d[i] = '\0';
    return d;
}

/* kstrncpy は BSD strlcpy 相当 — 残りを 0 で埋めない (lib/kstring_c.c の契約) */
static char *fk_do_strncpy(char *d, const char *s, u32 n)
{
    u32 i;
    if (n == 0UL) return d;
    for (i = 0UL; (i + 1UL) < n && s[i] != '\0'; i++) d[i] = s[i];
    d[i] = '\0';
    return d;
}

/* ---- 壊し方: 宛先に書く関数は 1 バイト裏返し、値を返す関数は値をずらす -- */
#define FK_PAIR_MEMCPY(nm, ix) \
    void *a_##nm(void *d, const void *s, u32 n) \
    { fk_bump(); fk_note(ix, n); return fk_do_memcpy(d, s, n); } \
    void *c_##nm(void *d, const void *s, u32 n) \
    { void *r; fk_bump(); r = fk_do_memcpy(d, s, n); \
      if (fk_bad[ix] && n > 0UL) { ((u8 *)d)[0] = (u8)(((u8 *)d)[0] ^ 0xFF); } \
      return r; }

#define FK_PAIR_MEMSET(nm, ix) \
    void *a_##nm(void *d, int v, u32 n) \
    { fk_bump(); fk_note(ix, n); return fk_do_memset(d, v, n); } \
    void *c_##nm(void *d, int v, u32 n) \
    { void *r; fk_bump(); r = fk_do_memset(d, v, n); \
      if (fk_bad[ix] && n > 0UL) { ((u8 *)d)[0] = (u8)(((u8 *)d)[0] ^ 0xFF); } \
      return r; }

#define FK_PAIR_STRLEN(nm, ix) \
    u32 a_##nm(const char *s) \
    { u32 r; fk_bump(); r = fk_do_strlen(s); fk_note(ix, r); return r; } \
    u32 c_##nm(const char *s) \
    { u32 r; fk_bump(); r = fk_do_strlen(s); \
      if (fk_bad[ix]) { r = r + 1UL; } \
      return r; }

#define FK_PAIR_STRCMP(nm, ix) \
    int a_##nm(const char *a, const char *b) \
    { fk_bump(); fk_note(ix, fk_do_strlen(a)); return fk_do_strcmp(a, b); } \
    int c_##nm(const char *a, const char *b) \
    { int r; fk_bump(); r = fk_do_strcmp(a, b); \
      if (fk_bad[ix]) { r = -r; } \
      return r; }

#define FK_PAIR_STRNCMP(nm, ix) \
    int a_##nm(const char *a, const char *b, u32 n) \
    { fk_bump(); fk_note(ix, n); return fk_do_strncmp(a, b, n); } \
    int c_##nm(const char *a, const char *b, u32 n) \
    { int r; fk_bump(); r = fk_do_strncmp(a, b, n); \
      if (fk_bad[ix]) { r = -r; } \
      return r; }

#define FK_PAIR_MEMCMP(nm, ix) \
    int a_##nm(const void *a, const void *b, u32 n) \
    { fk_bump(); fk_note(ix, n); return fk_do_memcmp(a, b, n); } \
    int c_##nm(const void *a, const void *b, u32 n) \
    { int r; fk_bump(); r = fk_do_memcmp(a, b, n); \
      if (fk_bad[ix]) { r = -r; } \
      return r; }

#define FK_PAIR_STRCPY(nm, ix) \
    char *a_##nm(char *d, const char *s) \
    { fk_bump(); fk_note(ix, fk_do_strlen(s)); return fk_do_strcpy(d, s); } \
    char *c_##nm(char *d, const char *s) \
    { char *r; fk_bump(); r = fk_do_strcpy(d, s); \
      if (fk_bad[ix]) { d[0] = (char)(d[0] ^ 0x01); } \
      return r; }

#define FK_PAIR_STRNCPY(nm, ix) \
    char *a_##nm(char *d, const char *s, u32 n) \
    { fk_bump(); fk_note(ix, n); return fk_do_strncpy(d, s, n); } \
    char *c_##nm(char *d, const char *s, u32 n) \
    { char *r; fk_bump(); r = fk_do_strncpy(d, s, n); \
      if (fk_bad[ix] && n > 0UL) { d[0] = (char)(d[0] ^ 0x01); } \
      return r; }

FK_PAIR_MEMCPY(kmemcpy,   FK_KMEMCPY)
FK_PAIR_MEMCPY(memcpy,    FK_MEMCPY)
FK_PAIR_MEMSET(kmemset,   FK_KMEMSET)
FK_PAIR_MEMSET(memset,    FK_MEMSET)
FK_PAIR_STRLEN(kstrlen,   FK_KSTRLEN)
FK_PAIR_STRLEN(strlen,    FK_STRLEN)
FK_PAIR_STRCMP(kstrcmp,   FK_KSTRCMP)
FK_PAIR_STRCMP(strcmp,    FK_STRCMP)
FK_PAIR_STRNCMP(kstrncmp, FK_KSTRNCMP)
FK_PAIR_STRNCMP(strncmp,  FK_STRNCMP)
FK_PAIR_STRCPY(kstrcpy,   FK_KSTRCPY)
FK_PAIR_STRNCPY(kstrncpy, FK_KSTRNCPY)
FK_PAIR_MEMCMP(memcmp,    FK_MEMCMP)

/* 壊す 1 本を名前で選ぶ。名前が表に無ければ**黙って 0 本壊す**のではなく
 * 終了コード 3 で落ちる (試験が「壊したつもり」で緑になるのを防ぐ)。 */
static int fk_arm_mismatch(void)
{
    const char *want = KSTRB_MISMATCH_NAME;
    int j;
    int found = 0;

    if (want[0] == '\0') return 0;         /* 壊さない (既定) */
    for (j = 0; j < FK_COUNT; j++) {
        if (h_streq(fk_names[j], want)) { fk_bad[j] = 1; found = 1; }
    }
    if (!found) {
        h_write(2, "kstr_bench_host: unknown KSTRB_MISMATCH_NAME\n", 45UL);
        return -1;
    }
    return 0;
}

#endif /* KSTRB_FAKE */

/* ========================================================================= */
/*  実物の kstr_bench.c をそのまま取り込む (main だけ改名)                    */
/* ========================================================================= */
#define main kstr_bench_prog_main
#include "../../userland/tests/kstr_bench.c"
#undef main

/* ========================================================================= */
static char *g_argv[2];

void _start(void);
void _start(void)
{
    int rc;

#ifdef KSTRB_FAKE
    fk_tick_step = 0UL;
    if (fk_arm_mismatch() < 0) h_exit(3);
#else
    fk_tick_step = KSTRB_REAL_TICK_STEP;
#endif
    fk_tick = 0UL;

    g_api.get_tick  = fk_get_tick;
    g_api.sys_write = fk_sys_write;
    g_api.sys_yield = fk_sys_yield;

    g_argv[0] = (char *)"kstr_bench";
    g_argv[1] = (char *)0;

    rc = kstr_bench_prog_main(1, g_argv, &g_api);
#ifdef KSTRB_FAKE
    fk_report_witness();
#endif
    h_exit(rc);
}
