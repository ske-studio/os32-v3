static bool bit_get(const u32 *bits, u32 n) { return (bits[n / BITS_PER_U32] & (1U << (n % BITS_PER_U32))) != 0; }
static void bit_set(u32 *bits, u32 n) { bits[n / BITS_PER_U32] |= 1U << (n % BITS_PER_U32); }
static bool dependencies(const struct recover *r, const struct recover_profile *p, const struct recover_plan *plan,
                         u32 node, const u32 *bits)
{
    const struct recover_plan_node *n = &plan->nodes[node];
    const struct recover_rule *w = r->node_write[node];
    const struct recover_opcode_layout *layout = r->node_layout[node];
    for (u32 i = 0; i < n->arg_count; i++) {
        const struct recover_plan_arg *arg = &plan->args[n->arg_first + i];
        bool target_read = i == 0 && layout &&
            (layout->dir_substep == REC_DATA || layout->dir_substep == REC_READ);
        if (layout && (layout->flags & REC_LAYOUT_MODE_CHECK) &&
            i == REC_CHECK_KEY && plan->args[n->arg_first + REC_CHECK_SOURCE].value == REC_CHECK_TARGET)
            target_read = true;
        if (!target_read && arg->kind != REC_ARG_READ) continue;
        u32 key = arg->value;
        if (key >= p->target_count) return false;
        if (p->targets[key].space == REC_VGA_INDEXED) {
            bool selected = false;
            for (u32 j = 0; j < node; j++) {
                const struct recover_plan_node *q = &plan->nodes[j];
                const struct recover_opcode_layout *l = r->node_layout[j];
                const struct recover_rule *rw = r->node_write[j];
                if (l && l->dir_substep == REC_SELECT && rw && rw->key == key &&
                    q->ctx == n->ctx && q->phase == n->phase && q->main_id == n->main_id && bit_get(bits, j)) selected = true;
            }
            if (!selected) return false;
        }
    }
    if (!w) return n->rule_id == RECOVER_NONE;
    for (u32 i = 0; i < w->requires_count; i++) {
        u32 id = w->requires[i], explicit_node = RECOVER_NONE;
        for (u32 j = 0; j < plan->dep_count; j++)
            if (plan->deps[j].consumer == node && plan->deps[j].required_rule == id)
                explicit_node = plan->deps[j].producer;
        bool found = false;
        for (u32 j = 0; j < node; j++) {
            const struct recover_plan_node *q = &plan->nodes[j];
            if (q->rule_id == id && bit_get(bits, j) &&
                (explicit_node != RECOVER_NONE ? j == explicit_node :
                 (q->ctx == n->ctx && q->phase == n->phase && q->main_id == n->main_id)))
                found = true;
        }
        if (!found) return false;
    }
    return true;
}
/* A SELECT invalidates older selections of the same physical index port. */
static void select_record(const struct recover *r, u32 node, u32 *bits)
{
    const struct recover_opcode_layout *l = r->node_layout[node];
    const struct recover_rule *w = r->node_write[node];
    if (!l || l->dir_substep != REC_SELECT || !w) return;
    for (u32 i = 0; i < node; i++) {
        const struct recover_rule *q = r->node_write[i];
        const struct recover_opcode_layout *ql = r->node_layout[i];
        if (q && ql && ql->dir_substep == REC_SELECT) {
            if (r->index_port[w->key] == r->index_port[q->key])
                bits[i / BITS_PER_U32] &= ~(1U << (i % BITS_PER_U32));
        }
    }
}
/* A profile must explicitly authorize a logical head transition. */
static bool skip_head_change(const struct recover_plan_node *n,
                             const struct recover_opcode_layout *l)
{
    return n->skip && l && l->dir_substep == RECOVER_NONE &&
        (l->flags & REC_LAYOUT_SKIP_HEAD);
}
static bool node_rule(const struct recover_plan_node *n, const struct recover_rule *w)
{
    return n->phase == w->phase && n->main_id == w->main_id &&
        n->pc == w->pc && n->substep == w->substep;
}
static bool arg_ok(const struct recover_profile *p, const struct recover_plan *plan, u32 id, const struct recover_plan_arg *a)
{
    if (a->kind == REC_IMM) return true;
    if (a->kind == REC_ARG_READ)
        return a->value < p->target_count && p->targets[a->value].read_safe;
    if (a->kind == REC_ARG_SAVED && a->value >= p->count) {
        u32 i = a->value - p->count;
        return i < plan->capture_count && plan->captures[i].slot == a->value &&
            plan->captures[i].consumer_node == id && (plan->nodes[id].phase == REC_RUN || plan->nodes[id].phase == REC_MODE_C);
    }
    return a->kind == REC_ARG_SAVED && a->value < p->count &&
        p->regs[a->value].kind == REC_RW && p->regs[a->value].read_safe;
}
static bool node_ok(const struct recover_profile *p, const struct recover_plan *plan, u32 id)
{
    const struct recover_plan_node *n = &plan->nodes[id];
    const struct recover_opcode_layout *l = layout_for(p, n);
    if (!l || (n->flags & ~REC_NODE_CHECK_ENTRY) ||
        (n->flags && !(l->flags & REC_LAYOUT_MODE_CHECK)) || n->phase > REC_MODE_C || n->ctx >= plan->ctx_count ||
        n->head >= RECOVER_HEAD_COUNT || n->skip > 1 || n->arg_first > plan->arg_count ||
        n->arg_count > plan->arg_count - n->arg_first || n->arg_count != l->argc ||
        (n->phase == REC_RUN ? n->main_id > RECOVER_MAIN_MAX || n->pc == RECOVER_NONE :
         n->main_id != RECOVER_NONE || n->pc != RECOVER_NONE)) return false;
    const struct recover_plan_arg *args = n->arg_count ? &plan->args[n->arg_first] : 0;
    for (u32 i = 0; i < n->arg_count; i++)
        if (args[i].kind != l->kinds[i] || !arg_ok(p, plan, id, &args[i])) return false;
    if (l->dir_substep == RECOVER_NONE) {
        if (n->rule_id != RECOVER_NONE) return false;
        if (l->flags & REC_LAYOUT_MODE_ROUTE)
            return l->flags==REC_LAYOUT_MODE_ROUTE && n->phase==REC_MODE_C &&
                !n->head && !n->skip && n->expr_id==RECOVER_NONE && n->arg_count==2 &&
                args[0].value==RECOVER_ROUTE_RGB_IN && args[1].value==RECOVER_ROUTE_MASK;
        if (l->flags & (REC_LAYOUT_MODE_CHECK | REC_LAYOUT_MODE_WAIT)) {
            if (n->phase != REC_MODE_C || n->head || n->skip || n->expr_id != RECOVER_NONE) return false;
            if (l->flags == REC_LAYOUT_MODE_WAIT)
                return n->arg_count == 1 && args[0].kind == REC_IMM && args[0].value;
            if (l->flags != REC_LAYOUT_MODE_CHECK || n->arg_count != REC_CHECK_ARGS) return false;
            for (u32 i=0;i<REC_CHECK_ARGS;i++)
                if (i != REC_CHECK_EXPECTED && args[i].kind != REC_IMM) return false;
            if (args[REC_CHECK_EXPECTED].kind != REC_IMM && args[REC_CHECK_EXPECTED].kind != REC_ARG_SAVED) return false;
            u32 source=args[REC_CHECK_SOURCE].value, key=args[REC_CHECK_KEY].value;
            if (source == REC_CHECK_TARGET)
                return key < p->target_count && p->targets[key].read_safe;
            return key == RECOVER_NONE && (source == REC_CHECK_IDENTITY || source == REC_CHECK_ROUTE || source == REC_CHECK_CELL);
        }
        if (l->flags & REC_LAYOUT_MODE_COLOR) {
            if (l->flags != REC_LAYOUT_MODE_COLOR || n->phase != REC_MODE_C ||
                n->head || n->skip || n->expr_id != RECOVER_NONE || n->arg_count != 2 ||
                args[0].kind != REC_IMM || args[1].kind != REC_IMM) return false;
            u32 row=args[0].value, value=args[1].value;
            if (row == GF_MODE_ROW_DAC) return value == GF_MODE_BYTE_MASK;
            if (row == GF_MODE_ROW_GATE_OFF) return value == 0;
            if (row == GF_MODE_ROW_GATE_ON || row == GF_MODE_ROW_GATE_COMMIT) return value == GF_MODE_AR_GATE_ENABLE;
            if (row < GF_MODE_ROW_AR_FIRST || row >= GF_MODE_ROW_AR_END) return false;
            u32 index=row-GF_MODE_ROW_AR_FIRST;
            return value == (index < GF_MODE_AR_IDENTITY_COUNT ? index :
                index == GF_MODE_AR_CONTROL_INDEX ? 1 : index == GF_MODE_AR_PLANES_INDEX ? GF_MODE_PLANES : 0);
        }
        if (l->flags & REC_LAYOUT_MODE_VRAM)
            return l->flags == REC_LAYOUT_MODE_VRAM && n->phase == REC_MODE_C &&
                !n->head && !n->skip && n->expr_id == RECOVER_NONE && n->arg_count == 4 &&
                args[0].kind == REC_IMM && args[0].value == GF_MODE_VRAM_BAR &&
                args[1].kind == REC_IMM && args[1].value == GF_MODE_VRAM_OFFSET &&
                args[2].kind == REC_IMM && args[2].value == GF_MODE_VRAM_BYTES &&
                args[3].kind == REC_IMM && args[3].value <= REC_WRITE &&
                p->vram_reserved.base == GF_MODE_VRAM_OFFSET && p->vram_reserved.len == GF_MODE_VRAM_BYTES;
        if (!(l->flags & REC_LAYOUT_CHECK_EXPR)) return n->expr_id == RECOVER_NONE;
        const struct recover_expr *e = expr_by_id(p, n->expr_id);
        if (!e || e->argc != n->arg_count || n->skip) return false;
        for (u32 i = 0; i < e->argc; i++)
            if (args[i].kind != e->kinds[i] || args[i].kind == REC_ARG_SAVED ||
                (args[i].kind == REC_ARG_READ && p->targets[args[i].value].space == REC_VGA_INDEXED)) return false;
        return true;
    }
    if (!n->arg_count || args[0].kind != REC_IMM || args[0].value >= p->target_count) return false;
    const struct recover_target *t = &p->targets[args[0].value];
    struct recover_bus_access bus;
    if (recover_target_address(t, l->dir_substep, &bus) ||
        ((t->space == REC_VGA_DIRECT || t->space == REC_VGA_INDEXED) && t->head != n->head &&
         !(t->space == REC_VGA_INDEXED && t->port == RECOVER_CRTC &&
           t->index == RECOVER_CR44 && t->head == 0))) return false;
    if (l->dir_substep == REC_READ)
        return !n->skip && t->read_safe && n->arg_count == 1 && n->rule_id == RECOVER_NONE && n->expr_id == RECOVER_NONE;
    const struct recover_rule *w = rule_by_id(p, n->rule_id);
    if (!w || !node_rule(n, w) || w->key != args[0].value || n->expr_id != w->value.expr_id ||
        (n->skip && !(w->flags & REC_FORCE))) return false;
    if (w->value.mode == REC_EXPR) {
        const struct recover_expr *e = expr_by_id(p, n->expr_id);
        if (!e || n->arg_count != e->argc + 1) return false;
        if (zero_target(t) && (w->value.mask != width_mask(t->width) || w->value.bits)) return false;
        if (n1_target(t) && n->phase == REC_RUN && !(w->flags & REC_N1) &&
            (!(w->value.mask & RECOVER_SR_BLANK) || !(w->value.bits & RECOVER_SR_BLANK))) return false;
        bool imm = true; u32 values[RECOVER_EXPR_ARGS], result;
        for (u32 i = 0; i < e->argc; i++) {
            if (args[i + 1].kind != e->kinds[i]) return false;
            values[i] = args[i + 1].value;
            if (args[i + 1].kind != REC_IMM) imm = false;
        }
        if (imm && (e->eval(values, e->argc, &result) || !value_ok(&w->value, t->width, result))) return false;
    } else {
        if (n->arg_count != 2) return false;
        if (w->value.mode == REC_SAVED) {
            if (args[1].kind != REC_ARG_SAVED || args[1].value >= p->count || p->regs[args[1].value].key != w->key) return false;
        } else if (args[1].kind != REC_IMM || !value_ok(&w->value, t->width, args[1].value)) return false;
        if (l->dir_substep == REC_SELECT && args[1].value != t->index) return false;
        if (zero_target(t) && args[1].value) return false;
        if (l->dir_substep != REC_SELECT && n1_target(t) && !(args[1].value & RECOVER_SR_BLANK) &&
            !(w->flags & REC_N1) && n->phase == REC_RUN) return false;
    }
    return true;
}
static u32 plan_crc(const struct recover_plan *p)
{
    u32 c = CRC32_INIT;
    c = crc_word(c, p->version);
    c = crc_word(c, p->profile_crc);
    c = crc_word(c, p->rom_crc); c = crc_word(c, p->rom_size);
    c = crc_word(c, p->node_count); c = crc_word(c, p->arg_count);
    c = crc_word(c, p->ctx_count); c = crc_word(c, p->dep_count); c = crc_word(c, p->entry_count);
    for (u32 i = 0; i < p->node_count; i++) {
        const struct recover_plan_node *n = &p->nodes[i];
        c = crc_word(c, n->phase); c = crc_word(c, n->main_id); c = crc_word(c, n->pc);
        c = crc_word(c, n->opcode); c = crc_word(c, n->substep); c = crc_word(c, n->ctx);
        c = crc_word(c, n->head); c = crc_word(c, n->skip); c = crc_word(c, n->rule_id);
        c = crc_word(c, n->expr_id); c = crc_word(c, n->arg_first); c = crc_word(c, n->arg_count);
        c = crc_word(c, n->next[0]); c = crc_word(c, n->next[1]);
        c = crc_word(c, n->flags);
    }
    for (u32 i = 0; i < p->arg_count; i++) {
        c = crc_word(c, p->args[i].kind); c = crc_word(c, p->args[i].value);
    }
    for (u32 i = 0; i < p->dep_count; i++) {
        c = crc_word(c, p->deps[i].consumer); c = crc_word(c, p->deps[i].required_rule);
        c = crc_word(c, p->deps[i].producer);
    }
    for (u32 i = 0; i < p->entry_count; i++) {
        c = crc_word(c, p->entries[i].phase); c = crc_word(c, p->entries[i].main_id);
        c = crc_word(c, p->entries[i].first);
    }
    c = crc_word(c, p->unresolved_count);
    c = crc_word(c, p->capture_count);
    for (u32 i = 0; i < p->capture_count; i++) {
        const struct recover_capture *v = &p->captures[i];
        c = crc_word(c, v->slot); c = crc_word(c, v->producer_node);
        c = crc_word(c, v->consumer_node); c = crc_word(c, v->target);
        c = crc_word(c, v->read_ordinal);
    }
    return crc32_core_final(c);
}
int recover_bind_plan(struct recover *r, const struct recover_profile *p,
    const struct recover_plan *plan, struct recover_plan_buffers *buf, u32 *hash)
{
    bool reentry = gf_stage_reentry(r);
    if (r->c_only && (!reentry || !p || !p->cold_required ||
        p->expected_device != RECOVER_GF_DEVICE)) return -1;
    if (!r->enabled || r->plan ||
        ((r->state != REC_IDLE && r->state != REC_VERIFIED &&
          !(r->state == REC_FAILED && reentry)) ||
         ((r->failed || r->cold_latched) && !reentry)) || recover_profile_ok(p) || p->crc != recover_profile_crc(p) ||
        !plan || !buf || !buf->flow_bits || !buf->valid_bits || !hash || !plan->version ||
        plan->profile_crc != p->crc || !plan->node_count || plan->node_count > RECOVER_NODE_MAX ||
        !plan->nodes || plan->arg_count > RECOVER_ARG_MAX || (plan->arg_count && !plan->args) ||
        !plan->ctx_count || plan->ctx_count > RECOVER_CTX_MAX || plan->dep_count > RECOVER_DEP_MAX ||
        (plan->dep_count && !plan->deps) || !plan->entry_count || plan->entry_count > RECOVER_ENTRY_MAX ||
        !plan->entries || plan->unresolved_count || plan->capture_count > RECOVER_CAPTURE_MAX ||
        (plan->capture_count && !plan->captures)) return -1;
    for (u32 i = 0; i < plan->node_count; i++) {
        if (r->c_only && plan->nodes[i].phase != REC_MODE_C) return -1;
        r->node_layout[i] = layout_for(p, &plan->nodes[i]);
        r->node_write[i] = rule_by_id(p, plan->nodes[i].rule_id);
    }
    for (u32 i = 0; i < p->target_count; i++) {
        r->index_port[i] = i; r->port_selected[i] = RECOVER_NONE;
        if (p->targets[i].space != REC_VGA_INDEXED) continue;
        struct recover_bus_access a, b;
        if (recover_target_address(&p->targets[i], REC_SELECT, &a)) return -1;
        for (u32 j = 0; j < i; j++) {
            if (p->targets[j].space != REC_VGA_INDEXED) continue;
            if (recover_target_address(&p->targets[j], REC_SELECT, &b)) return -1;
            if (a.space == b.space && a.bar == b.bar && a.offset == b.offset) {
                r->index_port[i] = r->index_port[j]; break;
            }
        }
    }
    for (u32 i = 0; i < RECOVER_BITS_WORDS; i++) buf->valid_bits[i] = 0;
    for (u32 i = 0; i < plan->node_count * RECOVER_BITS_WORDS; i++) buf->flow_bits[i] = 0;
    for (u32 i = 0; i < plan->entry_count; i++) {
        const struct recover_plan_entry *e = &plan->entries[i];
        if (e->first >= plan->node_count || (i == 0 && e->first != 0) ||
            plan->nodes[e->first].phase != e->phase || plan->nodes[e->first].main_id != e->main_id) return -1;
        if (i && (e->first <= plan->entries[i - 1].first ||
            e->phase < plan->entries[i - 1].phase ||
            (e->phase == plan->entries[i - 1].phase &&
             (e->phase != REC_RUN || e->main_id != plan->entries[i - 1].main_id + 1)))) return -1;
        if (e->phase == REC_RUN && (i == 0 || plan->entries[i - 1].phase != REC_RUN) && e->main_id) return -1;
    }
    for (u32 i = 0; i < plan->dep_count; i++) {
        const struct recover_plan_dep *d = &plan->deps[i];
        if (d->consumer >= plan->node_count || d->producer >= d->consumer ||
            plan->nodes[d->producer].rule_id != d->required_rule) return -1;
        const struct recover_rule *w = rule_by_id(p, plan->nodes[d->consumer].rule_id);
        bool found = false;
        if (w) for (u32 j = 0; j < w->requires_count; j++) if (w->requires[j] == d->required_rule) found = true;
        if (!found) return -1;
        for (u32 j = 0; j < i; j++) if (plan->deps[j].consumer == d->consumer &&
            plan->deps[j].required_rule == d->required_rule) return -1;
    }
    /* A capture names a read source and one explicit expression consumer;
     * the consumer may write a different target (e.g. a PLL mirror). The
     * same instruction/context, dominance and single-use checks still apply. */
    for (u32 i = 0; i < plan->capture_count; i++) {
        const struct recover_capture *c = &plan->captures[i];
        if (c->slot != p->count + i || c->producer_node >= c->consumer_node ||
            c->consumer_node >= plan->node_count || c->target >= p->target_count) return -1;
        const struct recover_plan_node *a = &plan->nodes[c->producer_node];
        const struct recover_plan_node *b = &plan->nodes[c->consumer_node];
        if (!node_ok(p, plan, c->producer_node) || !node_ok(p, plan, c->consumer_node) ||
            (a->phase != REC_RUN && a->phase != REC_MODE_C) || b->phase != a->phase || a->main_id != b->main_id ||
            a->pc != b->pc || a->ctx != b->ctx || a->head != b->head ||
            b->arg_count < 2) return -1;
        bool uses_slot = false;
        for (u32 j = 1; j < b->arg_count; j++)
            if (plan->args[b->arg_first + j].kind == REC_ARG_SAVED &&
                plan->args[b->arg_first + j].value == c->slot) uses_slot = true;
        if (!uses_slot) return -1;
        u32 ordinal = 0; bool found = false;
        const struct recover_opcode_layout *l = r->node_layout[c->producer_node];
        if (l->dir_substep == RECOVER_NONE) return -1;
        if (l->dir_substep == REC_READ) {
            found = c->read_ordinal == 0 && plan->args[a->arg_first].value == c->target;
        } else for (u32 j = 1; j < a->arg_count; j++) {
            const struct recover_plan_arg *v = &plan->args[a->arg_first + j];
            if (v->kind == REC_ARG_READ && ordinal++ == c->read_ordinal)
                found = v->value == c->target;
        }
        if (!found) return -1;
    }
    r->capture_valid = 0;
    bit_set(buf->valid_bits, 0); /* temporary reachability during forward scan */
    u32 entry = 0;
    for (u32 i = 0; i < plan->node_count; i++) {
        const struct recover_plan_node *n = &plan->nodes[i];
        if (entry + 1 < plan->entry_count && plan->entries[entry + 1].first == i) entry++;
        if (!bit_get(buf->valid_bits, i) || !node_ok(p, plan, i) ||
            n->phase != plan->entries[entry].phase || n->main_id != plan->entries[entry].main_id) return -1;
        u32 *in = &buf->flow_bits[i * RECOVER_BITS_WORDS];
        if (!dependencies(r, p, plan, i, in)) return -1;
        for (u32 j = 0; j < plan->capture_count; j++)
            if (plan->captures[j].consumer_node == i &&
                !bit_get(in, plan->captures[j].producer_node)) return -1;
        select_record(r, i, in); bit_set(in, i);
        for (u32 edge = 0; edge < 2; edge++) {
            u32 next = n->next[edge];
            if (next == RECOVER_NONE) {
                if (entry + 1 != plan->entry_count) return -1;
                continue;
            }
            if (next <= i || next >= plan->node_count) return -1;
            if (entry + 1 < plan->entry_count && next >= plan->entries[entry + 1].first &&
                next != plan->entries[entry + 1].first) return -1;
            const struct recover_plan_node *q = &plan->nodes[next];
            u32 head = n->head;
            const struct recover_rule *w = rule_by_id(p, n->rule_id);
            if (w) {
                const struct recover_target *t = &p->targets[w->key];
                const struct recover_opcode_layout *l = layout_for(p, n);
                if (t->space == REC_VGA_INDEXED && t->port == RECOVER_CRTC &&
                    t->index == RECOVER_CR44 && l->dir_substep == REC_DATA) {
                    if (t->head) return -1;
                    u32 value;
                    if (w->value.mode == REC_EXPR) {
                        /* A fixed result constraint also proves the head for
                         * an RMW expression; a dynamic head stays forbidden. */
                        if (w->value.mask != width_mask(t->width)) return -1;
                        value = w->value.bits;
                    } else {
                        if (n->arg_count != 2 || plan->args[n->arg_first + 1].kind != REC_IMM) return -1;
                        value = plan->args[n->arg_first + 1].value;
                    }
                    if (value != RECOVER_CR44_HEAD0 && value != RECOVER_CR44_HEAD1) return -1;
                    head = value == RECOVER_CR44_HEAD1;
                }
            }
            if (q->phase == REC_RUN && (q->phase != n->phase || q->main_id != n->main_id)) head = 0;
            if (q->head != head && !skip_head_change(n, r->node_layout[i])) return -1;
            u32 *out = &buf->flow_bits[next * RECOVER_BITS_WORDS];
            bool seen = bit_get(buf->valid_bits, next);
            for (u32 j = 0; j < RECOVER_BITS_WORDS; j++) out[j] = seen ? out[j] & in[j] : in[j];
            bit_set(buf->valid_bits, next);
        }
    }
    for (u32 i = 0; i < RECOVER_BITS_WORDS; i++) buf->valid_bits[i] = 0;
    r->plan = plan; r->bound_profile = p; r->buffers = *buf;
    r->bound_hash = plan_crc(plan); *hash = r->bound_hash;
    r->cursor = RECOVER_NONE; r->expected_node = 0; r->head = plan->nodes[0].head;
    r->entered = false;
    return 0;
}
static void release_plan(struct recover *r)
{
    if (r->plan) for (u32 i = 0; i < RECOVER_BITS_WORDS; i++) r->buffers.valid_bits[i] = 0;
    for (u32 i = 0; i < RECOVER_NODE_MAX; i++) {
        r->node_layout[i] = 0; r->node_write[i] = 0;
    }
    r->capture_valid = 0;
    r->plan = 0; r->bound_profile = 0; r->buffers = (struct recover_plan_buffers){0};
    r->cursor = r->expected_node = RECOVER_NONE; r->entered = false;
}
