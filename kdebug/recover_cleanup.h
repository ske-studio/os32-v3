#ifndef KDEBUG_RECOVER_CLEANUP_H
#define KDEBUG_RECOVER_CLEANUP_H

#include "config.h"
#include "pc98.h"
#include "memmap.h"
#include "gfinit_decode.h"

/* Internal to recover.c. A row's sizeof includes one compensation LF in
 * addition to its explicit LF; CR is also allowed for the budget proof.
 * No wire-format fields or profile CRC inputs are changed by this budget. */
#define REC_CLEANUP_BUSY_PER_BYTE 1U
#define REC_CLEANUP_POINTS_PER_PAIR 16U
#define REC_CLEANUP_FIXED_POINTS RECOVER_BUDGET_S0_POINTS
#define REC_CLEANUP_S0_PAIRS 14U
struct rec_cleanup_budget {
    u32 ticks, polls, bytes;
    u32 s0_ticks, s0_polls, s0_bytes;
    u32 seconds;
};
static struct rec_cleanup_budget cb;

static u32 cleanup_zero_count(const struct recover_profile *p)
{
    u32 n = 0;
    for (u32 i = 0; i < p->target_count; i++) if (zero_target(&p->targets[i])) n++;
    return n;
}

/* Number of logged read/write pairs in access_impl, including both guard
 * passes, RMW input and optional readback. Failed branches emit no more. */
static u32 cleanup_access_pairs(const struct recover_profile *p, u32 key, u32 dir, u32 phase)
{
    const struct recover_rule *w = 0;
    for (u32 i = 0; i < p->rule_count; i++) {
        const struct recover_rule *q = &p->rules[i];
        if (q->key == key && q->phase == phase && q->substep == dir &&
            q->main_id == RECOVER_NONE && q->pc == RECOVER_NONE) w = q;
    }
    if (!w) return 0; /* authorization rejects before any bus operation */
    const struct recover_target *t = &p->targets[key];
    u32 pairs = 1 + (w->verify_mask != 0) + (w->write_mask != width_mask(t->width));
    if (w->flags & REC_N1) pairs += 2 * cleanup_zero_count(p);
    for (u32 i = 0; i < w->guard_count; i++) {
        if (w->guards[i].when & REC_BEFORE) pairs++;
        if (w->guards[i].when & REC_AFTER) pairs++;
    }
    return pairs;
}

static int cleanup_stage(u32 bytes, u32 ms, u32 points, u32 frame,
                          u32 *ticks, u32 *polls)
{
    unsigned long long us = (unsigned long long)bytes * frame;
    if (us > ~0U || ms > ~0U - RECOVER_US_PER_MS + 1) return -1;
    u32 tx = (u32)us / RECOVER_UART_US + ((u32)us % RECOVER_UART_US != 0);
    u32 margin = tx / 2 + (tx % 2 != 0);
    if (margin < RECOVER_BUDGET_MARGIN_S) margin = RECOVER_BUDGET_MARGIN_S;
    unsigned long long seconds = (unsigned long long)tx + (ms + RECOVER_US_PER_MS - 1) / RECOVER_US_PER_MS + margin;
    unsigned long long time = seconds * PIT_HZ;
    unsigned long long count = (unsigned long long)bytes * (1 + REC_CLEANUP_BUSY_PER_BYTE) + ms + points;
    if (!time || time >= RECOVER_HALF_TICKS || !count || count >= RECOVER_HALF_TICKS) return -1;
    *ticks = (u32)time; *polls = (u32)count;
    return 0;
}

