static u32 instruction_at(const struct gfinit_decode *d, u32 pc)
{
    for (u32 i = 0; i < d->instruction_count; i++) if (d->insns[i].pc == pc) return i;
    return RECOVER_NONE;
}
static int schedule_add(struct gfinit_decode *d, u32 ins, u32 ctx, u32 main,
                        u32 kind, u32 k, u32 count)
{
    if (d->schedule_count == GF_INIT_WORK_STEPS) return -1;
    d->schedule[d->schedule_count++] = (struct gfinit_scheduled){ins, ctx, main, kind, k, count, 0};
    return 0;
}
/* kind 0 instruction, 1 call return, 2 repeat iteration, 3 repeat exit. */
static int schedule_block(struct gfinit_decode *d, u32 pc, u32 end, u32 ctx,
                          u32 main, u32 calls, u32 repeats, u32 *active)
{
    if (calls > GF_INIT_CALL_DEPTH || repeats > GF_INIT_REPEAT_DEPTH) return -1;
    if (calls > d->max_call) d->max_call = calls;
    if (repeats > d->max_repeat) d->max_repeat = repeats;
    for (;;) {
        u32 i = instruction_at(d, pc);
        if (i == RECOVER_NONE || active[i]) return -1;
        struct gfinit_instruction *v = &d->insns[i];
        u32 at = d->schedule_count;
        if (schedule_add(d, i, ctx, main, 0, 0, 0)) return -1;
        if (v->opcode == GF_INIT_DONE || v->opcode == GF_INIT_END_REPEAT) return v->pc == end ? 0 : -1;
        active[i] = 1;
        if (v->opcode == GF_INIT_SUB_DIRECT || v->opcode == GF_INIT_SUB) {
            u32 target_end = RECOVER_NONE;
            for (u32 j = 0; j < GF_ARRAY(scripts); j++)
                if (scripts[j].pc == v->ref && (v->opcode != GF_INIT_SUB || scripts[j].id == v->index)) target_end = scripts[j].pc + scripts[j].length - 1;
            if (target_end == RECOVER_NONE || d->plan.ctx_count == RECOVER_CTX_MAX ||
                schedule_block(d, v->ref, target_end, d->plan.ctx_count++, main, calls + 1, repeats, active)) return -1;
            if (schedule_add(d, i, ctx, main, 1, 0, 0)) return -1;
            d->schedule[at].jump = d->schedule_count;
        } else if (v->opcode == GF_INIT_REPEAT) {
            for (u32 k = 0; k < v->count; k++) {
                if (d->plan.ctx_count == RECOVER_CTX_MAX) return -1;
                u32 child = d->plan.ctx_count++;
                if (schedule_add(d, i, child, main, 2, k, v->count) ||
                    schedule_block(d, pc + v->length, v->end_repeat, child, main, calls, repeats + 1, active)) return -1;
                d->schedule[d->schedule_count - 1].k = k;
                d->schedule[d->schedule_count - 1].count = v->count;
                d->schedule[d->schedule_count - 1].jump = pc;
            }
            if (schedule_add(d, i, ctx, main, 3, 0, v->count)) return -1;
            pc = v->end_repeat + 1;
            active[i] = 0; continue;
        }
        active[i] = 0; pc += v->length;
    }
}
