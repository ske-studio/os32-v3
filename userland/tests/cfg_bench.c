/* ======================================================================== */
/*  CFG_BENCH.C — 設定レジストリの実測 (票 docs/archive/settings/TASK_S5.md  */
/*                §0 の S5-C(1)、DESIGN §6 の M1 / M4 / M6)                  */
/*                                                                          */
/*    cfg_bench [n] [m]     読み: n 回 (既定 50) の                          */
/*                          「open(RO) → m 件 (既定 20) の get → close」      */
/*    cfg_bench -w [n]      書き: n 回 (既定 20) の                          */
/*                          「open(RW) → begin → set_int → commit → close」  */
/*                                                                          */
/*  出す行                                                                   */
/*    cfg_bench read n 50 m 20          何を測ったか (最初の 1 行)            */
/*    iter <i> ticks <t> pool <n> B     10 回ごと (と最終回)                  */
/*    ticks min <a> max <b> avg <c> total <s> n <k>                          */
/*    pool start <a> peak <b> end <c> B                                      */
/*    status <n> <名前>                 操作を終えて close する直前の状態      */
/*    failures <n>                      0 なら終了コード 0                    */
/*    saturated 1                       集計が u32 で飽和したときだけ出る      */
/*                                      (数字は信用できない = 失敗扱い)       */
/*                                                                          */
/*  規約 (libos32cfg.h の契約): **open 〜 close の間に yield しない**。        */
/*  計測の窓は cfg_open の直前から cfg_close の直後まで。コンソールへの出力は  */
/*  すべて窓の外 (1 回分の iter 行も、その回の close が済んでから印字する)。   */
/*  その窓の外では 1KB ごとに sys_yield を挟む — GUI 端末の con_sink は 8KB    */
/*  の環で、満杯になると**古い行から捨てる**。yield を挟まないと端末アプリが   */
/*  読み出す前に iter 行が消える (レビュー往復 1 の B4)。                     */
/*                                                                          */
/*  集計・引数解釈・整形は純関数に切り出してあり、tools/tests/cfg_host.c が    */
/*  同じソースを載せて直接呼ぶ (RED→GREEN は tools/tests/s5_tdd.md)。         */
/* ======================================================================== */

#include "os32api.h"
#include "cfg/libos32cfg.h"

/* ---- 既定値と上限 ([C4] 定数はここが管理元) ---------------------------- */
#define BN_DEF_READ_N    50      /* 読みの既定 回数 */
#define BN_DEF_READ_M    20      /* 読み 1 回あたりの get 件数 */
#define BN_DEF_WRITE_N   20      /* 書きの既定 回数 */
/* 上限は**集計の幅から決める** (レビュー往復 1 の B3)。1 回で数えうる失敗は
 * open 1 + get m + 状態 1 + begin/set/commit 3 + close 1 <= m + 6 なので、
 * 失敗の総数は n * (m + 6) <= 10000 * 1006 ~= 1.0e7 で u32 に十分収まる。
 * それでも溢れは飽和加算で検出し、`saturated 1` を出して失敗にする。 */
#define BN_MAX_N         10000
#define BN_MAX_M          1000
#define BN_U32_MAX  0xFFFFFFFFUL
#define BN_LOG_EVERY        10   /* 何回ごとに 1 行出すか */
#define BN_KEYS              3   /* 巡回する「存在するキー」の本数 */
#define BN_MISS_EVERY        4   /* 何件に 1 件を「無いキー」にするか */
#define BN_LINE_MAX        128   /* iter 行 1 本 */
#define BN_SUM_MAX         256   /* まとめ 4〜5 行 */
#define BN_YIELD_CHUNK    1024   /* 何バイト書いたら yield するか (cfg と同じ) */
#define BN_TEXT_MAX  (CFG_TEXT_MAX + 1)

/* 実測に使う番地 (assets/settings/defaults.tsv の 3 本 + 必ず無い 1 本)。 */
#define BN_SCOPE        "gshell"
#define BN_KEY_COLOR    "desktop/color"      /* int  */
#define BN_KEY_WALL     "desktop/wallpaper"  /* text */
#define BN_KEY_CLOCK    "taskbar/clock_24h"  /* int  */
#define BN_KEY_MISSING  "nosuch/key"
#define BN_WSCOPE       "app:bench"
#define BN_WKEY         "counter"

