/* ========================================================================= */
/*  KSTR_BENCH.C — kstring のアセンブリ版と C 版を実機で測る                  */
/*                                                                           */
/*  票: docs/archive/portability/TASK_KSTRING_BENCH.md                         */
/*                                                                           */
/*  目的は「x86 で lib/kstring_asm.asm を lib/kstring_c.c に替えてよいか」の  */
/*  **材料**を出すこと。判断はここではしない (票 §0)。                        */
/*                                                                           */
/*  ■ 両版の同居のしかた                                                     */
/*    出荷するソースそのもの (lib/kstring_asm.asm と lib/kstring_c.c) を      */
/*    このプログラムにもリンクする。13 本の名前が丸ごと衝突するので、        */
/*    tools/tests/test_kstring_c.py と同じ手で objcopy --redefine-syms に     */
/*    接頭辞を付けさせ、アセンブリ版を a_*、C 版を c_* にして 1 つの実行      */
/*    ファイルに入れる (build/programs.mk の kstr_bench 規則)。**写しは       */
/*    作らない** — 測る対象は出荷する実物でなければ意味が無い。              */
/*    名前を潰したので libc (-lc) の memcpy / strlen 等とも衝突しない。       */
/*                                                                           */
/*  ■ 出す行 (1 ケース 1 行、固定書式。人が読む必要は無い)                   */
/*    KSTR <関数> <asm|c> <長さ> <ずれ> <ティック> <回数>                     */
/*    KSTR MISMATCH <関数> <長さ> <ずれ>   両版の結果が違った (票 K1)         */
/*    KSTR DONE                            最後に必ず 1 行                    */
/*    集計は tools/kstr_bench_report.py。                                     */
/*                                                                           */
/*  ■ 回数の決め方                                                           */
/*    1 ケースにつき合計 KB_TARGET_BYTES (1MB) 以上を処理する回数から始め、  */
/*    所要が KB_MIN_TICKS (30 ティック = 0.3 秒) 未満なら回数を倍にして測り   */
/*    直す (100Hz タイマの分解能を確保する)。倍にするのは KB_MAX_DOUBLE 回    */
/*    まで — 止まらない計測にしないため。実際に使った回数は行に出すので、    */
/*    集計側は「1 回あたり」に直せる。                                        */
/*                                                                           */
/*  ■ 速度より先に正しさ (票 K1)                                             */
/*    計測の前に全ケースで両版を回し、戻り値と宛先バッファ全体 (番兵込み) を  */
/*    突き合わせる。食い違った関数は MISMATCH を出して**計測を飛ばす** —      */
/*    違うものを比べた数字を表に載せない。                                    */
/*                                                                           */
/*  ■ 所要時間                                                               */
/*    13 本 × 7 長 × 2 ずれ × 2 版 = 364 ケース。1 ケースは最低 30 ティック     */
/*    (0.3 秒) なので下限 110 秒、倍化と照合を入れて数分〜十数分かかる。       */
/*    遠隔実行の待ち時間を短く切らないこと ([V3])。                           */
/*                                                                           */
/*  main() が **ファイルの最初の関数** であること (OS32X の約束)。            */
/* ========================================================================= */
#include "os32api.h"

/* ---- 測る対象: lib/kstring_asm.asm の global 13 本 (a_ = asm, c_ = C) ---- */
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

/* ---- 定数 ([C4] ここが管理元) ------------------------------------------ */
#define KB_KIND_MEMCPY   0      /* void *(void *, const void *, u32)        */
#define KB_KIND_MEMSET   1      /* void *(void *, int, u32)                 */
#define KB_KIND_STRLEN   2      /* u32   (const char *)                     */
#define KB_KIND_STRCMP   3      /* int   (const char *, const char *)       */
#define KB_KIND_STRNCMP  4      /* int   (const char *, const char *, u32)  */
#define KB_KIND_MEMCMP   5      /* int   (const void *, const void *, u32)  */
#define KB_KIND_STRCPY   6      /* char *(char *, const char *)             */
#define KB_KIND_STRNCPY  7      /* char *(char *, const char *, u32)        */

