/* Execution of S2_LAYOUT's forward plan, using only recover-authorized access.
 * Control semantics: DESIGN.final §4, MIT nouveau bios/init.c handlers as
 * cited by gfinit_decode.c. No ROM x86 code or hardware accessor lives here. */
#include "gfinit_exec.h"
static int exec_point(struct recover *r, struct gfinit_exec *e)
{
    u32 now;
    if (recover_safe_point(r) || e->ops->clock_us(e->opaque, &now)) return -1;
    e->elapsed_us = now - e->started;
    return e->elapsed_us >= GF_INIT_EXEC_US ? -1 : 0;
}
static int exec_wait(struct recover *r, struct gfinit_exec *e, u32 us)
{
    if (us > GF_INIT_WAIT_MAX - e->wait_us) return -1;
    e->wait_us += us;
    while (us) {
        u32 part = us > GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;
        if (exec_point(r, e) || e->ops->wait_us(e->opaque, part) || exec_point(r, e)) return -1;
        us -= part;
    }
    return 0;
}
static int exec_control(struct recover *r, struct gfinit_exec *e,
                        const struct recover_plan_node *n, u32 *edge)
{
    const struct recover_plan_arg *a = &r->plan->args[n->arg_first];
    if (n->substep >= GF_INIT_K) return 0; /* checked K: no access or wait */
    switch (n->opcode) {
    case 0x32: case 0x4a:
        if (n->arg_count != 4 || a[1].value >= 32 || e->read_pc != n->pc || e->read_ctx != n->ctx) return -1;
        *edge = ((e->read_value & a[0].value) >> a[1].value) == a[2].value;
        return 0;
    case 0x75:
        if (n->arg_count != 3 || e->read_pc != n->pc || e->read_ctx != n->ctx) return -1;
        *edge = (e->read_value & a[1].value) == a[2].value;
        return 0;
    case 0x39: {
        if (n->arg_count != 6 || a[2].value >= 32 || e->read_pc != n->pc || e->read_ctx != n->ctx) return -1;
        u32 index = (e->read_value & a[1].value) >> a[2].value;
        if (a[3].value >= e->rom_size || index >= e->rom_size - a[3].value) return -1;
        *edge = (e->rom[a[3].value + index] & a[4].value) == a[5].value;
        return 0;
    }
    case 0x5b: case 0x6b:
        if (n->substep == GF_INIT_C) {
            if (e->calls == GF_INIT_CALL_DEPTH) return -1;
            e->call[e->calls++] = (struct gfinit_exec_frame){n->pc, n->ctx, 0};
        } else {
            if (!e->calls || e->call[e->calls-1].pc != n->pc || e->call[e->calls-1].ctx != n->ctx) return -1;
            e->calls--;
        }
        return 0;
    case 0x33:
        if (n->substep == GF_INIT_C) {
            if (n->arg_count != 2 || !a[0].value || e->repeats == GF_INIT_REPEAT_DEPTH) return -1;
            e->repeat[e->repeats++] = (struct gfinit_exec_frame){n->pc, n->ctx, a[0].value};
        } else {
            if (!e->repeats || n->arg_count != 1) return -1;
            const struct gfinit_exec_frame *f = &e->repeat[e->repeats-1];
            if (n->pc != f->pc) return -1;
            if (n->substep == GF_INIT_C + 1 + f->count) {
                if (n->ctx != f->ctx || a[0].value != f->count) return -1;
                e->repeats--;
            } else if (a[0].value >= f->count) return -1;
        }
        return 0;
    case 0x36:
        return e->repeats ? 0 : -1;
    case 0x74: case 0x65:
        return n->arg_count == 1 ? exec_wait(r, e, a[0].value) : -1;
    case 0x38: case 0x72: case 0x63: case 0x71:
        return n->arg_count ? -1 : 0; /* head/skip live in checked successors */
    default: return -1;
    }
}
int gfinit_exec_run(struct recover *r, void *arg)
{
    struct gfinit_exec *e = arg;
    if (!e || !e->ops || !e->ops->clock_us || !e->ops->wait_us || !e->rom ||
        !r->enabled || !r->plan || r->plan->unresolved_count || e->rom_size != r->plan->rom_size ||
        e->main_id >= GF_INIT_MAINS || r->state != REC_RUNNING || r->entered) return -1;
    u32 first = RECOVER_NONE;
    for (u32 i = 0; i < r->plan->entry_count; i++)
        if (r->plan->entries[i].phase == REC_RUN && r->plan->entries[i].main_id == e->main_id)
            first = r->plan->entries[i].first;
    if (first == RECOVER_NONE || first != r->expected_node || r->head) return -1;
    e->steps = e->wait_us = e->elapsed_us = e->calls = e->repeats = 0;
    e->read_pc = e->read_ctx = e->fail_pc = RECOVER_NONE;
    if (e->ops->clock_us(e->opaque, &e->started)) return -1;
    for (;;) {
        u32 id = r->expected_node;
        if (id == RECOVER_NONE || r->plan->nodes[id].phase != REC_RUN ||
            r->plan->nodes[id].main_id != e->main_id)
            return e->calls || e->repeats ? -1 : 0;
        const struct recover_plan_node *n = &r->plan->nodes[id];
        const struct recover_opcode_layout *l = r->node_layout[id];
        e->fail_pc = n->pc;
        /* Each expanded node costs one; expression-internal READs also cost
         * one. Mutually exclusive static branches are never summed. */
        u32 cost = 1;
        for (u32 i = 0; i < n->arg_count; i++)
            if (r->plan->args[n->arg_first+i].kind == REC_ARG_READ) cost++;
        if (cost > GF_INIT_SCHEDULE - e->steps) return -1;
        e->steps += cost;
        if (exec_point(r, e) || recover_plan_enter(r, id)) return -1;
        u32 edge = 0, value = 0;
        if (l->dir_substep != RECOVER_NONE) {
            if (recover_plan_access(r, &value)) return -1;
            if (l->dir_substep == REC_READ) {
                e->read_value = value; e->read_pc = n->pc; e->read_ctx = n->ctx;
            }
        } else if (exec_control(r, e, n, &edge)) return -1;
        if (exec_point(r, e) || recover_plan_leave(r, edge)) return -1;
    }
}