static int cleanup_budget_compute(const struct recover *r, const struct recover_profile *p, bool maximum,
                                   struct rec_cleanup_budget *out)
{
    static const char begin_row[] = "REC sid=4294967295 seq=4294967295 phase=restore event=BEGIN step=4294967295 addr=4294967295\r\n";
    static const char end_row[] = "REC sid=4294967295 seq=4294967295 phase=restore event=END rc=-2147483648 elapsed=4294967295\r\n";
    static const char state_row[] = "REC sid=4294967295 seq=4294967295 phase=state event=RESTORING version=4294967295 profile_crc=4294967295 plan_hash=4294967295 snapshot_crc=4294967295 cleanup_seconds=4294967295\r\n";
    static const char final_row[] = "REC sid=4294967295 seq=4294967295 phase=final event=END result=PARTIAL target=S0 verified=1 unknown=1 override=1 saved=4294967295 actual=4294967295 trial=4294967295 lost_lines=4294967295 discarded_bytes=4294967295 txd=1\r\n";
    static const char discard_row[] = "REC sid=4294967295 seq=4294967295 phase=receive event=DISCARD discarded_bytes=4294967295\r\n";
    static const char diag_row[] = "REC sid=4294967295 seq=4294967295 phase=diag event=TX fail_seq=4294967295 pos=4294967295 fn=WAIT_FRAME rc=-2147483648 where=BUSY_WAIT sent=1 polls=4294967295 bytes=4294967295\r\n";
    u32 pair = sizeof(begin_row) + sizeof(end_row);
    unsigned long long pairs = 0;
    if (!maximum && (r->local_saved_count > RECOVER_MAX_REGS || r->saved_count > p->count)) return -1;
    if (maximum) {
        u32 worst = 0;
        for (u32 i = 0; i < p->target_count; i++) {
            const struct recover_target *t = &p->targets[i];
            u32 n = cleanup_access_pairs(p, i, REC_SELECT, REC_RESTORE);
            if (t->index == RECOVER_CR1F || t->index == RECOVER_CR11 || t->index == RECOVER_CR44) {
                u32 data = n + cleanup_access_pairs(p, i, REC_DATA, REC_RESTORE);
                if (t->index == RECOVER_CR1F) data *= 2; /* unlock, then lock */
                if (data > n) n = data;
            }
            if (n > worst) worst = n;
        }
        pairs = (unsigned long long)RECOVER_MAX_REGS * worst;
    } else for (u32 i = 0; i < r->local_saved_count; i++) {
        const struct recover_local_saved *v = &r->local_saved[i];
        if (v->key >= p->target_count) return -1;
        const struct recover_target *t = &p->targets[v->key];
        if (v->dir_substep == REC_DATA) {
            if (t->index != RECOVER_CR1F && t->index != RECOVER_CR11 && t->index != RECOVER_CR44) continue;
            u32 n = cleanup_access_pairs(p, v->key, REC_SELECT, REC_RESTORE) +
                    cleanup_access_pairs(p, v->key, REC_DATA, REC_RESTORE);
            pairs += t->index == RECOVER_CR1F ? 2 * n : n;
        } else if (v->dir_substep == REC_SELECT) {
            /* Cleanup substitutes an index-port representative; choose the
             * largest authorized SELECT over the actual saved index set. */
            u32 n = cleanup_access_pairs(p, v->key, REC_SELECT, REC_RESTORE);
            for (u32 j = 0; j < p->index_save_count; j++) {
                u32 key = p->index_save_targets[j];
                if (key >= p->target_count) return -1;
                if (r->index_port[key] == r->index_port[v->key]) {
                    n = cleanup_access_pairs(p, key, REC_SELECT, REC_RESTORE); break;
                }
            }
            pairs += n;
        } else return -1;
    }
    for (u32 i = 0; i < p->quiesce_count; i++)
        pairs += cleanup_access_pairs(p, p->quiesce[i].key, REC_WRITE, REC_QUIESCE);
    u32 saved = maximum ? p->count : r->saved_count;
    for (u32 i = 0; i < saved; i++) {
        const struct recover_reg *d = &p->regs[i];
        bool command = p->targets[d->key].space == REC_PCI_CFG && p->targets[d->key].offset == RECOVER_COMMAND;
        if (d->kind == REC_COLD || (p->cold_required && !command)) continue;
        pairs += 1 + cleanup_access_pairs(p, d->key, REC_WRITE, REC_RESTORE);
    }
    if (maximum || r->zero_hold) pairs += cleanup_zero_count(p);
    /* A cancelled runner can still own one pending PLL/index transaction.
     * Reserve its reverse writes before S0; C-only and A1 keep their budgets. */
    u32 runner_pairs = 0;
    if (r->plan) for (u32 i=0;i<r->plan->node_count;i++)
        if (r->plan->nodes[i].opcode==GF_INIT_RUNNER && (maximum || i==r->expected_node)) {
            runner_pairs=2U*GF_PLL_WRITES_MAX; /* select + data per indexed save */
            break;
        }
    unsigned long long pre = pairs * pair + sizeof(state_row) +
        (unsigned long long)runner_pairs * 2U * (RECOVER_ROW_MAX+1U);
    u32 s0 = REC_CLEANUP_S0_PAIRS * pair + sizeof(final_row) + sizeof(discard_row) + sizeof(diag_row);
    if (s0 < RECOVER_BUDGET_BYTES_S0) s0 = RECOVER_BUDGET_BYTES_S0;
    if (pre + s0 > ~0U || (pairs + runner_pairs) * REC_CLEANUP_POINTS_PER_PAIR + REC_CLEANUP_FIXED_POINTS > ~0U) return -1;
    u32 frame = r->transport ? r->transport->frame_us :
        (RECOVER_UART_FRAME_US / SYS_SERIAL_BAUD + (RECOVER_UART_FRAME_US % SYS_SERIAL_BAUD != 0));
    if (!frame || frame > RECOVER_UART_US) return -1;
    u32 ms = (maximum || r->measuring) ? RECOVER_BUDGET_EXEC_MS_SIZE_BARS : RECOVER_BUDGET_EXEC_MS_RUN;
    u32 ticks, polls, s0_ticks, s0_polls;
    if (cleanup_stage((u32)pre, ms - RECOVER_BUDGET_EXEC_MS_S0,
            ((u32)pairs + runner_pairs) * REC_CLEANUP_POINTS_PER_PAIR + REC_CLEANUP_FIXED_POINTS, frame, &ticks, &polls) ||
        cleanup_stage(s0, RECOVER_BUDGET_EXEC_MS_S0, RECOVER_BUDGET_S0_POINTS, frame, &s0_ticks, &s0_polls)) return -1;
    if (s0_polls < RECOVER_BUDGET_POLLS_S0) s0_polls = RECOVER_BUDGET_POLLS_S0;
    if (ticks >= RECOVER_HALF_TICKS - s0_ticks || polls >= RECOVER_HALF_TICKS - s0_polls) return -1;
    *out = (struct rec_cleanup_budget){ ticks + s0_ticks, polls + s0_polls,
        (u32)pre + s0, s0_ticks, s0_polls, s0,
        (ticks + s0_ticks + PIT_HZ - 1) / PIT_HZ };
    return 0;
}

static int cleanup_budget_prepare(struct recover *r)
{
    const struct recover_profile *p = r->profile;
    cb = (struct rec_cleanup_budget){p->restore_limit, p->restore_max_polls,
        p->restore_log_bytes, p->s0_reserve_ticks, p->s0_reserve_polls,
        p->s0_reserve_bytes, p->restore_limit / PIT_HZ + (p->restore_limit % PIT_HZ != 0)};
    if (p->expected_device != RECOVER_GF_DEVICE || r->s0_only || r->c_only) return 0;
    struct rec_cleanup_budget next;
    if (cleanup_budget_compute(r, p, false, &next) ||
        (r->transport && next.polls > r->transport->max_polls)) return -1;
    cb = next;
    return 0;
}
#endif
