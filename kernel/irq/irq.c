/* ======================================================================== */
/*  IRQ.C — 動的 IRQ 登録の実体 (表 + PIC)                                  */
/*                                                                          */
/*  判断そのものは kernel/irq/irq_math.c (I/O なし、ホストで全分岐を踏む)。      */
/*  ここがやるのは「表を irq_save で書く」「PIC のマスクを登録数で持つ」     */
/*  「EOI を 1 回だけ送る」の 3 つだけ。**残件の回収も再走査もしない** —     */
/*  それは装置ごとの driver が自分の tick フックで行う (票 §1-1 の契約)。    */
/*                                                                          */
/*  票: docs/tasks/v3/TASK_HAL_WIRING.md §1-1                               */
/* ======================================================================== */

#include "irq.h"
#include "kernel_tick.h"
#include "../../include/platform_irq.h"
#include "io.h"
#include "kprintf.h"
#ifdef PCM_E4_DIAG
#include "pcm_e4_diag.h"
#endif

/* isr_handlers.c。EOI は送らない診断だけの関数 (票 §1-1 の 5)。 */
extern void isr_unexpected_report(u32 irq);

struct irq_line irq_lines[IRQ_DYN_COUNT];
u32 irq_unexpected = 0;
u32 irq_storm_masked = 0;
u32 irq_line_quarantined = 0;
u32 irq_ctx_violations = 0;
int irq_test_quiet = 0;   /* 試験専用 (irq.h の注記) */

/* Legacy diagnostics project only the low 32 lines. Per-line state is authoritative. */
static u32 irq_projection(unsigned irq)
{
    return irq < 32 ? (u32)(1u << irq) : 0;
}

void irq_topology_check(void)
{
    const struct irq_topology *t = platform_irq_topology();
    unsigned i, j;
    int valid = t->lines == PLATFORM_IRQ_LINES &&
                t->dynamic_count == PLATFORM_IRQ_DYNAMIC_COUNT && t->dynamic_lines;
    if (valid) for (i = 0; i < t->dynamic_count; i++) {
        if (t->dynamic_lines[i] >= t->lines || platform_irq_reserved(t->dynamic_lines[i]))
            valid = 0;
        for (j = 0; j < i; j++)
            if (t->dynamic_lines[j] == t->dynamic_lines[i]) valid = 0;
    }
    if (!valid) {
        kprintf(ATTR_YELLOW, "[irq] invalid topology\n");
        for (;;) _stop();
    }
}

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
        irq_storm_masked &= ~irq_projection(irq);
        platform_irq_unmask(irq);
    } else {
        platform_irq_mask(irq);
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
    slot = irq_register_check(irq, ln, fn, arg, flags, kctx_irq_depth);
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
    slot = irq_unregister_find(irq, ln, fn, arg, kctx_irq_depth);
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
/*  irq_finish — dispatch と排他的に呼ぶ互換完了口                          */
/* ------------------------------------------------------------------------ */
void irq_finish(unsigned int irq, int handled_any)
{
    struct irq_claim claim;
    if (platform_irq_claim(irq, &claim) < 0) return;
    if (claim.needs_finish) platform_irq_complete(&claim);

    if (!handled_any && !irq_test_quiet) isr_unexpected_report((u32)irq);
}

/* ------------------------------------------------------------------------ */
/*  irq_dispatch — 共通スタブの入口                                          */
/* ------------------------------------------------------------------------ */
void irq_dispatch(unsigned int irq)
{
    struct irq_claim claim;
    struct irq_line *ln;
    int idx;
    int handled = 0;

    if (platform_irq_claim(irq, &claim) < 0) return;
    if (!claim.deliver) {
        if (claim.needs_finish) platform_irq_complete(&claim);
        return;
    }
    irq = claim.line;
    idx = irq_dyn_index(irq);
    if (idx >= 0) {
        ln = &irq_lines[idx];
#ifdef PCM_E4_DIAG
        pcm_e4_dispatch_enter(irq);
#endif
        handled = irq_dispatch_line(ln, irq);
#ifdef PCM_E4_DIAG
        pcm_e4_dispatch_leave(irq);
#endif
        if (irq_storm_step(ln, (unsigned int)tick_count, handled)) {
            platform_irq_mask(irq);
            if (!ln->storm_masked) {
                ln->storm_masked = 1;
                irq_storm_masked |= irq_projection(irq);
                if (!irq_test_quiet)
                    kprintf(ATTR_YELLOW, "[irq] storm on IRQ%d (>%d/tick unclaimed) -> masked\n",
                            (int)irq, (int)IRQ_STORM_LIMIT);
            }
        }
    }
    if (!handled) irq_unexpected++;

    if (claim.needs_finish) platform_irq_complete(&claim);
    if (!handled && !irq_test_quiet) isr_unexpected_report((u32)irq);
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
        irq_line_quarantined |= irq_projection(irq);
        platform_irq_mask(irq);
        irq_restore(saved);
        if (!irq_test_quiet)
            kprintf(ATTR_YELLOW, "[irq] IRQ%d quarantined (%d registrant(s) fall back to their tick hook)\n",
                    (int)irq, (int)irq_lines[idx].count);
        return 0;
    }
    platform_irq_mask(irq);
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

/* **試験専用** (kernel/irq/irq.h の注記を読むこと)。隔離とストームの試験の後に
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
    irq_storm_masked &= ~irq_projection(irq);
    irq_line_quarantined &= ~irq_projection(irq);
    irq_recount_locked(irq, &irq_lines[idx]);
    irq_restore(saved);
}