/* bn_pick の戻り */
#define BN_PICK_COLOR   0
#define BN_PICK_WALL    1
#define BN_PICK_CLOCK   2
#define BN_PICK_MISS    3

typedef struct {
    int write_mode;
    int n;
    int m;
} BnArgs;

typedef struct {
    u32 n;                       /* 取った標本の数 (<= BN_MAX_N) */
    u32 t_min, t_max, t_sum;
    u32 pool_start, pool_peak, pool_end;
    u32 failures;
    int saturated;               /* 1 = u32 で飽和した。数字は信用できない */
} BnStats;

/* 出力の組み立て。溢れたら err を立て、そこから先は 1 バイトも足さない。 */
typedef struct {
    char *buf;
    int   cap;
    int   len;
    int   err;
} BnOut;

/* --- 純関数 (ホスト TDD が直接呼ぶ) --- */
static int  bn_parse_u32(const char *s, u32 lo, u32 hi, int *out);
static int  bn_args(int argc, char **argv, BnArgs *out);
static int  bn_pick(int j);
static int  bn_get_failed(int rc);
static int  bn_should_log(int i, int n);
static u32  bn_add_sat(u32 a, u32 b, int *sat);
static void bn_reset(BnStats *s, u32 pool_start);
static void bn_sample(BnStats *s, u32 ticks, u32 pool_open, u32 pool_closed);
static u32  bn_avg(const BnStats *s);
static int  bn_bad(const BnStats *s);
static const char *bn_status_name(int st);
static const char *bn_key_of(int which);
static int  bn_iter_line(char *buf, int cap, int iter, u32 ticks, u32 pool);
static int  bn_summary(char *buf, int cap, const BnStats *s, int status);

/* --- 出力 --- */
static void bo_init(BnOut *o, char *buf, int cap);
static void bo_str(BnOut *o, const char *s);
static void bo_u32(BnOut *o, u32 v);
static int  bo_end(BnOut *o);
static int  bn_slen(const char *s);
static int  bn_emit(const char *p, int n);
static void bn_drain(void);
static void bn_puts(const char *s);
static void bn_usage(void);
static void bn_log_iter(int iter, u32 ticks, u32 pool);
static void bn_report(const BnStats *s, int status);

/* --- 実測 --- */
static int  bn_one_get(CfgDb *db, int which, char *tbuf, int tcap);
static int  bn_run_read(const BnArgs *a);
static int  bn_run_write(const BnArgs *a);

static KernelAPI *bn_api;
static int bn_out_err;              /* 出力の取りこぼし → 終了コード */
static u32 bn_pending;              /* 直近の yield からの出力バイト数 */
/* 読んだ値の捨て場。**符号なしの XOR** で畳む — `+=` だと 2147483647 が 2 つ
 * 並んだだけで signed overflow (未定義動作) になる (レビュー往復 1 の B1)。
 * volatile なので最適化で消えない。 */
static volatile u32 bn_sink;

int main(int argc, char **argv, KernelAPI *api)
{
    BnArgs a;
    int rc;

    bn_api = api;
    bn_out_err = 0;
    bn_pending = 0;
    bn_sink = 0;

    if (bn_args(argc, argv, &a) != 0) { bn_usage(); bn_drain(); return 1; }
    if (api->version < 50) {
        bn_puts("cfg_bench: kernel is older than KAPI v50\n");
        bn_drain();
        return 1;
    }

    {   /* 何を測ったのかを最初の 1 行に残す (記録は票 §6)。 */
        char line[BN_LINE_MAX];
        BnOut o;
        bo_init(&o, line, (int)sizeof(line));
        bo_str(&o, a.write_mode ? "cfg_bench write n " : "cfg_bench read n ");
        bo_u32(&o, (u32)a.n);
        if (!a.write_mode) { bo_str(&o, " m "); bo_u32(&o, (u32)a.m); }
        bo_str(&o, "\n");
        if (bo_end(&o) < 0) bn_out_err = 1;
        else bn_emit(line, o.len);
    }

    rc = a.write_mode ? bn_run_write(&a) : bn_run_read(&a);
    bn_drain();                     /* 端末が吐き切る機会を最後にもう一度 */
    if (bn_out_err) return 1;
    return rc;
}

