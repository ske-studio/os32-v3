int recover_plan_access(struct recover *r, u32 *value)
{
    if (!r->plan || !r->entered || r->cursor >= r->plan->node_count || !value) return -1;
    const struct recover_plan_node *n = &r->plan->nodes[r->cursor];
    const struct recover_opcode_layout *l = r->node_layout[r->cursor];
    if (l && (l->flags & REC_LAYOUT_MODE_ROUTE)) return mode_route_access(r);
    if (l && (l->flags & REC_LAYOUT_MODE_VRAM)) return mode_vram_access(r);
    if (l && (l->flags & REC_LAYOUT_MODE_COLOR)) return mode_color_access(r);
    if (l && (l->flags & REC_LAYOUT_MODE_CHECK)) return mode_check_access(r,value);
    if (l && (l->flags & REC_LAYOUT_MODE_WAIT)) return mode_wait_access(r);
    if (!l || l->dir_substep == RECOVER_NONE || !n->arg_count) return -1;
    u32 key = r->plan->args[n->arg_first].value;
    if (key >= r->profile->target_count) return -1;
    return access_impl(r, r->cursor, l->dir_substep, key,
                       r->profile->targets[key].width, value, true);
}
int recover_plan_check(struct recover *r, u32 *value)
{
    if (r->state != REC_RUNNING || !r->plan || !r->entered || r->accessed ||
        !r->valid || !value || r->cursor >= r->plan->node_count) return -1;
    const struct recover_plan_node *n = &r->plan->nodes[r->cursor];
    const struct recover_opcode_layout *l = r->node_layout[r->cursor];
    const struct recover_expr *e = expr_by_id(r->profile, n->expr_id);
    if (!l || !(l->flags & REC_LAYOUT_CHECK_EXPR) || !e ||
        !node_ok(r->profile, r->plan, r->cursor)) return access_error(r, r->cursor);
    u32 values[RECOVER_EXPR_ARGS];
    for (u32 i = 0; i < e->argc; i++) {
        const struct recover_plan_arg *a = &r->plan->args[n->arg_first + i];
        if (a->kind == REC_IMM) values[i] = a->value;
        else if (bus_read(r, a->value, &values[i])) return access_error(r, a->value);
    }
    if (e->eval(values, e->argc, value) || *value > 1) return access_error(r, r->cursor);
    r->accessed = true;
    bit_set(r->buffers.valid_bits, r->cursor);
    return 0;
}
int recover_plan_enter(struct recover *r, u32 node)
{
    if (r->state != REC_RUNNING || !r->plan || !r->valid || r->entered ||
        node >= r->plan->node_count || node != r->expected_node) return access_error(r, node);
    const struct recover_plan_node *n = &r->plan->nodes[node];
    if ((r->node_layout[node]->flags & REC_LAYOUT_UNBOUND) ||
        n->head != r->head || !dependencies(r, r->profile, r->plan, node, r->buffers.valid_bits)) return access_error(r, node);
    r->cursor = node; r->entered = true; r->accessed = false; r->auth_phase = n->phase;
    if (n->phase == REC_RUN) {
        for (u32 i = 0; i < r->profile->target_count; i++)
            if (zero_target(&r->profile->targets[i])) r->zero_hold = true;
        if (r->zero_hold && !zero_guards(r)) return access_error(r, node);
    }
    return 0;
}
int recover_plan_leave(struct recover *r, u32 edge)
{
    if (r->state != REC_RUNNING || !r->plan || !r->entered || edge > 1) return access_error(r, edge);
    const struct recover_plan_node *n = &r->plan->nodes[r->cursor];
    const struct recover_opcode_layout *l = layout_for(r->profile, n);
    if (!l || (l->flags & REC_LAYOUT_UNBOUND) || (((l->dir_substep != RECOVER_NONE || (l->flags & REC_LAYOUT_CHECK_EXPR)) || (l->flags & (REC_LAYOUT_MODE_VRAM | REC_LAYOUT_MODE_COLOR | REC_LAYOUT_MODE_CHECK | REC_LAYOUT_MODE_WAIT | REC_LAYOUT_MODE_ROUTE))) && !r->accessed)) return access_error(r, r->cursor);
    if (n->phase == REC_RUN && r->zero_hold && !zero_guards(r)) return access_error(r, r->cursor);
    u32 next = n->next[edge];
    if (n->phase == REC_PREPARE && (next == RECOVER_NONE || r->plan->nodes[next].phase != REC_PREPARE)) {
        bool gf = false;
        for (u32 i = 0; i < r->profile->target_count; i++) if (zero_target(&r->profile->targets[i])) gf = true;
        if (gf && !zero_guards(r)) return access_error(r, r->cursor);
        r->zero_hold = gf;
    }
    if (next != RECOVER_NONE && (skip_head_change(n, l) ||
        (r->plan->nodes[next].phase == REC_RUN && (n->phase != REC_RUN ||
         r->plan->nodes[next].main_id != n->main_id)))) r->head = r->plan->nodes[next].head;
    r->expected_node = next; r->entered = false; r->cursor = RECOVER_NONE;
    return 0;
}