#define KB_FN_MAX        16     /* 表の枠 (今は 13 本) */
#define KB_LEN_COUNT      7     /* 4 / 16 / 64 / 256 / 1K / 16K / 256K */
#define KB_ALIGN_COUNT    2     /* 0 = 両方 4 境界 / 1 = 片方 1 ずれ */

#define KB_MAX_LEN     262144UL /* 長さの最大 = 256KB */
#define KB_PAD             16UL /* ずれ (1) + 終端 (1) + 番兵の余白 */
#define KB_BUF         (KB_MAX_LEN + KB_PAD)

#define KB_TARGET_BYTES 1048576UL   /* 1 ケースで処理する最低バイト数 (1MB) */
#define KB_MIN_TICKS      30UL      /* これ未満なら回数を倍にする (0.3 秒) */
#define KB_MAX_DOUBLE      5        /* 倍にするのは何回までか (最大 32 倍) */

#define KB_POISON       0x5A    /* 宛先の初期値 (両版で同じ) */
#define KB_SET_VAL      0xA5    /* kmemset / memset に渡す値 */
#define KB_SEED       0x12345UL /* 入力の擬似乱数の種 */
#define KB_LINE_MAX       96    /* 1 行の上限 */
#define KB_YIELD_CHUNK  1024    /* 何バイト書いたら sys_yield するか */

/* ---- 関数ポインタの束 --------------------------------------------------
 * C89 の共用体初期化は第 1 メンバだけなので、表は実行時に組む。
 * 関数ポインタ同士のキャストを 1 つも書かずに 8 種の型を 1 つの表に載せる
 * ための共用体 (キャストで畳むと型の取り違えが -Wall に映らなくなる)。 */
typedef union {
    void *(*memcpy_fn)(void *, const void *, u32);
    void *(*memset_fn)(void *, int, u32);
    u32   (*strlen_fn)(const char *);
    int   (*strcmp_fn)(const char *, const char *);
    int   (*strncmp_fn)(const char *, const char *, u32);
    int   (*memcmp_fn)(const void *, const void *, u32);
    char *(*strcpy_fn)(char *, const char *);
    char *(*strncpy_fn)(char *, const char *, u32);
} KbFnPtr;

typedef struct {
    const char *name;
    int         kind;
    KbFnPtr     fa;         /* アセンブリ版 */
    KbFnPtr     fc;         /* C 版 */
    int         skip;       /* 1 = 結果が食い違ったので計測しない */
} KbFn;

/* ---- 前方宣言 (main を最初の関数にするため) ---------------------------- */
static void  kb_build_table(void);
static void  kb_verify_all(void);
static void  kb_bench_all(void);
static void  kb_case_offsets(int kind, int align, u32 *off_s, u32 *off_d);
static void  kb_prepare(int kind, u32 len, u32 off_s, u32 off_d);
static u32   kb_call(int kind, KbFnPtr f, u8 *dst, u8 *src, u32 len);
static u32   kb_measure(int kind, KbFnPtr f, u32 len, u32 off_s, u32 off_d,
                        u32 *reps_out);
static u32   kb_reps_for(u32 len);
static void  kb_emit(const char *name, const char *variant, u32 len, int align,
                     u32 ticks, u32 reps);
static void  kb_emit_mismatch(const char *name, u32 len, int align);
static void  kb_out(const char *s, u32 n);
static u32   kb_u32(char *buf, u32 v);
static u32   kb_word(char *buf, u32 pos, const char *s);
static void  kb_fill_src(void);
static void  kb_plant_nul(u32 pos);
static void  kb_memset8(u8 *p, u8 v, u32 n);
static void  kb_copy8(u8 *d, const u8 *s, u32 n);
static int   kb_diff8(const u8 *a, const u8 *b, u32 n);
static int   kb_is_cmp(int kind);
static int   kb_writes_dst(int kind);

