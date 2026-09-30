/* ======================================================================== */
/*  IRQ_MATH_HOST.C — kernel/irq_math.c をそのままホストで回す              */
/*                                                                          */
/*  実物を 1 行も写さずに #include する。irq_math.c は PIC も IDT も         */
/*  tick_count も触らないので、模型は 1 つも要らない — ハンドラだけが        */
/*  「筋書きどおりの値を返す偽装置」になる。                                */
/*                                                                          */
/*  見るのは **NP21/W でも実機でも狙って作れない**分岐 (記録: irq_math_tdd.md):
 *    (a) A と B が**同時に**要因を持つ (走査を最初の HANDLED で打ち切らない)
 *    (b) 1 巡目で受けて 2 巡目が空 (handled_any を 0 で上書きしない)
 *    (c) 1 tick に 200 回と 201 回 (ストームの閾値)
 *    (d) IRQ15 のスレーブ ISR bit7 が落ちている (スプリアス)
 *    (e) 登録の拒否規則 (固定 IRQ / 範囲外 / 重複 / SHARED 不一致 / 5 件目 /
 *        隔離済み / ISR 文脈)
 * ======================================================================== */
#include <stdio.h>
#include <string.h>
#include "../../kernel/irq_math.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* ------------------------------------------------------------------ */
/*  偽装置: 呼ばれた順を記録し、筋書きどおりの値を返す                 */
/* ------------------------------------------------------------------ */
#define LOG_MAX 32
static int  g_log[LOG_MAX];
static int  g_log_n;
static unsigned int g_last_irq;

struct fake {
    int id;
    int calls;
    int ret[4];      /* 呼ばれた回ごとの戻り値 (4 回目以降は ret[3]) */
};

static struct fake g_dev[5];

static int fake_handler(unsigned int irq, void *arg)
{
    struct fake *d = (struct fake *)arg;
    int rc = d->ret[(d->calls < 4) ? d->calls : 3];
    g_last_irq = irq;
    if (g_log_n < LOG_MAX) g_log[g_log_n++] = d->id;
    d->calls++;
    return rc;
}

static int other_handler(unsigned int irq, void *arg)
{
    (void)irq; (void)arg;
    return IRQ_NONE;
}

static void reset_devs(void)
{
    int i, k;
    memset(g_dev, 0, sizeof(g_dev));
    for (i = 0; i < 5; i++) {
        g_dev[i].id = i;
        for (k = 0; k < 4; k++) g_dev[i].ret[k] = IRQ_NONE;
    }
    g_log_n = 0;
    g_last_irq = 0xFFFFFFFFu;
}

/* 表に直接積む (irq.c の irq_register と同じ並べ方)。 */
static void put(struct irq_line *ln, int dev, unsigned int flags)
{
    int i = ln->count;
    ln->slot[i].arg   = &g_dev[dev];
    ln->slot[i].flags = flags;
    ln->slot[i].fn    = fake_handler;
    ln->count = (unsigned char)(i + 1);
}

