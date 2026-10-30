/* 実 IDT 0x23 → 共通スタブ → irq_dispatch → EOI と PIC IMR の接続だけ。
 * 登録規則・共有走査・storm・隔離は test_irq_math.py が実 irq.c で検査する。
 * ソフトウェア int は PIC の実エッジ/ISR を作らない。実 IRQ はゲスト受入で見る。 */
#define KSEL_IRQ_LINE 3
#define KSEL_IRQ_VECTOR "$0x23"

static volatile int ksel_irq_calls;
static volatile int ksel_irq_seen;

static int ksel_fake_irq(unsigned int irq, void *arg)
{
    if (irq == KSEL_IRQ_LINE && arg == (void *)&ksel_irq_calls &&
        kctx_irq_depth == 1)
        ksel_irq_seen++;
    return ++ksel_irq_calls == 1 ? IRQ_HANDLED : IRQ_NONE;
}

static void test_irq_dynamic(void)
{
    unsigned int saved = irq_save();
    int idx = irq_dyn_index(KSEL_IRQ_LINE);
    int rc;

    /* IRQ3 は実機の LAN も使う。使用中の線に触らず、実要求が来ている
     * ときも IMR を開けない (IF=0 でも偽 IRQ7 を誘発し得る)。 */
    if (irq_lines[idx].count || irq_lines[idx].quarantined ||
        irq_lines[idx].storm_masked ||
        ((irq_line_quarantined | irq_storm_masked) & (1u << KSEL_IRQ_LINE)) ||
        !(inp(PIC1_DATA) & (1u << KSEL_IRQ_LINE))) {
        irq_restore(saved);
        check(0, "irq:line pristine b4 test");
        return;
    }
    outp(PIC1_CMD, OCW3_IRR);
    if (inp(PIC1_CMD) & (1u << KSEL_IRQ_LINE)) {
        irq_restore(saved);
        kprintf(KLOG_ATTR, "[selftest] irq: IRQ3 pending in IRR -> wiring skipped\n");
        return;
    }

    ksel_irq_calls = ksel_irq_seen = 0;
    rc = irq_register(KSEL_IRQ_LINE, ksel_fake_irq,
                      (void *)&ksel_irq_calls, 0);
    check(rc == 0 && !(inp(PIC1_DATA) & (1u << KSEL_IRQ_LINE)),
          "irq:reg1 unmasks");
    if (rc == 0) {
        __asm__ __volatile__("int " KSEL_IRQ_VECTOR : : : "memory");
        check(ksel_irq_calls == 2 && ksel_irq_seen == 2 && kctx_irq_depth == 0,
              "irq:int23 dispatch");
        rc = irq_unregister(KSEL_IRQ_LINE, ksel_fake_irq,
                            (void *)&ksel_irq_calls);
    }
    check(rc == 0 && irq_lines[idx].count == 0 &&
          (inp(PIC1_DATA) & (1u << KSEL_IRQ_LINE)), "irq:masked at end");
    irq_restore(saved);
}
