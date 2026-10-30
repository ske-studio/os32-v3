/* ------------------------------------------------------------------------ */
/*  W3: 動的 IRQ の登録・ディスパッチ (票 TASK_HAL_WIRING §1-1)             */
/*                                                                          */
/*  **PIC のエッジを使わずに経路を通す**: IDT ゲート 0x23 (IRQ3) を          */
/*  `int $0x23` で直に叩く。共通スタブ → irq_dispatch → 表 → irq_finish     */
/*  まで本物が走り、最後の EOI は「ISR ビットが 1 つも立っていない 8259 への */
/*  非特定 EOI」= 無動作になる (CPL=0 の通常コードでは ISR は空)。           */
/*  実 IRQ での確認 (master 3 / slave 9、/api/pic で IRR・ISR を見る) は      */
/*  W3 の NP21/W 側が持つ。                                                  */
/*                                                                          */
/*  判定の網羅そのものはホスト試験 (tools/tests/test_irq_math.py) にある。    */
/*  ここで見るのは「配線が本当に通っているか」— 表・PIC のマスク・スタブ。   */
/* ------------------------------------------------------------------------ */
#define KSEL_IRQ_LINE   3
#define KSEL_IRQ_VECTOR "$0x23"

struct ksel_fake_dev {
    int id;
    int calls;
    int ret[4];
    int try_register_rc;   /* ISR 文脈からの登録を試した結果 (0 = 試していない) */
    int try_register;
};

static struct ksel_fake_dev ksel_dev[5];
static int ksel_irq_log[8];
static int ksel_irq_log_n;

static int ksel_fake_irq(unsigned int irq, void *arg)
{
    struct ksel_fake_dev *d = (struct ksel_fake_dev *)arg;
    int rc;

    (void)irq;
    rc = d->ret[(d->calls < 4) ? d->calls : 3];
    if (ksel_irq_log_n < 8) ksel_irq_log[ksel_irq_log_n++] = d->id;
    d->calls++;
    if (d->try_register) {
        d->try_register = 0;
        /* **ISR の中からの登録は契約違反** — 断られることを見る。 */
        d->try_register_rc = irq_register(5, ksel_fake_irq, &ksel_dev[4],
                                          IRQ_F_SHARED);
    }
    return rc;
}

static void ksel_raise_irq3(void)
{
    __asm__ __volatile__("int " KSEL_IRQ_VECTOR);
}

/* PIC のマスク (IMR) を読む。1 = マスク中。 */
static int ksel_irq_masked(unsigned int irq)
{
    u8 imr = (u8)inp((irq < 8) ? PIC1_DATA : PIC2_DATA);
    return (int)((imr >> (irq & 7)) & 1);
}

static void ksel_irq_reset_devs(void)
{
    int i, k;
    for (i = 0; i < 5; i++) {
        ksel_dev[i].id = i;
        ksel_dev[i].calls = 0;
        ksel_dev[i].try_register = 0;
        ksel_dev[i].try_register_rc = 0;
        for (k = 0; k < 4; k++) ksel_dev[i].ret[k] = IRQ_NONE;
    }
    ksel_irq_log_n = 0;
}

/* 試験の前後で見比べる IRQ3 の状態 (課題: 実機 Ra266 の 82557 が IRQ3)。 */
struct ksel_irq_snap {
    int  masked;        /* PIC の IMR (1 = マスク) */
    int  quarantined;   /* irq_line_quarantined のビット */
    int  storm;         /* irq_storm_masked のビット */
    int  count;         /* 登録数 */
    int  ln_q;          /* 表の quarantined / storm_masked */
    int  ln_s;
};

static void ksel_irq_snap_take(struct ksel_irq_snap *s)
{
    int idx = irq_dyn_index(KSEL_IRQ_LINE);
    s->masked      = ksel_irq_masked(KSEL_IRQ_LINE);
    s->quarantined = (irq_line_quarantined & (1u << KSEL_IRQ_LINE)) ? 1 : 0;
    s->storm       = (irq_storm_masked & (1u << KSEL_IRQ_LINE)) ? 1 : 0;
    s->count       = irq_lines[idx].count;
    s->ln_q        = irq_lines[idx].quarantined;
    s->ln_s        = irq_lines[idx].storm_masked;
}

static void test_irq_dynamic_body(int skip_unmask);