/* ======================================================================== */
/*  引数 (純関数)                                                            */
/* ======================================================================== */

/* 10 進の符号なし。前置 0 は許すが、空・非数字・範囲外は -1。
 * u32 で積んで hi を越えた時点で止める (符号付きの桁あふれを踏まない)。 */
static int bn_parse_u32(const char *s, u32 lo, u32 hi, int *out)
{
    u32 v = 0;
    int i;

    if (!s || !out) return -1;
    if (s[0] == '\0') return -1;
    for (i = 0; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10u + (u32)(s[i] - '0');
        if (v > hi) return -1;
    }
    if (v < lo) return -1;
    *out = (int)v;
    return 0;
}

/* cfg_bench            → 読み n=50 m=20
 * cfg_bench <n>        → 読み n=<n> m=20
 * cfg_bench <n> <m>    → 読み
 * cfg_bench -w         → 書き n=20
 * cfg_bench -w <n>     → 書き
 * それ以外は -1 (usage)。 */
static int bn_args(int argc, char **argv, BnArgs *out)
{
    if (!out) return -1;
    out->write_mode = 0;
    out->n = BN_DEF_READ_N;
    out->m = BN_DEF_READ_M;
    if (argc < 1 || !argv) return -1;

    if (argc >= 2 && argv[1] && argv[1][0] == '-') {
        if (argv[1][1] != 'w' || argv[1][2] != '\0') return -1;
        out->write_mode = 1;
        out->n = BN_DEF_WRITE_N;
        out->m = 0;
        if (argc == 2) return 0;
        if (argc != 3) return -1;
        return bn_parse_u32(argv[2], 1, BN_MAX_N, &out->n);
    }
    if (argc == 1) return 0;
    if (argc > 3) return -1;
    if (bn_parse_u32(argv[1], 1, BN_MAX_N, &out->n) != 0) return -1;
    if (argc == 2) return 0;
    return bn_parse_u32(argv[2], 1, BN_MAX_M, &out->m);
}

/* j 件目の get が引く先。BN_MISS_EVERY 件に 1 件は「無いキー」で、残りは
 * 存在する 3 キーの巡回。j - j/BN_MISS_EVERY が「無いキーを除いた通し番号」。*/
static int bn_pick(int j)
{
    if (j < 0) return BN_PICK_MISS;
    if ((j % BN_MISS_EVERY) == (BN_MISS_EVERY - 1)) return BN_PICK_MISS;
    return (j - j / BN_MISS_EVERY) % BN_KEYS;
}

/* get の戻りを失敗として数えるか。長さ / 0 は成功、NOTFOUND は「無いキー」
 * なので数えない (票 S5-C)。それ以外の負だけが失敗。 */
static int bn_get_failed(int rc)
{
    if (rc >= 0) return 0;
    if (rc == OS32_ERR_NOTFOUND) return 0;
    return 1;
}

/* i は 0 始まり。10 回ごと、および最終回 (短い n でも 1 行は残す)。 */
static int bn_should_log(int i, int n)
{
    if (i < 0) return 0;
    if (((i + 1) % BN_LOG_EVERY) == 0) return 1;
    return (i + 1) >= n;
}

/* ======================================================================== */
/*  集計 (純関数)                                                            */
/* ======================================================================== */

/* 飽和加算。溢れたら *sat を立てて上限で止める — 巻き戻った数字を
 * 「測れた値」として出さないため (レビュー往復 1 の B3)。 */
static u32 bn_add_sat(u32 a, u32 b, int *sat)
{
    if (a > BN_U32_MAX - b) {
        if (sat) *sat = 1;
        return (u32)BN_U32_MAX;
    }
    return a + b;
}