static KernelAPI *kb_api;
static KbFn  kb_fns[KB_FN_MAX];
static int   kb_nfn;
static int   kb_mismatch;
static int   kb_out_err;
static u32   kb_out_since_yield;
static volatile u32 kb_sink;    /* 呼び出しを最適化で消させない受け皿 */
static u32   kb_nul_pos;        /* 直前に 0 を植えた位置 */
static u8    kb_nul_saved;      /* そこに元々あったバイト */
static int   kb_nul_valid;

static const u32 kb_lens[KB_LEN_COUNT] = {
    4UL, 16UL, 64UL, 256UL, 1024UL, 16384UL, 262144UL
};

/* 入力 / 宛先。番兵ぶん余分に取り、比較は**バッファ全体**で行う。 */
static u8 kb_src[KB_BUF];
static u8 kb_da[KB_BUF];        /* asm 版の宛先 / 比較系の第 2 引数 */
static u8 kb_dc[KB_BUF];        /* C 版の宛先 / 比較系の第 2 引数 */

/* ========================================================================= */
int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;

    kb_api = api;
    kb_mismatch = 0;
    kb_out_err = 0;
    kb_out_since_yield = 0UL;
    kb_sink = 0UL;
    kb_nul_valid = 0;

    kb_build_table();
    kb_fill_src();

    kb_verify_all();        /* 票 K1: 速さより先に「同じ結果か」 */
    kb_bench_all();

    kb_out("KSTR DONE\n", 10UL);

    if (kb_out_err) return 2;
    if (kb_mismatch) return 1;
    return 0;
}

/* ---- 関数ポインタを共用体に載せる小物 (キャストを使わないため) --------- */
static KbFnPtr u_memcpy(void *(*f)(void *, const void *, u32))
{ KbFnPtr u; u.memcpy_fn = f; return u; }
static KbFnPtr u_memset(void *(*f)(void *, int, u32))
{ KbFnPtr u; u.memset_fn = f; return u; }
static KbFnPtr u_strlen(u32 (*f)(const char *))
{ KbFnPtr u; u.strlen_fn = f; return u; }
static KbFnPtr u_strcmp(int (*f)(const char *, const char *))
{ KbFnPtr u; u.strcmp_fn = f; return u; }
static KbFnPtr u_strncmp(int (*f)(const char *, const char *, u32))
{ KbFnPtr u; u.strncmp_fn = f; return u; }
static KbFnPtr u_memcmp(int (*f)(const void *, const void *, u32))
{ KbFnPtr u; u.memcmp_fn = f; return u; }
static KbFnPtr u_strcpy(char *(*f)(char *, const char *))
{ KbFnPtr u; u.strcpy_fn = f; return u; }
static KbFnPtr u_strncpy(char *(*f)(char *, const char *, u32))
{ KbFnPtr u; u.strncpy_fn = f; return u; }

static void kb_add(const char *name, int kind, KbFnPtr fa, KbFnPtr fc)
{
    if (kb_nfn >= KB_FN_MAX) return;
    kb_fns[kb_nfn].name = name;
    kb_fns[kb_nfn].kind = kind;
    kb_fns[kb_nfn].fa   = fa;
    kb_fns[kb_nfn].fc   = fc;
    kb_fns[kb_nfn].skip = 0;
    kb_nfn++;
}

/* lib/kstring_asm.asm の global と 1 対 1。増減したらここも直す
 * (tools/tests/test_kstr_bench.py が .asm の global と突き合わせる)。 */