/* IMR を開ける部分 (test_irq_dynamic_body の skip_unmask より後) の check 数。
 * IRR に本物の要求が来ていて飛ばしたとき、飛ばした件数としてログに出す
 * (実機の合計が NP21/W より少なく見える理由を行で分かるように。レビュー
 * 往復 2)。**数え違いは本体の最後の "irq:unmask count" が検出する** —
 * 飛ばさなかった起動 (NP21/W) で、実際に走った check の数と突き合わせる。
 * 飛ばす件数は、その突き合わせの 1 件を足した値。 */
#define KSEL_IRQ_UNMASK_CHECKS   28
#define KSEL_IRQ_UNMASK_SKIPPED  (KSEL_IRQ_UNMASK_CHECKS + 1)

/* マスタ PIC の IRR (要求が来ているか) を OCW3 で読む。IMR に関係なく立つ。
 * 読んだ後は既定の IRR 読み出しのまま (irq_finish と同じ流儀)。 */
static int ksel_irq_pending(unsigned int irq)
{
    u8 irr;
    outp(PIC1_CMD, OCW3_IRR);
    irr = (u8)inp(PIC1_CMD);
    return (int)((irr >> (irq & 7)) & 1);
}

/* ------------------------------------------------------------------------ */
/*  IRQ3 は **本物の線** (実機 Ra266 では内蔵 LAN の 82557 が PCI Line 3 で   */
/*  上げたままにしている)。この試験は PIC のマスクを登録数で持つことを見る   */
/*  ので、途中で本当に IRQ3 の IMR を開ける。そこへ本物の IRQ3 が入ると:     */
/*    - 偽の登録者が IRQ_NONE で断り、呼ばれた回数の判定が揺れる。          */
/*    - 試験が終わった後の線の状態が、試験の前と同じだと言えなくなる。      */
/*  そこで:                                                                  */
/*    1. 試験の本体を **IF=0 で** 回す。`int $0x23` は IF に関係なく通るが、  */
/*       PIC からの本物の IRQ3 は CPU が受け取らない (IMR を開けていても)。  */
/*       本体の最後で登録数が 0 に戻り、irq_recount_locked が IMR を閉じて   */
/*       から IF を戻す。                                                    */
/*    2. 試験の前に線が「素」(未登録・未隔離・ストーム無し・マスク) でなけ   */
/*       れば **試験しない** — 本物の登録者の線を試験で上書きしない。        */
/*    3. 試験の後、線の状態 (IMR・隔離・ストーム・登録数) が試験の前と同じ   */
/*       ことを **ここで検査する**。誰も受けなかった数 (irq_unexpected) も   */
/*       試験の前の値へ戻す (実機の切り分けで kernel.map から読む数)。       */
/*    4. 試験がわざと起こすストーム・隔離・unclaimed の表示は irq_test_quiet */
/*       で黙らせる。起動画面に本物の障害と同じ文言で出ていたうえ、        */
/*       unclaimed の表示は線ごと 1 回きりなので、試験が IRQ3 の 1 回を使い   */
/*       切って本物の装置の unclaimed が出なくなっていた。                   */
/* ------------------------------------------------------------------------ */
static void test_irq_dynamic(void)
{
    struct ksel_irq_snap before, after;
    u32 unexpected_before;
    unsigned int saved;
    int pending;

    ksel_irq_snap_take(&before);
    if (before.count != 0 || before.quarantined || before.storm ||
        before.ln_q || before.ln_s || !before.masked) {
        kprintf(0xC1, "[selftest] irq: IRQ%d not pristine "
                "(cnt=%d q=%d storm=%d masked=%d) -> skipped\n",
                KSEL_IRQ_LINE, before.count, before.quarantined,
                before.storm, before.masked);
        check(0, "irq:line pristine b4 test");
        return;
    }

    saved = irq_save();
    /* **本物の要求が IRR に来ていたら IMR を開けない** (レビュー往復 1)。
     * IF=0 でも、要求の立った線の IMR を開けて閉じると 8259 が INT を上げて
     * 下ろすことになり、偽の割り込み (IR7 / IRQ15 のスプリアス) を招きうる。
     * それが 1 回きりの unclaimed 表示を食うのを避ける。この場合は IMR を
     * 開けない拒否規則だけを見て、残りは飛ばしたことをログに残す
     * (FAIL にはしない — 装置が線を上げているのは試験の失敗ではない)。
     * 実機 Ra266 では内蔵 LAN の 82557 が IRQ3 を上げたままのはず (未確認)。 */
    pending = ksel_irq_pending(KSEL_IRQ_LINE);
    unexpected_before = irq_unexpected;
    kdi_set_irq_test_quiet(1);
    test_irq_dynamic_body(pending);
    kdi_set_irq_test_quiet(0);
    kdi_set_irq_unexpected(unexpected_before);
    ksel_irq_snap_take(&after);
    irq_restore(saved);

    if (pending)
        kprintf(KLOG_ATTR, "[selftest] irq: IRQ%d pending in IRR -> unmask part "
                "skipped %d checks (not a failure)\n", KSEL_IRQ_LINE,
                (int)KSEL_IRQ_UNMASK_SKIPPED);

    check(after.masked == before.masked &&
          after.quarantined == before.quarantined &&
          after.storm == before.storm &&
          after.count == before.count &&
          after.ln_q == before.ln_q && after.ln_s == before.ln_s,
          "irq:line restored");
}