static void bn_reset(BnStats *s, u32 pool_start)
{
    s->n = 0;
    s->t_min = 0;
    s->t_max = 0;
    s->t_sum = 0;
    s->pool_start = pool_start;
    s->pool_peak = pool_start;
    s->pool_end = pool_start;
    s->failures = 0;
    s->saturated = 0;
}

/* pool_open = close の直前 (接続を抱えている間) の値、
 * pool_closed = close の直後の値。peak は**この 2 点の観測最大**であって、
 * prepare / step / commit の途中で一時的に確保される分を含む真の最大では
 * ない (レビュー往復 1 の non-blocker)。 */
static void bn_sample(BnStats *s, u32 ticks, u32 pool_open, u32 pool_closed)
{
    if (s->n == 0) {
        s->t_min = ticks;
        s->t_max = ticks;
    } else {
        if (ticks < s->t_min) s->t_min = ticks;
        if (ticks > s->t_max) s->t_max = ticks;
    }
    s->t_sum = bn_add_sat(s->t_sum, ticks, &s->saturated);
    s->n = bn_add_sat(s->n, 1, &s->saturated);
    if (pool_open > s->pool_peak) s->pool_peak = pool_open;
    if (pool_closed > s->pool_peak) s->pool_peak = pool_closed;
    s->pool_end = pool_closed;
}

/* 四捨五入。標本が無ければ 0。商と余りで丸めるので、`t_sum + n/2` のような
 * 途中の桁あふれを踏まない (`r >= n - r` は `2r >= n` と同じ)。 */
static u32 bn_avg(const BnStats *s)
{
    u32 q, r;
    if (s->n == 0) return 0;
    q = s->t_sum / s->n;
    r = s->t_sum % s->n;
    if (r != 0 && r >= s->n - r) q++;
    return q;
}

/* 「測り切れなかった」= 失敗があったか、集計が飽和したか。 */
static int bn_bad(const BnStats *s)
{
    return (s->failures != 0 || s->saturated) ? 1 : 0;
}

static const char *bn_status_name(int st)
{
    switch (st) {
    case CFG_OK:      return "OK";
    case CFG_MISSING: return "MISSING";
    case CFG_CORRUPT: return "CORRUPT";
    case CFG_VERSION: return "VERSION";
    default:          return "ERROR";
    }
}

static const char *bn_key_of(int which)
{
    switch (which) {
    case BN_PICK_COLOR: return BN_KEY_COLOR;
    case BN_PICK_WALL:  return BN_KEY_WALL;
    case BN_PICK_CLOCK: return BN_KEY_CLOCK;
    default:            return BN_KEY_MISSING;
    }
}

/* ======================================================================== */
/*  整形 (純関数)                                                            */
/* ======================================================================== */

static void bo_init(BnOut *o, char *buf, int cap)
{
    o->buf = buf;
    o->cap = cap;
    o->len = 0;
    o->err = (buf && cap > 0) ? 0 : 1;
}

static void bo_str(BnOut *o, const char *s)
{
    int i;
    if (o->err || !s) return;
    for (i = 0; s[i]; i++) {
        if (o->len + 1 >= o->cap) { o->err = 1; return; }
        o->buf[o->len++] = s[i];
    }
}

static void bo_u32(BnOut *o, u32 v)
{
    char tmp[12];
    int n = 0;
    if (o->err) return;
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = (char)('0' + (int)(v % 10u));
            v /= 10u;
        }
    }
    if (o->len + n >= o->cap) { o->err = 1; return; }
    while (n > 0) o->buf[o->len++] = tmp[--n];
}

static int bo_end(BnOut *o)
{
    if (o->err) return -1;
    o->buf[o->len] = '\0';
    return o->len;
}

static int bn_iter_line(char *buf, int cap, int iter, u32 ticks, u32 pool)
{
    BnOut o;
    bo_init(&o, buf, cap);
    bo_str(&o, "iter ");
    bo_u32(&o, (u32)iter);
    bo_str(&o, " ticks ");
    bo_u32(&o, ticks);
    bo_str(&o, " pool ");
    bo_u32(&o, pool);
    bo_str(&o, " B\n");
    return bo_end(&o);
}