/* ------------------------------------------------------------------ */
/*  (a)(b) 集約と 2 巡                                                 */
/* ------------------------------------------------------------------ */
static void dispatch_two_pass(void)
{
    struct irq_line ln;
    int any;

    /* --- 誰も受けない: 1 巡だけ、handled_any = 0 --- */
    memset(&ln, 0, sizeof(ln));
    reset_devs();
    put(&ln, 0, IRQ_F_SHARED);
    put(&ln, 1, IRQ_F_SHARED);
    any = irq_dispatch_line(&ln, 3);
    CHECK(any == 0);
    CHECK(g_log_n == 2);                 /* A, B の 1 巡だけ */
    CHECK(g_log[0] == 0 && g_log[1] == 1);
    CHECK(g_last_irq == 3);              /* irq 番号がそのまま渡る */
    CHECK(ln.shared_dispatch == 1);
    CHECK(ln.deferred_count == 0);

    /* --- **A と B が同時に要因を持つ**: 最初の HANDLED で打ち切らない ---
     * 打ち切ると B の要因が残り、共有線は上がったまま。エッジの 8259 は
     * 次を出さないので、その線は二度と上がらない。 */
    memset(&ln, 0, sizeof(ln));
    reset_devs();
    g_dev[0].ret[0] = IRQ_HANDLED;       /* A: 1 巡目で受ける */
    g_dev[1].ret[0] = IRQ_HANDLED;       /* B: 同じ巡で受ける */
    put(&ln, 0, IRQ_F_SHARED);
    put(&ln, 1, IRQ_F_SHARED);
    any = irq_dispatch_line(&ln, 5);
    CHECK(any == 1);
    CHECK(g_dev[1].calls >= 1);          /* **B が呼ばれている** */
    CHECK(g_log_n == 4);                 /* A B A B (受けたので 2 巡目) */
    CHECK(g_log[0] == 0 && g_log[1] == 1 && g_log[2] == 0 && g_log[3] == 1);

    /* --- 1 巡目で受けて 2 巡目は空: handled_any は 1 のまま --- */
    memset(&ln, 0, sizeof(ln));
    reset_devs();
    g_dev[0].ret[0] = IRQ_HANDLED;       /* 1 回目だけ受け、2 回目は NONE */
    put(&ln, 0, IRQ_F_SHARED);
    put(&ln, 1, IRQ_F_SHARED);
    any = irq_dispatch_line(&ln, 9);
    CHECK(any == 1);                     /* **2 巡目の 0 で上書きしない** */
    CHECK(g_log_n == 4);
    CHECK(g_dev[0].calls == 2);          /* 2 巡ちょうど。3 巡目は無い */

    /* --- 2 巡目で別の装置に要因が積まれた (回収できる) --- */
    memset(&ln, 0, sizeof(ln));
    reset_devs();
    g_dev[0].ret[0] = IRQ_HANDLED;
    g_dev[1].ret[1] = IRQ_HANDLED;       /* B は **2 回目の呼び出し**で受ける */
    put(&ln, 0, IRQ_F_SHARED);
    put(&ln, 1, IRQ_F_SHARED);
    any = irq_dispatch_line(&ln, 9);
    CHECK(any == 1);
    CHECK(g_dev[1].calls == 2);
    CHECK(g_log_n == 4);                 /* **3 巡目はしない** (有界) */

    /* --- DEFERRED は handled に数え、回数を記録する --- */
    memset(&ln, 0, sizeof(ln));
    reset_devs();
    g_dev[0].ret[0] = IRQ_DEFERRED;
    g_dev[0].ret[1] = IRQ_DEFERRED;
    put(&ln, 0, IRQ_F_SHARED);
    any = irq_dispatch_line(&ln, 3);
    CHECK(any == 1);                     /* 自分の要因だったので handled */
    CHECK(ln.deferred_count == 2);
    CHECK(ln.shared_dispatch == 0);      /* 1 登録だけなら共有ではない */

    /* --- 登録 0 件でも落ちない (誰も受けない = EOI だけ) --- */
    memset(&ln, 0, sizeof(ln));
    reset_devs();
    CHECK(irq_dispatch_line(&ln, 14) == 0);
    CHECK(irq_dispatch_line(0, 14) == 0);
}

