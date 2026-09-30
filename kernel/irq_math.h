/* ======================================================================== */
/*  IRQ_MATH.H — 動的 IRQ 登録・ディスパッチの「決め事」だけ                */
/*                                                                          */
/*  PIC も IDT も触らない純粋な判断だけをここに置く。I/O を伴う実体は        */
/*  kernel/irq.c。分けてあるのは **ホストで全部の分岐を踏むため** —          */
/*  共有線の取りこぼし・2 巡の累積・ストームの閾値・IRQ15 のスプリアスは、   */
/*  どれも NP21/W では狙って作れない (実機でしか出ない機種差ではなく、       */
/*  「同時に要因が立つ」という**時間の重なり**なので、どちらでも作れない)。  */
/*    試験: tools/tests/test_irq_math.py                                    */
/*    記録: tools/tests/irq_math_tdd.md                                     */
/*    票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-1                           */
/* ======================================================================== */

#ifndef IRQ_MATH_H
#define IRQ_MATH_H

/* ---- ハンドラの戻り値 (票 §1-1) -------------------------------------- */
#define IRQ_NONE      0   /* 自分の要因ではない */
#define IRQ_HANDLED   1   /* 自分の要因を見つけて落とし、残件は無い */
#define IRQ_DEFERRED  2   /* 自分の要因だが残件がある (装置側はマスク済み) */

/* ---- 登録フラグ ------------------------------------------------------ */
#define IRQ_F_SHARED  0x01
#define IRQ_F_ALL     (IRQ_F_SHARED)   /* これ以外のビットは拒否する */

/* ---- 表の大きさ ------------------------------------------------------ */
/* 動的登録を受けるのは共通スタブ irq_stub_common_%1 に結んだ線だけ
 * (3/5/6/8/9/10/14/15)。固定スタブの線 (0/1/2/4/7/11/12/13) は拒否する。 */
#define IRQ_DYN_COUNT      8
#define IRQ_MAX_HANDLERS   4    /* 1 線あたりの登録上限 (票: 最大 4 登録) */
#define IRQ_LINE_MAX       16   /* 8259 ×2 */

/* 1 tick の間に「誰も受けない」ディスパッチがこれを**超えたら**線をマスクする。
 * 200 回ちょうどでは入らない (W3 が 200 と 201 を撃ち分ける)。 */
#define IRQ_STORM_LIMIT    200

/* ---- 失敗の返り値 ----------------------------------------------------- */
/* **OS32_ERR_* は使わない。** あちらは CPL=3 に見せる KAPI / VFS の ABI 空間
 * (os32_kapi_shared.h、番号は凍結) で、NOTSUP と BUSY を別々に表す番号を
 * 持っていない。この層はカーネル内部にしか出ないので独自に持つ。
 * `lspci` が出す `[irq N unsupported]` / `[irq N quarantined]` はこの 2 つ。 */
#define IRQ_ERR_INVAL   (-1)   /* irq >= 16 / fn == NULL / 知らない flags */
#define IRQ_ERR_NOTSUP  (-2)   /* 固定スタブの線 (動的登録を受けない) */
#define IRQ_ERR_BUSY    (-3)   /* 隔離済みの線 */
#define IRQ_ERR_EXIST   (-4)   /* 同じ {fn, arg} が既に居る */
#define IRQ_ERR_SHARE   (-5)   /* 既存と新規のどちらかが IRQ_F_SHARED でない */
#define IRQ_ERR_FULL    (-6)   /* 5 件目 */
#define IRQ_ERR_CTX     (-7)   /* ISR の中からの登録・解除 (契約違反) */
#define IRQ_ERR_NOENT   (-8)   /* 解除: その {fn, arg} は居ない */

/* ---- EOI の送り先 ----------------------------------------------------- */
#define IRQ_EOI_NONE          0   /* 送らない (範囲外) */
#define IRQ_EOI_MASTER        1   /* マスタだけ (irq < 8、および IRQ15 スプリアス) */
#define IRQ_EOI_SLAVE_MASTER  2   /* スレーブ → マスタ */

typedef int (*irq_handler_fn)(unsigned int irq, void *arg);

struct irq_slot {
    irq_handler_fn fn;
    void *arg;
    unsigned int flags;
};

/* 1 本の線の全部。**kernel/irq.c の実表がこの型そのもの** — ホスト試験用に
 * 別の模型を作らない (模型と実物がずれたら試験の意味が無い)。 */
struct irq_line {
    struct irq_slot slot[IRQ_MAX_HANDLERS];
    unsigned char count;          /* 登録数 */
    unsigned char quarantined;    /* 隔離済み (sticky。再計算では解けない) */
    unsigned char storm_masked;   /* ストームでマスク中 (再計算で解ける) */
    unsigned char pad;
    unsigned int  tick_hits;      /* いまの tick に入ったディスパッチ回数 */
    unsigned int  tick_stamp;     /* その tick の tick_count */
    unsigned int  deferred_count; /* IRQ_DEFERRED を返された回数 */
    unsigned int  shared_dispatch;/* 2 登録以上の線でのディスパッチ回数 */
};

/* irq → 動的表の添字 (0..IRQ_DYN_COUNT-1)。動的でない線と範囲外は負。
 * **シフトや PIC 操作の前にこれを通す** (PCI の未割り当て 0xFF 対策、票 1-6)。 */
int irq_dyn_index(unsigned int irq);

/* 登録を受けてよいか。受けるなら**入れる添字** (= ln->count) を返す。
 * 検査の順序はここで固定する (W1 が順序ごと見る):
 *   ISR 文脈 → 線の種別/範囲 → fn → flags → 隔離 → 重複 → 共有不一致 → 満杯
 * ln は irq が動的でなければ NULL でよい。 */
int irq_register_check(unsigned int irq, const struct irq_line *ln,
                       irq_handler_fn fn, void *arg, unsigned int flags,
                       int in_irq);

/* 解除してよいか。よければ**その添字**を返す。 */
int irq_unregister_find(unsigned int irq, const struct irq_line *ln,
                        irq_handler_fn fn, void *arg, int in_irq);

/* 表から 1 件抜いて前へ詰める (穴を作らない)。 */
void irq_slot_remove(struct irq_line *ln, int idx);

/* 全登録者を**2 巡ぶん**呼び、handled_any (0/1) を返す。
 *  - 最初の IRQ_HANDLED で打ち切らない (同時に要因を持つ相方を取りこぼすと
 *    共有線が上がったままになり、エッジの 8259 は次を出さない)
 *  - `any` は 2 巡全体の OR。**2 巡目の 0 で上書きしない**
 *  - 2 巡目は 1 巡目で誰かが受けたときだけ
 *  - ネストしない。IF=0 で呼ばれる前提。 */
int irq_dispatch_line(struct irq_line *ln, unsigned int irq);

/* ディスパッチ 1 回ぶんのストーム勘定。1 = いま線をマスクすべき。
 * now_tick が変われば窓を作り直す (tick をまたいだ回数は持ち越さない)。 */
int irq_storm_step(struct irq_line *ln, unsigned int now_tick, int handled_any);

/* EOI の送り先。slave_isr_bit7 は **irq == 15 のときだけ**意味を持ち、
 * handled_any とは**独立**に効く (スプリアスは誰も受けないとは限らない)。 */
int irq_eoi_plan(unsigned int irq, int slave_isr_bit7);

#endif /* IRQ_MATH_H */