static void kb_build_table(void)
{
    kb_nfn = 0;
    kb_add("kmemcpy",  KB_KIND_MEMCPY,  u_memcpy(a_kmemcpy),   u_memcpy(c_kmemcpy));
    kb_add("memcpy",   KB_KIND_MEMCPY,  u_memcpy(a_memcpy),    u_memcpy(c_memcpy));
    kb_add("kmemset",  KB_KIND_MEMSET,  u_memset(a_kmemset),   u_memset(c_kmemset));
    kb_add("memset",   KB_KIND_MEMSET,  u_memset(a_memset),    u_memset(c_memset));
    kb_add("kstrlen",  KB_KIND_STRLEN,  u_strlen(a_kstrlen),   u_strlen(c_kstrlen));
    kb_add("strlen",   KB_KIND_STRLEN,  u_strlen(a_strlen),    u_strlen(c_strlen));
    kb_add("kstrcmp",  KB_KIND_STRCMP,  u_strcmp(a_kstrcmp),   u_strcmp(c_kstrcmp));
    kb_add("strcmp",   KB_KIND_STRCMP,  u_strcmp(a_strcmp),    u_strcmp(c_strcmp));
    kb_add("kstrncmp", KB_KIND_STRNCMP, u_strncmp(a_kstrncmp), u_strncmp(c_kstrncmp));
    kb_add("strncmp",  KB_KIND_STRNCMP, u_strncmp(a_strncmp),  u_strncmp(c_strncmp));
    kb_add("kstrcpy",  KB_KIND_STRCPY,  u_strcpy(a_kstrcpy),   u_strcpy(c_kstrcpy));
    kb_add("kstrncpy", KB_KIND_STRNCPY, u_strncpy(a_kstrncpy), u_strncpy(c_kstrncpy));
    kb_add("memcmp",   KB_KIND_MEMCMP,  u_memcmp(a_memcmp),    u_memcmp(c_memcmp));
}

static int kb_is_cmp(int kind)
{
    return kind == KB_KIND_STRCMP || kind == KB_KIND_STRNCMP ||
           kind == KB_KIND_MEMCMP;
}

static int kb_writes_dst(int kind)
{
    return kind == KB_KIND_MEMCPY || kind == KB_KIND_MEMSET ||
           kind == KB_KIND_STRCPY || kind == KB_KIND_STRNCPY;
}

/* ずれの割り当て。「片方 1 ずれ」は**その関数で意味のある側**をずらす:
 * 転送・比較・長さは読み側 (src)、memset は書き側 (dst)。 */
static void kb_case_offsets(int kind, int align, u32 *off_s, u32 *off_d)
{
    *off_s = 0UL;
    *off_d = 0UL;
    if (align == 0) return;
    if (kind == KB_KIND_MEMSET) *off_d = 1UL;
    else                        *off_s = 1UL;
}

/* ---- 入力の用意 -------------------------------------------------------- */
/* 0 バイトを含まない擬似乱数 (0x80 以上も出す — 符号の扱いを踏むため)。 */
static void kb_fill_src(void)
{
    u32 i;
    u32 x = KB_SEED;

    for (i = 0UL; i < KB_BUF; i++) {
        x = x * 1103515245UL + 12345UL;
        kb_src[i] = (u8)(((x >> 16) & 0xFFUL) | 1UL);
    }
}

/* 文字列系の終端を植える。**前に植えた 0 は必ず戻す** — 戻さないと短い
 * ケースの終端が kb_src に残り、長いケースで測っているのは実は短い文字列に
 * なる (kstrlen が 4 を返し続ける)。両版が同じ入力を見るので MISMATCH には
 * ならず、表だけが静かに嘘になる。 */
static void kb_plant_nul(u32 pos)
{
    if (kb_nul_valid) kb_src[kb_nul_pos] = kb_nul_saved;
    kb_nul_saved = kb_src[pos];
    kb_nul_pos = pos;
    kb_nul_valid = 1;
    kb_src[pos] = 0;
}

static void kb_memset8(u8 *p, u8 v, u32 n)
{
    u32 i;
    for (i = 0UL; i < n; i++) p[i] = v;
}

static void kb_copy8(u8 *d, const u8 *s, u32 n)
{
    u32 i;
    for (i = 0UL; i < n; i++) d[i] = s[i];
}

static int kb_diff8(const u8 *a, const u8 *b, u32 n)
{
    u32 i;
    for (i = 0UL; i < n; i++) {
        if (a[i] != b[i]) return 1;
    }
    return 0;
}

/* 1 ケースぶんの入力を組む。宛先は**両方とも**同じ毒で埋め、比較系では
 * 第 2 引数として src と同じ内容 (最後の 1 バイトだけ違う) を置く。 */