/* ------------------------------------------------------------------ */
/*  (e) 登録の拒否規則                                                 */
/* ------------------------------------------------------------------ */
static void register_rules(void)
{
    struct irq_line ln;

    /* 動的な線だけ。固定スタブの線は -NOTSUP。 */
    CHECK(irq_dyn_index(3) == 0);
    CHECK(irq_dyn_index(15) == IRQ_DYN_COUNT - 1);
    CHECK(irq_dyn_index(0) < 0);         /* タイマ */
    CHECK(irq_dyn_index(1) < 0);         /* KBD */
    CHECK(irq_dyn_index(2) < 0);         /* VSYNC (V86) */
    CHECK(irq_dyn_index(4) < 0);         /* シリアル */
    CHECK(irq_dyn_index(7) < 0);         /* スプリアス */
    CHECK(irq_dyn_index(11) < 0);        /* FDC */
    CHECK(irq_dyn_index(12) < 0);        /* サウンド (V86) */
    CHECK(irq_dyn_index(13) < 0);        /* マウス */
    CHECK(irq_dyn_index(16) < 0);
    CHECK(irq_dyn_index(0xFF) < 0);      /* PCI の未割り当て */

    memset(&ln, 0, sizeof(ln));
    reset_devs();

    /* ISR 文脈からは何であれ断る (契約違反)。 */
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[0], IRQ_F_SHARED, 1)
          == IRQ_ERR_CTX);
    /* 固定 IRQ / 範囲外 */
    CHECK(irq_register_check(0, 0, fake_handler, &g_dev[0], 0, 0) == IRQ_ERR_NOTSUP);
    CHECK(irq_register_check(11, 0, fake_handler, &g_dev[0], 0, 0) == IRQ_ERR_NOTSUP);
    CHECK(irq_register_check(16, 0, fake_handler, &g_dev[0], 0, 0) == IRQ_ERR_INVAL);
    CHECK(irq_register_check(0xFF, 0, fake_handler, &g_dev[0], 0, 0) == IRQ_ERR_INVAL);
    /* NULL の fn / 知らない flags */
    CHECK(irq_register_check(3, &ln, 0, &g_dev[0], 0, 0) == IRQ_ERR_INVAL);
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[0], 0x80, 0) == IRQ_ERR_INVAL);
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[0],
                             IRQ_F_SHARED | 0x02, 0) == IRQ_ERR_INVAL);

    /* 1 件目は排他でも共有でも受ける */
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[0], 0, 0) == 0);
    put(&ln, 0, 0);                      /* **排他で** 1 件入れる */

    /* 同じ {fn, arg} の重複 */
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[0], 0, 0) == IRQ_ERR_EXIST);
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[0], IRQ_F_SHARED, 0)
          == IRQ_ERR_EXIST);
    /* 同じ fn でも arg が違えば別物 */
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[1], IRQ_F_SHARED, 0)
          == IRQ_ERR_SHARE);            /* **既存が排他**なので受けない */
    CHECK(irq_register_check(3, &ln, other_handler, &g_dev[0], IRQ_F_SHARED, 0)
          == IRQ_ERR_SHARE);

    /* 既存が共有・新規が排他でも受けない (**両側が SHARED のときだけ**) */
    memset(&ln, 0, sizeof(ln));
    put(&ln, 0, IRQ_F_SHARED);
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[1], 0, 0) == IRQ_ERR_SHARE);
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[1], IRQ_F_SHARED, 0) == 1);

    /* 4 件まで。5 件目は満杯 */
    put(&ln, 1, IRQ_F_SHARED);
    put(&ln, 2, IRQ_F_SHARED);
    put(&ln, 3, IRQ_F_SHARED);
    CHECK(ln.count == IRQ_MAX_HANDLERS);
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[4], IRQ_F_SHARED, 0)
          == IRQ_ERR_FULL);

    /* 隔離済みの線は満杯より先に断る */
    ln.quarantined = 1;
    CHECK(irq_register_check(3, &ln, fake_handler, &g_dev[4], IRQ_F_SHARED, 0)
          == IRQ_ERR_BUSY);
    ln.quarantined = 0;

    /* --- 解除 --- */
    CHECK(irq_unregister_find(3, &ln, fake_handler, &g_dev[2], 0) == 2);
    CHECK(irq_unregister_find(3, &ln, fake_handler, &g_dev[4], 0) == IRQ_ERR_NOENT);
    CHECK(irq_unregister_find(3, &ln, fake_handler, &g_dev[0], 1) == IRQ_ERR_CTX);
    CHECK(irq_unregister_find(0, 0, fake_handler, &g_dev[0], 0) == IRQ_ERR_NOTSUP);

    /* 抜いたら前へ詰める (穴を作らない = 走査が止まらない) */
    irq_slot_remove(&ln, 1);
    CHECK(ln.count == 3);
    CHECK(ln.slot[0].arg == &g_dev[0]);
    CHECK(ln.slot[1].arg == &g_dev[2]);
    CHECK(ln.slot[2].arg == &g_dev[3]);
    CHECK(ln.slot[3].fn == 0);

    /* 抜けた後は呼ばれない */
    reset_devs();
    (void)irq_dispatch_line(&ln, 3);
    CHECK(g_dev[1].calls == 0);
}