static int bn_summary(char *buf, int cap, const BnStats *s, int status)
{
    BnOut o;
    bo_init(&o, buf, cap);
    bo_str(&o, "ticks min ");
    bo_u32(&o, s->t_min);
    bo_str(&o, " max ");
    bo_u32(&o, s->t_max);
    bo_str(&o, " avg ");
    bo_u32(&o, bn_avg(s));
    bo_str(&o, " total ");
    bo_u32(&o, s->t_sum);
    bo_str(&o, " n ");
    bo_u32(&o, s->n);
    bo_str(&o, "\npool start ");
    bo_u32(&o, s->pool_start);
    bo_str(&o, " peak ");
    bo_u32(&o, s->pool_peak);
    bo_str(&o, " end ");
    bo_u32(&o, s->pool_end);
    bo_str(&o, " B\nstatus ");
    bo_u32(&o, (u32)status);
    bo_str(&o, " ");
    bo_str(&o, bn_status_name(status));
    bo_str(&o, "\nfailures ");
    bo_u32(&o, s->failures);
    bo_str(&o, "\n");
    /* 飽和したときだけ 1 行足す (普段の書式は変えない)。 */
    if (s->saturated) bo_str(&o, "saturated 1\n");
    return bo_end(&o);
}

/* ======================================================================== */
/*  出力 (すべて close の後)                                                 */
/* ======================================================================== */