static void kb_prepare(int kind, u32 len, u32 off_s, u32 off_d)
{
    kb_plant_nul(off_s + len);          /* 文字列系の終端 */

    kb_memset8(kb_da, KB_POISON, KB_BUF);
    kb_memset8(kb_dc, KB_POISON, KB_BUF);

    if (kb_is_cmp(kind)) {
        kb_copy8(kb_da + off_d, kb_src + off_s, len + 1UL);
        kb_copy8(kb_dc + off_d, kb_src + off_s, len + 1UL);
        if (len > 0UL) {
            kb_da[off_d + len - 1UL] = (u8)(kb_da[off_d + len - 1UL] ^ 0x20);
            kb_dc[off_d + len - 1UL] = (u8)(kb_dc[off_d + len - 1UL] ^ 0x20);
        }
    }
}

/* 1 回呼ぶ。戻り値は突き合わせ用に u32 へ畳む。ポインタを返す関数は
 * 「宛先そのものを返したか」に畳む (asm 版と C 版で宛先が別なので値は違う)。 */
static u32 kb_call(int kind, KbFnPtr f, u8 *dst, u8 *src, u32 len)
{
    switch (kind) {
    case KB_KIND_MEMCPY:
        return (u32)(f.memcpy_fn((void *)dst, (const void *)src, len)
                     == (void *)dst);
    case KB_KIND_MEMSET:
        return (u32)(f.memset_fn((void *)dst, KB_SET_VAL, len) == (void *)dst);
    case KB_KIND_STRLEN:
        return f.strlen_fn((const char *)src);
    case KB_KIND_STRCMP:
        return (u32)f.strcmp_fn((const char *)src, (const char *)dst);
    case KB_KIND_STRNCMP:
        return (u32)f.strncmp_fn((const char *)src, (const char *)dst, len);
    case KB_KIND_MEMCMP:
        return (u32)f.memcmp_fn((const void *)src, (const void *)dst, len);
    case KB_KIND_STRCPY:
        return (u32)(f.strcpy_fn((char *)dst, (const char *)src) == (char *)dst);
    case KB_KIND_STRNCPY:
        return (u32)(f.strncpy_fn((char *)dst, (const char *)src, len)
                     == (char *)dst);
    default:
        break;
    }
    return 0UL;
}

/* ---- 票 K1: 全ケースで両版の結果を突き合わせる ------------------------- */
static void kb_verify_all(void)
{
    int fi, li, ai;

    for (fi = 0; fi < kb_nfn; fi++) {
        for (li = 0; li < KB_LEN_COUNT; li++) {
            for (ai = 0; ai < KB_ALIGN_COUNT; ai++) {
                u32 len = kb_lens[li];
                u32 off_s, off_d;
                u32 ra, rc;
                int kind = kb_fns[fi].kind;

                kb_case_offsets(kind, ai, &off_s, &off_d);
                kb_prepare(kind, len, off_s, off_d);

                ra = kb_call(kind, kb_fns[fi].fa, kb_da + off_d,
                             kb_src + off_s, len);
                rc = kb_call(kind, kb_fns[fi].fc, kb_dc + off_d,
                             kb_src + off_s, len);

                if (ra != rc ||
                    (kb_writes_dst(kind) && kb_diff8(kb_da, kb_dc, KB_BUF))) {
                    kb_emit_mismatch(kb_fns[fi].name, len, ai);
                    kb_fns[fi].skip = 1;
                    kb_mismatch = 1;
                }
            }
        }
    }
}

/* ---- 計測 -------------------------------------------------------------- */
static u32 kb_reps_for(u32 len)
{
    u32 r;

    if (len == 0UL) return 1UL;
    r = KB_TARGET_BYTES / len;
    if (r == 0UL) r = 1UL;
    return r;
}

/* ticks が KB_MIN_TICKS 未満なら回数を倍にして測り直す。
 * 実際に使った回数を *reps_out に返す (集計側が「1 回あたり」に直せる)。 */