/* ------------------------------------------------------------------ */
/*  (c) ストームの閾値                                                 */
/* ------------------------------------------------------------------ */
static void storm(void)
{
    struct irq_line ln;
    int i, masked;

    /* 200 回では入らない */
    memset(&ln, 0, sizeof(ln));
    masked = 0;
    for (i = 0; i < IRQ_STORM_LIMIT; i++) masked |= irq_storm_step(&ln, 7, 0);
    CHECK(masked == 0);
    CHECK(ln.tick_hits == (unsigned int)IRQ_STORM_LIMIT);

    /* 201 回目で入る */
    CHECK(irq_storm_step(&ln, 7, 0) == 1);
    ln.storm_masked = 1;                 /* irq.c が立てるのと同じ */
    /* 一度立てたら「いまマスクすべき」は 2 度返さない (報告は 1 回) */
    CHECK(irq_storm_step(&ln, 7, 0) == 0);

    /* tick が変われば窓を作り直す */
    memset(&ln, 0, sizeof(ln));
    for (i = 0; i < IRQ_STORM_LIMIT; i++) CHECK(irq_storm_step(&ln, 7, 0) == 0);
    for (i = 0; i < IRQ_STORM_LIMIT; i++) CHECK(irq_storm_step(&ln, 8, 0) == 0);
    CHECK(ln.tick_hits == (unsigned int)IRQ_STORM_LIMIT);

    /* **誰かが受けた回ではマスクしない** (正常に忙しい装置を殺さない) */
    memset(&ln, 0, sizeof(ln));
    for (i = 0; i < IRQ_STORM_LIMIT + 50; i++) {
        CHECK(irq_storm_step(&ln, 7, 1) == 0);
    }
    /* 受けた回も数には入る。次に 1 回でも受けなければ入る (票の字義)。 */
    CHECK(irq_storm_step(&ln, 7, 0) == 1);

    CHECK(irq_storm_step(0, 7, 0) == 0);
}

/* ------------------------------------------------------------------ */
/*  (d) EOI の送信列                                                   */
/* ------------------------------------------------------------------ */
static void eoi_plan(void)
{
    /* マスタ側 */
    CHECK(irq_eoi_plan(3, 0) == IRQ_EOI_MASTER);
    CHECK(irq_eoi_plan(5, 1) == IRQ_EOI_MASTER);
    CHECK(irq_eoi_plan(6, 0) == IRQ_EOI_MASTER);
    /* スレーブ側 (カスケードでマスタにも) */
    CHECK(irq_eoi_plan(8, 0) == IRQ_EOI_SLAVE_MASTER);
    CHECK(irq_eoi_plan(9, 0) == IRQ_EOI_SLAVE_MASTER);
    CHECK(irq_eoi_plan(10, 0) == IRQ_EOI_SLAVE_MASTER);
    CHECK(irq_eoi_plan(14, 0) == IRQ_EOI_SLAVE_MASTER);

    /* **IRQ15 のスプリアス**: スレーブの ISR で IR7 が落ちていたら
     * スレーブへ EOI を送ってはいけない (送ると本物の IR7 を潰す)。 */
    CHECK(irq_eoi_plan(15, 0) == IRQ_EOI_MASTER);
    CHECK(irq_eoi_plan(15, 1) == IRQ_EOI_SLAVE_MASTER);

    /* 範囲外は何もしない */
    CHECK(irq_eoi_plan(16, 1) == IRQ_EOI_NONE);
    CHECK(irq_eoi_plan(0xFF, 0) == IRQ_EOI_NONE);
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    const char *c = (argc > 1) ? argv[1] : "";

    failed = 0;
    if (!strcmp(c, "dispatch_two_pass")) dispatch_two_pass();
    else if (!strcmp(c, "register_rules")) register_rules();
    else if (!strcmp(c, "storm")) storm();
    else if (!strcmp(c, "eoi_plan")) eoi_plan();
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return failed ? 1 : 0;
}