/* 本体は **IF=0 で** 呼ぶこと (上の注記)。skip_unmask が 1 なら、IMR を
 * 開けない拒否規則だけを見て戻る。 */
static void test_irq_dynamic_body(int skip_unmask)
{
    u32 ctx_before = irq_ctx_violations;
    u32 deferred_before;
    int unmask_base;
    int i;

    ksel_irq_reset_devs();

    /* --- 拒否規則 (配線の確認。網羅はホスト試験) --- */
    check(irq_register(0, ksel_fake_irq, &ksel_dev[0], 0) == IRQ_ERR_NOTSUP,
          "irq:reg0=NOTSUP");
    check(irq_register(12, ksel_fake_irq, &ksel_dev[0], 0) == IRQ_ERR_NOTSUP,
          "irq:reg12=NOTSUP");
    check(irq_register(16, ksel_fake_irq, &ksel_dev[0], 0) == IRQ_ERR_INVAL,
          "irq:reg16=INVAL");
    check(irq_register(KSEL_IRQ_LINE, 0, &ksel_dev[0], 0) == IRQ_ERR_INVAL,
          "irq:regNULL=INVAL");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0], 0x80)
          == IRQ_ERR_INVAL, "irq:regflag=INVAL");

    if (skip_unmask) return;   /* ここから先は IRQ3 の IMR を開ける */
    unmask_base = ksel_pass + ksel_fail;

    /* --- 登録数で PIC のマスクを持つ --- */
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 1, "irq:masked b4 reg");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0], IRQ_F_SHARED)
          == 0, "irq:reg1 ok");
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 0, "irq:reg1 unmasks");

    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0], IRQ_F_SHARED)
          == IRQ_ERR_EXIST, "irq:dup=EXIST");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1], 0)
          == IRQ_ERR_SHARE, "irq:excl=SHARE");
    /* 2〜4 本目は同じ「末尾に足す」1 つの振る舞い (深さの境目は次の
     * reg5=FULL が見る)。個別に落ちる経路が無いので 1 本にまとめる。 */
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1], IRQ_F_SHARED)
              == 0 &&
          irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[2], IRQ_F_SHARED)
              == 0 &&
          irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[3], IRQ_F_SHARED)
              == 0, "irq:reg2-4 ok");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[4], IRQ_F_SHARED)
          == IRQ_ERR_FULL, "irq:reg5=FULL");

    /* --- 走査の順と 2 巡 (`int 0x23` で共通スタブを通す) --- */
    ksel_irq_reset_devs();
    ksel_dev[0].ret[0] = IRQ_HANDLED;    /* A は 1 巡目で受け、2 巡目は NONE */
    ksel_dev[3].try_register = 1;        /* D が ISR 文脈から登録を試す */
    ksel_raise_irq3();
    check(ksel_dev[0].calls == 2 && ksel_dev[1].calls == 2 &&
          ksel_dev[2].calls == 2 && ksel_dev[3].calls == 2,
          "irq:4 devs x2 passes");
    check(ksel_irq_log_n == 8 && ksel_irq_log[0] == 0 && ksel_irq_log[1] == 1 &&
          ksel_irq_log[2] == 2 && ksel_irq_log[3] == 3 && ksel_irq_log[4] == 0,
          "irq:order 0123 x2");
    check(irq_shared_dispatch(KSEL_IRQ_LINE) > 0, "irq:shared cnt");
    /* **ISR の中からの登録は断る** (契約、debug で数える) */
    check(ksel_dev[3].try_register_rc == IRQ_ERR_CTX,
          "irq:isr reg=CTX");
    check(irq_ctx_violations == ctx_before + 1, "irq:ctx cnt");

    /* --- DEFERRED を数える --- */
    deferred_before = irq_deferred_count(KSEL_IRQ_LINE);
    ksel_irq_reset_devs();
    ksel_dev[1].ret[0] = IRQ_DEFERRED;
    ksel_dev[1].ret[1] = IRQ_DEFERRED;
    ksel_raise_irq3();
    check(irq_deferred_count(KSEL_IRQ_LINE) == deferred_before + 2,
          "irq:deferred cnt");

    /* --- 解除: 残った側は動き、抜けた側は呼ばれない --- */
    check(irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1]) == 0,
          "irq:unreg ok");
    check(irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[1])
          == IRQ_ERR_NOENT, "irq:unreg2=NOENT");
    ksel_irq_reset_devs();
    ksel_raise_irq3();
    check(ksel_dev[1].calls == 0, "irq:unreg silent");
    check(ksel_dev[0].calls > 0 && ksel_dev[2].calls > 0,
          "irq:rest still run");
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 0, "irq:still unmasked");

    /* --- ストーム: 本体は IF=0 なので tick_count が止まり、窓は 1 つ --- */
    /* **撃つ前に窓を空にすること。** `irq_storm_step` は tick ごとに数え直す
     * ので、ここまでの解除の試験で撃った「誰も受けない `int 0x23`」が同じ
     * tick に入っていると、その数だけ下駄を履いて 200 本目で閾値を越える
     * (2026-09-23 に NP21/W で FAIL した形)。以前は IF=1 で tick の頭を
     * 待っていたが、その間は IRQ3 の IMR が開いていて本物の IRQ3 を受けて
     * しまう (実機 Ra266 の 82557)。irq_test_reset_line (試験専用) は
     * tick_hits を 0 にするので、それで窓を作り直す。登録は変わらない。 */
    irq_test_reset_line(KSEL_IRQ_LINE);
    ksel_irq_reset_devs();                      /* 全員 IRQ_NONE = 誰も受けない */
    for (i = 0; i < IRQ_STORM_LIMIT; i++) ksel_raise_irq3();
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 0 &&
          (irq_storm_masked & (1u << KSEL_IRQ_LINE)) == 0,
          "irq:200/tick no mask");
    irq_test_reset_line(KSEL_IRQ_LINE);         /* 窓を作り直す (試験専用) */
    for (i = 0; i <= IRQ_STORM_LIMIT; i++) ksel_raise_irq3();
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 1 &&
          (irq_storm_masked & (1u << KSEL_IRQ_LINE)) != 0,
          "irq:201/tick masks+bit");

    /* --- 隔離: sticky で、登録の再計算では解けない --- */
    check(irq_quarantine_line(0xFF) == IRQ_ERR_INVAL,
          "irq:qFF=INVAL");
    check(irq_quarantine_line(0) == IRQ_ERR_INVAL,
          "irq:q0=INVAL");
    /* 返り値・診断ビット・PIC のマスクは irq_quarantine_line が**同じ
     * irq_save の中でまとめて**書く。片方だけ落ちる経路は無いので 1 本で見る。 */
    check(irq_quarantine_line(KSEL_IRQ_LINE) == 0 &&
          (irq_line_quarantined & (1u << KSEL_IRQ_LINE)) != 0 &&
          ksel_irq_masked(KSEL_IRQ_LINE) == 1, "irq:q ok+bit+masked");
    check(irq_register(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[4], IRQ_F_SHARED)
          == IRQ_ERR_BUSY, "irq:q reg=BUSY");

    /* --- 後始末: 残りを外して線を素の状態へ戻す --- */
    (void)irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[0]);
    (void)irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[2]);
    (void)irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq, &ksel_dev[3]);
    check((irq_line_quarantined & (1u << KSEL_IRQ_LINE)) != 0,
          "irq:q vs recount");
    /* **試験専用の戻し** (kernel/irq/irq.h の注記)。これが無いと以後の起動で
     * IRQ3 が使えないままになる。 */
    irq_test_reset_line(KSEL_IRQ_LINE);
    check((irq_line_quarantined & (1u << KSEL_IRQ_LINE)) == 0,
          "irq:reset clears q");
    check(ksel_irq_masked(KSEL_IRQ_LINE) == 1,
          "irq:masked at end");
    check(irq_lines[0].count == 0, "irq:table empty");

    /* 飛ばしたときに出す件数 (KSEL_IRQ_UNMASK_SKIPPED) の元の数と一致するか */
    check(ksel_pass + ksel_fail - unmask_base == KSEL_IRQ_UNMASK_CHECKS,
          "irq:unmask count");
}
