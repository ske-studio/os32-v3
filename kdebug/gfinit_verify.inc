#ifndef KDEBUG_GFINIT_VERIFY_H
#define KDEBUG_GFINIT_VERIFY_H
#include "gfinit_plan_c.h"
#include "../hwdev/gfinit_mode.h"

/* Apply before binding. CHECK nodes, SELECTs and the legacy prefix stay intact.
 * The required target set comes from the immutable exit CHECKs themselves. */
static int gfinit_verify_policy(struct gfinit_plan *out, struct recover_profile *p,
                               u32 flags, const struct gfinit_no_verify *regs, u32 count)
{
    u32 matched[GF_NO_VERIFY_MAX] = {0};
    if (!(out->mode_flags & GF_MODE_C) || count > GF_NO_VERIFY_MAX) return -1;
    for (u32 i = 0; i < p->rule_count; i++) {
        struct recover_rule *w = &out->rules[i];
        if (w->phase != REC_MODE_C || w->substep == GF_C_SELECT) continue;
        const struct recover_target *t = &p->targets[w->key];
        bool required = false, selected = false, exit = false;
        for (u32 j = out->c_node; j < out->plan.node_count; j++) {
            const struct recover_plan_node *n = &out->plan.nodes[j];
            const struct recover_plan_arg *a = &out->plan.args[n->arg_first];
            if (n->substep == GF_C_SECTION && a[0].value == GF_C_CHECK_OUT) exit = true;
            if (exit && (n->substep == GF_C_CHECK || n->substep == GF_C_CHECK_SAVED) &&
                a[REC_CHECK_SOURCE].value == REC_CHECK_TARGET &&
                a[REC_CHECK_KEY].value == w->key && a[REC_CHECK_MASK].value) required = true;
        }
        for (u32 j = 0; j < count; j++) {
            bool hit = (regs[j].kind == GF_VERIFY_MMIO && t->space == REC_BAR_MMIO &&
                        !t->bar && t->offset == regs[j].reg) ||
                (t->space == REC_VGA_INDEXED && t->index == regs[j].reg &&
                 ((regs[j].kind == GF_VERIFY_CR && t->port == RECOVER_CRTC) ||
                  (regs[j].kind == GF_VERIFY_SR && t->port == RECOVER_SEQ)));
            if (hit) { matched[j] = 1; selected = true; }
        }
        if (selected || !required || (flags & GF_VERIFY_RECORD)) {
            w->flags |= REC_VERIFY_RECORD;
            /* Even table entries with no readback become evidence-only reads. */
            if (!w->verify_mask) w->verify_mask = t->width == 1 ? GF_MODE_BYTE_MASK : ~0U;
        }
    }
    for (u32 j = 0; j < count; j++) if (!matched[j]) return -1;
    p->crc = recover_profile_crc(p);
    out->plan.profile_crc = p->crc;
    return 0;
}
#endif