static int bn_slen(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

/* short write を「書けた」ことにしない。失敗は bn_out_err で終了コードへ。
 * 1KB ごとに sys_yield を挟む — GUI 端末の con_sink は 8KB の環で満杯に
 * なると古い行から捨てるので、端末アプリに読み出す機会を与える必要がある
 * (レビュー往復 1 の B4)。**呼ぶのは必ず cfg_close の後**。 */
static int bn_emit(const char *p, int n)
{
    int done = 0, rc, chunk;
    if (!bn_api || n < 0) { bn_out_err = 1; return -1; }
    while (done < n) {
        chunk = n - done;
        if (chunk > BN_YIELD_CHUNK) chunk = BN_YIELD_CHUNK;
        rc = bn_api->sys_write(1, p + done, (u32)chunk);
        if (rc <= 0) { bn_out_err = 1; return -1; }
        done += rc;
        bn_pending += (u32)rc;
        if (bn_pending >= BN_YIELD_CHUNK) {
            bn_api->sys_yield();
            bn_pending = 0;
        }
    }
    return 0;
}

/* 端が 1KB に満たない分を吐き切らせる。 */
static void bn_drain(void)
{
    if (bn_api && bn_pending > 0) {
        bn_api->sys_yield();
        bn_pending = 0;
    }
}

static void bn_puts(const char *s)
{
    bn_emit(s, bn_slen(s));
}

static void bn_usage(void)
{
    bn_puts("usage: cfg_bench [n] [m]"
            "   read: n x (open -> m gets -> close)\n");
    bn_puts("       cfg_bench -w [n]"
            "    write: n x (open -> begin -> set -> commit -> close)\n");
}

static void bn_log_iter(int iter, u32 ticks, u32 pool)
{
    char line[BN_LINE_MAX];
    int n = bn_iter_line(line, (int)sizeof(line), iter, ticks, pool);
    if (n < 0) { bn_out_err = 1; return; }
    bn_emit(line, n);
}

static void bn_report(const BnStats *s, int status)
{
    char text[BN_SUM_MAX];
    int n = bn_summary(text, (int)sizeof(text), s, status);
    if (n < 0) { bn_out_err = 1; return; }
    bn_emit(text, n);
}

/* ======================================================================== */
/*  読み                                                                     */
/* ======================================================================== */

/* 1 件の get。戻りは cfg_* の生の戻り (負 = 失敗 / NOTFOUND)。
 * int の 2 本は cfg_get_int をその場に展開したもの — cfg_get_int は
 * cfg_read_int の 1 行のラッパ (userland/lib/cfg/libos32cfg.c:612) なので
 * 引く手間は同じで、「失敗」と「既定値で埋めた」を票の数え方どおりに
 * 見分けられる。 */
static int bn_one_get(CfgDb *db, int which, char *tbuf, int tcap)
{
    int rc, iv = 0;

    if (which == BN_PICK_WALL)
        return cfg_get_text(db, BN_SCOPE, BN_KEY_WALL, tbuf, tcap);

    rc = cfg_read_int(db, BN_SCOPE, bn_key_of(which), &iv);
    if (rc != 0) iv = 0;             /* cfg_get_int(..., 0) と同じ既定 */
    bn_sink ^= (u32)iv;              /* 符号なしで畳む (B1) */
    return rc;
}

static int bn_run_read(const BnArgs *a)
{
    static char tbuf[BN_TEXT_MAX];
    BnStats st;
    int i, j, status = CFG_MISSING;

    bn_reset(&st, bn_api->db_mem_used());

    for (i = 0; i < a->n; i++) {
        CfgDb *db = (CfgDb *)0;
        u32 t0, t1, pool_open, pool_closed, fails = 0;

        /* ---- 計測の窓ここから (この中で yield / 出力をしない) ---- */
        t0 = bn_api->get_tick();
        if (cfg_open(&db, 0) != 0) {
            fails++;
            pool_open = bn_api->db_mem_used();
        } else {
            for (j = 0; j < a->m; j++)
                fails += (u32)bn_get_failed(bn_one_get(db, bn_pick(j), tbuf,
                                                       (int)sizeof(tbuf)));
            /* 状態は**操作を終えてから** close の前に採る。open 直後だと
             * get の途中で CFG_ERROR へ遷移しても `status 0 OK` を出して
             * しまう (レビュー往復 1 の B2)。 */
            status = cfg_status(db);
            if (status != CFG_OK) fails++;
            pool_open = bn_api->db_mem_used();
            if (cfg_close(db) != 0) fails++;
        }
        t1 = bn_api->get_tick();
        pool_closed = bn_api->db_mem_used();
        /* ---- 計測の窓ここまで。以降は接続を持っていない ---- */

        bn_sample(&st, t1 - t0, pool_open, pool_closed);
        st.failures = bn_add_sat(st.failures, fails, &st.saturated);
        if (bn_should_log(i, a->n)) bn_log_iter(i + 1, t1 - t0, pool_open);
    }

    bn_report(&st, status);
    return bn_bad(&st);
}

/* ======================================================================== */
/*  書き                                                                     */
/* ======================================================================== */

static int bn_run_write(const BnArgs *a)
{
    BnStats st;
    int i, status = CFG_MISSING;

    bn_reset(&st, bn_api->db_mem_used());

    for (i = 0; i < a->n; i++) {
        CfgDb *db = (CfgDb *)0;
        u32 t0, t1, pool_open, pool_closed, fails = 0;

        /* ---- 計測の窓ここから (この中で yield / 出力をしない) ---- */
        t0 = bn_api->get_tick();
        if (cfg_open(&db, 1) != 0) {
            fails++;
            pool_open = bn_api->db_mem_used();
        } else {
            if (cfg_begin(db) != 0) {
                fails++;
            } else {
                if (cfg_set_int(db, BN_WSCOPE, BN_WKEY, i) != 0) fails++;
                if (cfg_commit(db) != 0) fails++;
            }
            status = cfg_status(db);        /* 操作の後・close の前 (B2) */
            if (status != CFG_OK) fails++;
            pool_open = bn_api->db_mem_used();
            if (cfg_close(db) != 0) fails++;
        }
        t1 = bn_api->get_tick();
        pool_closed = bn_api->db_mem_used();
        /* ---- 計測の窓ここまで ---- */

        bn_sample(&st, t1 - t0, pool_open, pool_closed);
        st.failures = bn_add_sat(st.failures, fails, &st.saturated);
        if (bn_should_log(i, a->n)) bn_log_iter(i + 1, t1 - t0, pool_open);
    }

    bn_report(&st, status);
    return bn_bad(&st);
}