static u32 kb_measure(int kind, KbFnPtr f, u32 len, u32 off_s, u32 off_d,
                      u32 *reps_out)
{
    u32 reps = kb_reps_for(len);
    u32 ticks = 0UL;
    int doubled = 0;

    for (;;) {
        u32 i, start;

        start = kb_api->get_tick();
        for (i = 0UL; i < reps; i++) {
            kb_sink += kb_call(kind, f, kb_da + off_d, kb_src + off_s, len);
        }
        ticks = kb_api->get_tick() - start;

        if (ticks >= KB_MIN_TICKS) break;
        if (doubled >= KB_MAX_DOUBLE) break;
        reps *= 2UL;
        doubled++;
    }

    *reps_out = reps;
    return ticks;
}

static void kb_bench_all(void)
{
    int fi, li, ai;

    for (fi = 0; fi < kb_nfn; fi++) {
        if (kb_fns[fi].skip) continue;      /* 違うものを比べた数字は出さない */
        for (li = 0; li < KB_LEN_COUNT; li++) {
            for (ai = 0; ai < KB_ALIGN_COUNT; ai++) {
                u32 len = kb_lens[li];
                u32 off_s, off_d;
                u32 ticks, reps;
                int kind = kb_fns[fi].kind;

                kb_case_offsets(kind, ai, &off_s, &off_d);

                kb_prepare(kind, len, off_s, off_d);
                ticks = kb_measure(kind, kb_fns[fi].fa, len, off_s, off_d, &reps);
                kb_emit(kb_fns[fi].name, "asm", len, ai, ticks, reps);

                kb_prepare(kind, len, off_s, off_d);
                ticks = kb_measure(kind, kb_fns[fi].fc, len, off_s, off_d, &reps);
                kb_emit(kb_fns[fi].name, "c", len, ai, ticks, reps);
            }
        }
    }
}

/* ---- 出力 -------------------------------------------------------------- */
static u32 kb_u32(char *buf, u32 v)
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

static u32 kb_word(char *buf, u32 pos, const char *s)
{
    u32 i = 0UL;
    while (s[i] != '\0' && pos + i < (u32)(KB_LINE_MAX - 2)) {
        buf[pos + i] = s[i];
        i++;
    }
    return pos + i;
}

static void kb_emit(const char *name, const char *variant, u32 len, int align,
                    u32 ticks, u32 reps)
{
    char line[KB_LINE_MAX];
    u32 p = 0UL;

    p = kb_word(line, p, "KSTR ");
    p = kb_word(line, p, name);
    line[p++] = ' ';
    p = kb_word(line, p, variant);
    line[p++] = ' ';
    p += kb_u32(line + p, len);
    line[p++] = ' ';
    p += kb_u32(line + p, (u32)align);
    line[p++] = ' ';
    p += kb_u32(line + p, ticks);
    line[p++] = ' ';
    p += kb_u32(line + p, reps);
    line[p++] = '\n';
    kb_out(line, p);
}

static void kb_emit_mismatch(const char *name, u32 len, int align)
{
    char line[KB_LINE_MAX];
    u32 p = 0UL;

    p = kb_word(line, p, "KSTR MISMATCH ");
    p = kb_word(line, p, name);
    line[p++] = ' ';
    p += kb_u32(line + p, len);
    line[p++] = ' ';
    p += kb_u32(line + p, (u32)align);
    line[p++] = '\n';
    kb_out(line, p);
}

/* short write を「書けた」ことにしない。GUI 端末の con_sink は 8KB の環で
 * 満杯になると古い行から捨てるので、1KB ごとに sys_yield を挟む
 * (userland/tests/cfg_bench.c と同じ作法)。 */
static void kb_out(const char *s, u32 n)
{
    u32 done = 0UL;

    if (kb_api == 0) { kb_out_err = 1; return; }
    while (done < n) {
        int rc = kb_api->sys_write(1, s + done, n - done);
        if (rc <= 0) { kb_out_err = 1; return; }
        done += (u32)rc;
        kb_out_since_yield += (u32)rc;
        if (kb_out_since_yield >= (u32)KB_YIELD_CHUNK) {
            kb_out_since_yield = 0UL;
            kb_api->sys_yield();
        }
    }
}
