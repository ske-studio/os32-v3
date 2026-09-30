/* ======================================================================== */
/*  IRQ.C — 動的 IRQ 登録の実体 (表 + PIC)                                  */
/*                                                                          */
/*  判断そのものは kernel/irq_math.c (I/O なし、ホストで全分岐を踏む)。      */
/*  ここがやるのは「表を irq_save で書く」「PIC のマスクを登録数で持つ」     */
/*  「EOI を 1 回だけ送る」の 3 つだけ。**残件の回収も再走査もしない** —     */
/*  それは装置ごとの driver が自分の tick フックで行う (票 §1-1 の契約)。    */
/*                                                                          */
/*  票: docs/tasks/v3/TASK_HAL_WIRING.md §1-1                               */
/* ======================================================================== */

#include "irq.h"
#include "idt.h"
#include "io.h"
#include "kprintf.h"

/* isr_handlers.c。EOI は送らない診断だけの関数 (票 §1-1 の 5)。 */
extern void isr_unexpected_report(u32 irq);

struct irq_line irq_lines[IRQ_DYN_COUNT];
u32 irq_unexpected = 0;
u32 irq_storm_masked = 0;
u32 irq_line_quarantined = 0;
u32 irq_ctx_violations = 0;
volatile int irq_in_irq = 0;
int irq_test_quiet = 0;   /* 試験専用 (irq.h の注記) */

/* ------------------------------------------------------------------------ */
/*  PIC のマスクは**登録数で持つ** (irq_save の中から呼ぶこと)               */
/*  隔離された線は登録数に関係なくマスクのまま。                             */
/* ------------------------------------------------------------------------ */
static void irq_recount_locked(unsigned int irq, struct irq_line *ln)
{
    if (ln->count > 0 && !ln->quarantined) {
        /* **成功した再計算のときだけ**ストームのマスクを解く (票 1-6)。
         * 隔離 (quarantined) はここでは解けない。 */
        ln->storm_masked = 0;
        irq_storm_masked &= ~(u32)(1u << irq);
        irq_enable(irq);
    } else {
        irq_disable(irq);
    }
}

int irq_register(unsigned int irq, irq_handler_fn fn, void *arg,
                 unsigned int flags)
{
    struct irq_line *ln;
    unsigned int saved;
    int idx, slot;

    idx = irq_dyn_index(irq);
    ln = (idx >= 0) ? &irq_lines[idx] : 0;

    saved = irq_save();
    slot = irq_register_check(irq, ln, fn, arg, flags, irq_in_irq);
    if (slot < 0) {
        if (slot == IRQ_ERR_CTX) irq_ctx_violations++;
        irq_restore(saved);
        return slot;
    }
    /* {fn, arg, flags} は一括で有効化する — **fn を最後に書く**。 */
    ln->slot[slot].arg   = arg;
    ln->slot[slot].flags = flags;
    ln->slot[slot].fn    = fn;
    ln->count = (unsigned char)(slot + 1);
    irq_recount_locked(irq, ln);
    irq_restore(saved);
    return 0;
}

int irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg)
{
    struct irq_line *ln;
    unsigned int saved;
    int idx, slot;

    idx = irq_dyn_index(irq);
    ln = (idx >= 0) ? &irq_lines[idx] : 0;

    saved = irq_save();
    slot = irq_unregister_find(irq, ln, fn, arg, irq_in_irq);
    if (slot < 0) {
        if (slot == IRQ_ERR_CTX) irq_ctx_violations++;
        irq_restore(saved);
        return slot;
    }
    /* IF=0 で表から外す。単一 CPU なので走行中の callback は無く、
     * ここを抜けた後にその callback が走ることはない (契約)。 */
    irq_slot_remove(ln, slot);
    irq_recount_locked(irq, ln);
    irq_restore(saved);
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  irq_finish — 動的経路で **pic_eoi を呼ぶ唯一の場所**                     */
/* ------------------------------------------------------------------------ */
void irq_finish(unsigned int irq, int handled_any)
{
    int bit7 = 0;
    int plan;

    if (irq == 15) {
        /* handled に関係なくスレーブの ISR を見る (票 §1-1 の 5)。 */
        u8 slave_isr;
        outp(PIC2_CMD, OCW3_ISR);
        slave_isr = (u8)inp(PIC2_CMD);
        outp(PIC2_CMD, OCW3_IRR);       /* 既定の IRR 読み出しへ戻す */
        bit7 = (slave_isr & 0x80) ? 1 : 0;
    }

    plan = irq_eoi_plan(irq, bit7);
    if (plan == IRQ_EOI_SLAVE_MASTER) {
        pic_eoi(irq);                   /* スレーブ → マスタ */
    } else if (plan == IRQ_EOI_MASTER) {
        outp(PIC1_CMD, OCW2_EOI);
    }

    if (!handled_any && !irq_test_quiet) isr_unexpected_report((u32)irq);
}

/* ------------------------------------------------------------------------ */
/*  irq_dispatch — 共通スタブの入口                                          */
/* ------------------------------------------------------------------------ */
void irq_dispatch(unsigned int irq)
{
    struct irq_line *ln;
    int idx;
    int handled = 0;

    irq_in_irq++;

    idx = irq_dyn_index(irq);
    if (idx >= 0) {
        ln = &irq_lines[idx];
        handled = irq_dispatch_line(ln, irq);
        if (irq_storm_step(ln, (unsigned int)tick_count, handled)) {
            ln->storm_masked = 1;
            irq_disable(irq);
            if (!(irq_storm_masked & (u32)(1u << irq))) {
                irq_storm_masked |= (u32)(1u << irq);
                if (!irq_test_quiet)
                    kprintf(0xC1, "[irq] storm on IRQ%d (>%d/tick unclaimed) -> masked\n",
                            (int)irq, (int)IRQ_STORM_LIMIT);
            }
        }
    }
    if (!handled) irq_unexpected++;

    irq_finish(irq, handled);
    irq_in_irq--;
}

/* ------------------------------------------------------------------------ */
/*  irq_quarantine_line — 線ごと打ち切る                                     */
/* ------------------------------------------------------------------------ */
int irq_quarantine_line(unsigned int irq)
{
    unsigned int saved;
    int idx;

    /* **範囲検査はシフトにも PIC にも入る前** (票 1-6。PCI の 0xFF 対策)。 */
    idx = irq_dyn_index(irq);
    if (idx < 0) return IRQ_ERR_INVAL;

    saved = irq_save();
    if (!irq_lines[idx].quarantined) {
        irq_lines[idx].quarantined = 1;
        irq_line_quarantined |= (u32)(1u << irq);
        irq_disable(irq);
        irq_restore(saved);
        if (!irq_test_quiet)
            kprintf(0xC1, "[irq] IRQ%d quarantined (%d registrant(s) fall back to their tick hook)\n",
                    (int)irq, (int)irq_lines[idx].count);
        return 0;
    }
    irq_disable(irq);
    irq_restore(saved);
    return 0;
}

u32 irq_deferred_count(unsigned int irq)
{
    int idx = irq_dyn_index(irq);
    return (idx >= 0) ? irq_lines[idx].deferred_count : 0;
}

u32 irq_shared_dispatch(unsigned int irq)
{
    int idx = irq_dyn_index(irq);
    return (idx >= 0) ? irq_lines[idx].shared_dispatch : 0;
}

/* **試験専用** (kernel/irq.h の注記を読むこと)。隔離とストームの試験の後に
 * 線を初期状態へ戻すためだけに在る。製品経路から呼んではいけない。 */
void irq_test_reset_line(unsigned int irq)
{
    unsigned int saved;
    int idx;

    idx = irq_dyn_index(irq);
    if (idx < 0) return;

    saved = irq_save();
    irq_lines[idx].quarantined = 0;
    irq_lines[idx].storm_masked = 0;
    irq_lines[idx].tick_hits = 0;
    irq_lines[idx].tick_stamp = 0;
    irq_storm_masked &= ~(u32)(1u << irq);
    irq_line_quarantined &= ~(u32)(1u << irq);
    irq_recount_locked(irq, &irq_lines[idx]);
    irq_restore(saved);
}
