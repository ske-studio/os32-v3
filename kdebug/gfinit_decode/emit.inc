static u32 key_for(const struct recover_profile *p, u32 reg, u32 port, u32 index, u32 head)
{
    if (port == RECOVER_CRTC && index == RECOVER_CR44) head = 0;
    for (u32 i = 0; i < p->target_count; i++) {
        const struct recover_target *t = &p->targets[i];
        if (port ? t->space == REC_VGA_INDEXED && t->port == port && t->index == index && t->head == head :
                   t->space == REC_BAR_MMIO && !t->bar && t->offset == reg) return i;
    }
    return RECOVER_NONE;
}
#define GF_PENDING 0x80000000U
static u32 destination(u32 step, u32 state) { return GF_PENDING | (step * GF_STATE_COUNT + state); }
static u32 emit(struct gfinit_decode *d, const struct recover_profile *p,
                const struct gfinit_scheduled *s, u32 state, u32 sub, u32 dir,
                const struct recover_plan_arg *a, u32 argc)
{
    if (d->plan.node_count >= RECOVER_NODE_MAX || argc > GF_INIT_ARGS - d->plan.arg_count) {
        d->error = 1; return RECOVER_NONE;
    }
    const struct gfinit_instruction *v = &d->insns[s->instruction];
    u32 id = d->plan.node_count++;
    struct recover_plan_node *n = &d->nodes[id];
    *n = (struct recover_plan_node){.phase = REC_RUN, .main_id = s->main_id,
        .pc = v->pc, .opcode = v->opcode, .substep = sub, .ctx = s->ctx,
        .head = GF_STATE_HEAD(state), .skip = state & GF_STATE_SKIP, .rule_id = RECOVER_NONE,
        .expr_id = RECOVER_NONE, .arg_first = d->plan.arg_count, .arg_count = argc,
        .next = {id + 1, id + 1}};
    for (u32 j = 0; j < argc; j++) d->args[d->plan.arg_count++] = a[j];
    if (dir != RECOVER_NONE && dir != REC_READ) {
        for (u32 j = 0; j < p->rule_count; j++) {
            const struct recover_rule *w = &p->rules[j];
            if (w->phase == REC_RUN && w->main_id == s->main_id && w->pc == v->pc && w->substep == sub) {
                if (n->rule_id != RECOVER_NONE || !argc || w->key != a[0].value) d->error = 2;
                n->rule_id = w->id; n->expr_id = w->value.expr_id;
            }
        }
    }
    if (dir != RECOVER_NONE && dir != REC_READ && n->rule_id == RECOVER_NONE)
        d->plan.unresolved_count++;
    return id;
}
#define IMM(v) ((struct recover_plan_arg){REC_IMM, (v)})
#define READ(v) ((struct recover_plan_arg){REC_ARG_READ, (v)})
#define SAVED(v) ((struct recover_plan_arg){REC_ARG_SAVED, (v)})
static u32 control(struct gfinit_decode *d, const struct recover_profile *p,
                   const struct gfinit_scheduled *s, u32 state, u32 sub,
                   u32 a, u32 b, u32 c, u32 count)
{
    struct recover_plan_arg args[] = {IMM(a), IMM(b), IMM(c)};
    return emit(d, p, s, state, sub, RECOVER_NONE, args, count);
}
static u32 write_node(struct gfinit_decode *d, const struct recover_profile *p,
                      const struct gfinit_scheduled *s, u32 state, u32 e,
                      u32 dir, u32 key, u32 mask, u32 value, bool rmw)
{
    struct recover_plan_arg a[] = {IMM(key), rmw ? READ(key) : IMM(value), IMM(mask), IMM(value)};
    if (key == RECOVER_NONE) d->error = 3;
    return emit(d, p, s, state, e, dir, a, rmw ? 4 : 2);
}
static u32 read_node(struct gfinit_decode *d, const struct recover_profile *p,
                     const struct gfinit_scheduled *s, u32 state, u32 key)
{
    struct recover_plan_arg a = IMM(key);
    if (key == RECOVER_NONE || !p->targets[key].read_safe) d->error = 4;
    return emit(d, p, s, state, GF_INIT_Q, REC_READ, &a, 1);
}
