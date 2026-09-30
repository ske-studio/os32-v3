/* ======================================================================== */
/*  IRQ_MATH.C — 動的 IRQ の判断だけ (I/O なし)                             */
/*                                                                          */
/*  ここには outp/inp も tick_count も無い。全部の分岐がホストで踏める。     */
/*    → kernel/irq_math.h の先頭、tools/tests/irq_math_tdd.md               */
/* ======================================================================== */

#include "irq_math.h"

/* 共通スタブに結ぶ線。**並びがそのまま表の添字**で、kernel/irq.c の
 * irq_lines[] / 診断の順序もこれに従う。固定スタブの線 (0 タイマ / 1 KBD /
 * 2 VSYNC / 4 シリアル / 7 スプリアス / 11 FDC / 12 サウンド / 13 マウス) は
 * ここに無く、irq_register は -NOTSUP で断る。 */
static const unsigned char irq_dyn_lines[IRQ_DYN_COUNT] = {
    3, 5, 6, 8, 9, 10, 14, 15
};

int irq_dyn_index(unsigned int irq)
{
    int i;

    /* **範囲検査を先に**。呼び手は PCI の未割り当て 0xFF をそのまま渡し得る
     * ので、シフトにも PIC のポート選択にも入れる前に落とす (票 1-6)。 */
    if (irq >= (unsigned int)IRQ_LINE_MAX) return -1;
    for (i = 0; i < IRQ_DYN_COUNT; i++) {
        if ((unsigned int)irq_dyn_lines[i] == irq) return i;
    }
    return -1;
}

int irq_register_check(unsigned int irq, const struct irq_line *ln,
                       irq_handler_fn fn, void *arg, unsigned int flags,
                       int in_irq)
{
    int i;

    /* ISR の中からの登録は契約違反。救済しないで断る (debug で数える)。 */
    if (in_irq) return IRQ_ERR_CTX;

    if (irq >= (unsigned int)IRQ_LINE_MAX) return IRQ_ERR_INVAL;
    if (irq_dyn_index(irq) < 0 || ln == 0) return IRQ_ERR_NOTSUP;

    if (fn == 0) return IRQ_ERR_INVAL;
    if (flags & ~(unsigned int)IRQ_F_ALL) return IRQ_ERR_INVAL;

    if (ln->quarantined) return IRQ_ERR_BUSY;

    for (i = 0; i < (int)ln->count; i++) {
        if (ln->slot[i].fn == fn && ln->slot[i].arg == arg) return IRQ_ERR_EXIST;
    }

    /* 共有は**全員の合意**。新規が SHARED でも、既に居る誰か 1 人が
     * 排他で入っていれば受けない (片側だけ見ると、排他のつもりの driver が
     * 知らないうちに相席させられる)。 */
    if (ln->count > 0) {
        if (!(flags & (unsigned int)IRQ_F_SHARED)) return IRQ_ERR_SHARE;
        for (i = 0; i < (int)ln->count; i++) {
            if (!(ln->slot[i].flags & (unsigned int)IRQ_F_SHARED)) {
                return IRQ_ERR_SHARE;
            }
        }
    }

    if ((int)ln->count >= IRQ_MAX_HANDLERS) return IRQ_ERR_FULL;

    return (int)ln->count;
}

int irq_unregister_find(unsigned int irq, const struct irq_line *ln,
                        irq_handler_fn fn, void *arg, int in_irq)
{
    int i;

    if (in_irq) return IRQ_ERR_CTX;
    if (irq >= (unsigned int)IRQ_LINE_MAX) return IRQ_ERR_INVAL;
    if (irq_dyn_index(irq) < 0 || ln == 0) return IRQ_ERR_NOTSUP;
    if (fn == 0) return IRQ_ERR_INVAL;

    for (i = 0; i < (int)ln->count; i++) {
        if (ln->slot[i].fn == fn && ln->slot[i].arg == arg) return i;
    }
    return IRQ_ERR_NOENT;
}

void irq_slot_remove(struct irq_line *ln, int idx)
{
    int i;

    if (ln == 0 || idx < 0 || idx >= (int)ln->count) return;
    for (i = idx; i + 1 < (int)ln->count; i++) {
        ln->slot[i] = ln->slot[i + 1];
    }
    ln->count--;
    ln->slot[ln->count].fn    = 0;
    ln->slot[ln->count].arg   = 0;
    ln->slot[ln->count].flags = 0;
}

int irq_dispatch_line(struct irq_line *ln, unsigned int irq)
{
    int any = 0;
    int pass;

    if (ln == 0) return 0;
    if (ln->count >= 2) ln->shared_dispatch++;

    for (pass = 0; pass < 2; pass++) {
        int round = 0;
        int i;
        int n = (int)ln->count;

        for (i = 0; i < n; i++) {
            irq_handler_fn fn = ln->slot[i].fn;
            int rc;
            if (fn == 0) continue;
            rc = fn(irq, ln->slot[i].arg);
            if (rc == IRQ_DEFERRED) ln->deferred_count++;
            /* **打ち切らない。** A が受けても B の要因はまだ線を上げている。 */
            round |= rc;
        }
        /* **OR で足す。** 2 巡目が 0 でも 1 巡目の結果は消えない。 */
        any |= round;
        if (round == 0) break;      /* 1 巡目で誰も受けなければ 2 巡目は無い */
    }
    return any != 0;
}

int irq_storm_step(struct irq_line *ln, unsigned int now_tick, int handled_any)
{
    if (ln == 0) return 0;

    if (ln->tick_stamp != now_tick) {
        ln->tick_stamp = now_tick;
        ln->tick_hits = 0;
    }
    ln->tick_hits++;

    /* 票の字義どおり: 「1 tick の間のディスパッチが LIMIT を**超えて**、
     * かつ**そのディスパッチを誰も受けなかった**」。受けた回も数に入れる
     * (受けた回でマスクには入らない)。201 回目で入り、200 回では入らない。 */
    if (ln->tick_hits > (unsigned int)IRQ_STORM_LIMIT && !handled_any) {
        return ln->storm_masked ? 0 : 1;
    }
    return 0;
}

int irq_eoi_plan(unsigned int irq, int slave_isr_bit7)
{
    if (irq >= (unsigned int)IRQ_LINE_MAX) return IRQ_EOI_NONE;

    if (irq == 15) {
        /* スレーブ側スプリアス: ISR の IR7 が立っていなければスレーブへは
         * 送らない (カスケード分のマスタだけ)。**handled とは無関係** —
         * 受け手が居たかどうかでスプリアスかどうかは決まらない。 */
        return slave_isr_bit7 ? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;
    }
    return (irq >= 8) ? IRQ_EOI_SLAVE_MASTER : IRQ_EOI_MASTER;
}
