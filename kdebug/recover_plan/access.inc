static bool extent_ok(const struct recover_profile *p, const struct recover_target *t,
                      const struct recover_bar_extent bars[RECOVER_BAR_COUNT])
{
    if (!is_bar(t)) return true;
    struct recover_bus_access a;
    if (recover_target_address(t, REC_READ, &a)) return false;
    const struct recover_bar_extent *b = &bars[a.bar];
    if (!b->valid || a.width > b->length || a.offset > b->length - a.width ||
        (unsigned long long)b->base + a.offset + a.width > RECOVER_ADDRESS_END) return false;
    if (t->space == REC_BAR_VRAM) {
        const struct recover_range *v = &p->vram_reserved;
        if (!v->len || (unsigned long long)v->base + v->len > b->length || a.offset < v->base ||
            (unsigned long long)a.offset + a.width > (unsigned long long)v->base + v->len) return false;
    }
    return true;
}
static bool raw_matches(struct recover *r, bool memory)
{
    u32 command, bars[RECOVER_BAR_COUNT];
    if ((r->state == REC_RESTORING && recover_restore_point(r)) ||
        r->ops->raw_config(r->opaque, &command, bars) ||
        (memory && !(command & RECOVER_COMMAND_MEM))) return false;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) {
        if ((r->bars[i].valid || r->measuring) && bars[i] != r->bars[i].raw) return false;
    }
    return bars[0] == r->token.identity.bar0;
}
static int bus_address(struct recover *r, u32 key, u32 dir, struct recover_bus_access *a)
{
    if (!r->profile || key >= r->profile->target_count) return -1;
    const struct recover_target *t = &r->profile->targets[key];
    if (r->measuring && is_bar(t) && r->measurement.bar[t->bar].valid) {
        u32 command, bars[RECOVER_BAR_COUNT];
        if ((r->state == REC_RESTORING && recover_restore_point(r)) ||
        r->ops->raw_config(r->opaque, &command, bars) || command != RECOVER_COMMAND_OVERRIDE) return -1;
        for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
            if (bars[i] != r->bars[i].raw) return -1;
        r->bars[t->bar] = r->measurement.bar[t->bar];
    }
    if (recover_target_address(t, dir, a) || !extent_ok(r->profile, t, r->bars)) return -1;
    if (is_bar(t) && !raw_matches(r, true)) return -1;
    a->key = key;
    return 0;
}
static int bus_read(struct recover *r, u32 key, u32 *value)
{
    struct recover_bus_access a;
    if (key >= r->profile->target_count || !r->profile->targets[key].read_safe ||
        bus_address(r, key, REC_READ, &a)) return -1;
    if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;
    return logged_read(r, &a, value);
}
static bool zero_guards(struct recover *r)
{
    u32 found = 0;
    if (!raw_matches(r, true)) return false;
    for (u32 i = 0; i < r->profile->target_count; i++) {
        const struct recover_target *t = &r->profile->targets[i];
        if (!zero_target(t)) continue;
        u32 value;
        if (t->width != 4 || bus_read(r, i, &value) || value != 0) return false;
        if (t->space == REC_PCI_CFG) found |= t->offset == RECOVER_CFG50 ? RECOVER_N1_CFG50_BIT : RECOVER_N1_CFG54_BIT;
        else found |= t->offset == RECOVER_PBUS1850 ? RECOVER_N1_PBUS1850_BIT : RECOVER_N1_PBUS1854_BIT;
    }
    return found == RECOVER_N1_ALL_BITS;
}
static bool guards(struct recover *r, const struct recover_rule *w, u32 when)
{
    if ((w->flags & REC_N1) && !zero_guards(r)) return false;
    for (u32 i = 0; i < w->guard_count; i++) {
        const struct recover_guard *g = &w->guards[i]; u32 value;
        if ((g->when & when) && (bus_read(r, g->key, &value) ||
            (value & g->mask) != g->expected)) return false;
    }
    return true;
}
static void capture_read(struct recover *r, u32 target, u32 ordinal, u32 value)
{
    if (!r->plan || !r->entered || r->state != REC_RUNNING) return;
    for (u32 i = 0; i < r->plan->capture_count; i++) {
        const struct recover_capture *c = &r->plan->captures[i];
        if (c->producer_node == r->cursor && c->target == target && c->read_ordinal == ordinal) {
            r->capture_values[i] = value; r->capture_valid |= 1ULL << i;
        }
    }
}
static bool evaluate(struct recover *r, const struct recover_plan_node *n,
                     const struct recover_rule *w, u32 *old, u32 *result)
{
    const struct recover_plan_arg *args = &r->plan->args[n->arg_first];
    if (w->value.mode == REC_EXPR) {
        const struct recover_expr *e = expr_by_id(r->profile, n->expr_id);
        u32 values[RECOVER_EXPR_ARGS], ordinal = 0;
        if (!e || n->arg_count != e->argc + 1) return false;
        for (u32 i = 0; i < e->argc; i++) {
            const struct recover_plan_arg *a = &args[i + 1];
            if (a->kind == REC_IMM) values[i] = a->value;
            else if (a->kind == REC_ARG_READ) {
                if ((r->profile->targets[a->value].space == REC_VGA_INDEXED &&
                     r->port_selected[r->index_port[a->value]] != a->value) || bus_read(r, a->value, &values[i])) return false;
                capture_read(r, a->value, ordinal++, values[i]);
                /* Validate preserved bits against the destination sample used
                 * by the expression. Status bits can change while serial logs
                 * separate this READ from the earlier pre-read. */
                if (a->value == w->key) *old = values[i];
            } else {
                if (!r->valid) return false;
                if (a->value < r->profile->count) {
                    if (a->value >= r->saved_count) return false;
                    values[i] = r->saved[a->value];
                } else {
                    u32 slot = a->value - r->profile->count;
                    if (slot >= r->plan->capture_count || !(r->capture_valid & (1ULL << slot)) ||
                        r->plan->captures[slot].consumer_node != r->cursor) return false;
                    values[i] = r->capture_values[slot];
                }
            }
        }
        return !e->eval(values, e->argc, result);
    }
    if (w->value.mode == REC_SAVED) {
        if (!r->valid || args[1].value >= r->saved_count) return false;
        *result = (*old & ~w->write_mask) | (r->saved[args[1].value] & w->write_mask);
    } else *result = args[1].value;
    return true;
}
static int access_error(struct recover *r, u32 key)
{
    if (r->state == REC_VERIFIED || r->state == REC_FAILED) return -1;
    fail(r, REC_CONTRACT, REC_PHASE_NONE, key);
    r->cursor = RECOVER_NONE; r->entered = false;
    if (r->state == REC_RUNNING) cleanup(r);
    return -1;
}
static enum recover_phase access_phase(const struct recover *r)
{
    switch (r->auth_phase) {
    case REC_QUIESCE: return REC_PHASE_QUIESCE;
    case REC_RESTORE: return REC_PHASE_RESTORE;
    case REC_ROUTE: return REC_PHASE_ROUTE;
    case REC_SCREEN: return REC_PHASE_SCREEN;
    case REC_VERIFY_PHASE: return REC_PHASE_VERIFY;
    default: return REC_PHASE_NONE;
    }
}
/* NV CR1F reads state (0/3), but accepts commands (99/57).
 * envytools nv_vga.xml:253-259, as recorded in the S5 addendum. */
static bool cr1f_data(const struct recover_target *t, u32 dir)
{
    return dir == REC_DATA && t->space == REC_VGA_INDEXED &&
        t->port == RECOVER_CRTC && t->index == RECOVER_CR1F;
}
static u32 saved_write_value(const struct recover_target *t, u32 dir, u32 value)
{
    return cr1f_data(t, dir) ? (value == RECOVER_CR1F_UNLOCKED ?
        RECOVER_CR1F_UNLOCK : RECOVER_CR1F_LOCK) : value;
}
static u32 readback_value(const struct recover_target *t, u32 dir, u32 value)
{
    return cr1f_data(t, dir) ? (value == RECOVER_CR1F_UNLOCK ?
        RECOVER_CR1F_UNLOCKED : 0) : value;
}
static int access_impl(struct recover *r, u32 node, u32 dir, u32 target, u32 width, u32 *value, bool planned)
{
    if (r->access_blocked) return -1;
    if (r->enabled && value && node == RECOVER_NONE && dir == REC_READ &&
        (r->state == REC_IDLE || r->state == REC_VERIFIED) && r->bound_profile) {
        const struct recover_profile *p = r->bound_profile;
        struct recover_bus_access a;
        if (target >= p->target_count) return -1;
        const struct recover_target *t = &p->targets[target];
        if (!t->read_safe || t->space == REC_VGA_INDEXED || width != t->width ||
            recover_target_address(t, REC_READ, &a) || !extent_ok(p, t, r->cache.bar)) return -1;
        if (is_bar(t)) {
            struct recover_identity id; u32 command, bars[RECOVER_BAR_COUNT];
            if (r->ops->identity(r->opaque, &id) || !same_identity(&id, &r->cache.identity) ||
                r->ops->raw_config(r->opaque, &command, bars) || !(command & RECOVER_COMMAND_MEM)) return -1;
            for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) if (bars[i] != r->cache.bar[i].raw) return -1;
        }
        a.key = target;
        return logged_read(r, &a, value);
    }
    if (!r->enabled || !r->profile || !value || target >= r->profile->target_count ||
        (r->state != REC_RUNNING && r->state != REC_RESTORING && r->state != REC_SNAPSHOTTING)) return -1;
    const struct recover_target *t = &r->profile->targets[target];
    const struct recover_plan_node *n = 0;
    const struct recover_rule *w = 0;
    if (node != RECOVER_NONE) {
        if (!r->plan || !r->entered || node != r->cursor || r->accessed ||
            !r->valid || r->state != REC_RUNNING) return access_error(r, target);
        n = &r->plan->nodes[node];
        const struct recover_opcode_layout *l = layout_for(r->profile, n);
        if (!l || l->dir_substep != dir || !node_ok(r->profile, r->plan, node) ||
            !n->arg_count || r->plan->args[n->arg_first].value != target ||
            n->head != r->head) return access_error(r, target);
        w = rule_by_id(r->profile, n->rule_id);
        if (!dependencies(r, r->profile, r->plan, node, r->buffers.valid_bits)) return access_error(r, target);
    } else {
        if (r->auth_phase != REC_RESTORE && r->auth_phase != REC_QUIESCE) return access_error(r, target);
        for (u32 i = 0; i < r->profile->rule_count; i++) {
            const struct recover_rule *q = &r->profile->rules[i];
            if (q->key == target && q->phase == r->auth_phase && q->substep == dir &&
                q->main_id == RECOVER_NONE && q->pc == RECOVER_NONE) {
                if (w) return access_error(r, target);
                w = q;
            }
        }
        if (w) for (u32 i = 0; i < w->requires_count; i++) {
            const struct recover_rule *required = rule_by_id(r->profile, w->requires[i]);
            if (!required || !bit_get(r->restored_rules, (u32)(required - r->profile->rules)))
                return access_error(r, target);
        }
    }
    struct recover_bus_access a;
    if (width != t->width || bus_address(r, target, dir, &a)) return access_error(r, target);
    if (dir == REC_READ) {
        if (!t->read_safe || (t->space == REC_VGA_INDEXED && r->port_selected[r->index_port[target]] != target) ||
            (r->state == REC_RESTORING && recover_restore_point(r)) ||
            logged_read(r, &a, value)) return access_error(r, target);
        capture_read(r, target, 0, *value);
    } else {
        if (r->state == REC_RESTORING && !restore_time(r)) return -1;
        if (t->space == REC_PCI_CFG && t->offset == RECOVER_COMMAND) {
            if (*value != RECOVER_COMMAND_OVERRIDE &&
                (r->auth_phase != REC_SIZE_BARS || (*value & RECOVER_COMMAND_DMA_MEM))) return access_error(r, target);
            if ((*value & RECOVER_COMMAND_MEM) && !raw_matches(r, false)) return access_error(r, target);
        }
        u32 old = 0, expected = 0;
        if (!w || t->access == REC_UNCLASSIFIED || (n && !node_rule(n, w)) ||
            (dir == REC_DATA && r->port_selected[r->index_port[target]] != target))
            return access_error(r, target);
        if (w->write_mask != width_mask(width) && (dir == REC_SELECT || bus_read(r, target, &old)))
            return access_error(r, target);
        if (r->transport && r->state == REC_RUNNING && w->write_mask == width_mask(width) &&
            t->read_safe && !(w->flags & REC_RUN_NO_PREREAD) && logged_read(r, &a, &old)) return access_error(r, target);
        if (n && !evaluate(r, n, w, &old, &expected)) return access_error(r, target);
        if (planned) *value = expected;
        if (!value_ok(&w->value, width, *value)) return access_error(r, target);
        if (dir == REC_SELECT && !(w->flags & REC_LOCAL) && *value != t->index) return access_error(r, target);
        if (zero_target(t) && *value != 0) return access_error(r, target);
        if (n1_target(t) && dir == REC_DATA && !(*value & RECOVER_SR_BLANK) &&
            r->auth_phase == REC_RUN && !(w->flags & REC_N1)) return access_error(r, target);
        if (((old ^ *value) & ~w->write_mask) != 0) return access_error(r, target);
        if (n) {
            if (*value != expected) return access_error(r, target);
        } else if (w->value.mode == REC_EXPR) return access_error(r, target);
        else if (w->value.mode == REC_SAVED) {
            bool found = false;
            for (u32 i = 0; i < r->saved_count; i++) {
                const struct recover_reg *d = &r->profile->regs[i];
                if (d->key == target && d->kind == REC_RW && t->access == REC_RW &&
                    d->restore_rule_id == w->id &&
                    !((*value ^ r->saved[i]) & w->write_mask)) found = true;
            }
            if (w->flags & REC_LOCAL) for (u32 i = 0; i < r->local_saved_count; i++) {
                const struct recover_local_saved *v = &r->local_saved[i];
                /* A classified saved DATA needs its own SELECT before replay;
                 * the original port index is still restored last. */
                if (dir == REC_SELECT && v->dir_substep == REC_DATA &&
                    v->key == target && *value == t->index && t->access == REC_RW) found = true;
                if (v->dir_substep == dir && (v->key == target ||
                    (dir == REC_SELECT && index_save_safe(r->profile, target) &&
                     r->index_port[v->key] == r->index_port[target])) &&
                    !((*value ^ saved_write_value(t, dir, v->value)) & w->write_mask)) found = true;
                /* Cleanup first unlocks the saved lock, then restores DATA,
                 * then replays its saved lock state before original indices. */
                if (v->key == target && v->dir_substep == REC_DATA &&
                    cr1f_data(t, dir) && *value == RECOVER_CR1F_UNLOCK) found = true;
            }
            if (!found) return access_error(r, target);
        }
        if (t->space == REC_VGA_INDEXED && r->state == REC_RUNNING &&
            ((t->access == REC_RW && (r->auth_phase != REC_MODE_C ||
              (r->c_only && dir == REC_DATA && t->port == RECOVER_CRTC &&
               (t->index == RECOVER_CR1F || t->index == RECOVER_CR11 ||
                t->index == RECOVER_CR44)))) ||
             (dir == REC_SELECT && index_save_safe(r->profile, target)))) {
            bool saved = false;
            for (u32 i = 0; i < r->local_saved_count; i++)
                if (r->local_saved[i].dir_substep == dir &&
                    (r->local_saved[i].key == target || (dir == REC_SELECT &&
                     r->index_port[r->local_saved[i].key] == r->index_port[target]))) saved = true;
            if (!saved) {
                u32 original;
                if ((!t->read_safe && !(dir == REC_SELECT && index_save_safe(r->profile, target))) ||
                    r->local_saved_count == RECOVER_MAX_REGS || logged_read(r, &a, &original)) return access_error(r, target);
                r->local_saved[r->local_saved_count++] = (struct recover_local_saved){target, dir, original};
            }
        }
        if (!guards(r, w, REC_BEFORE)) return access_error(r, target);
        if ((r->state == REC_RESTORING && recover_restore_point(r)) ||
            logged_write(r, &a, *value, old)) {
            if (r->state == REC_VERIFIED || r->state == REC_FAILED) return -1;
            fail(r, REC_BUS, access_phase(r), target);
            return access_error(r, target);
        }
        if (is_bar(t)) r->gpu_written = true;
        if (dir == REC_SELECT) r->port_selected[r->index_port[target]] =
            *value == t->index ? target : RECOVER_NONE;
        u32 actual = RECOVER_NONE;
        bool mismatch = false;
        if (w->verify_mask
#if GF_VPLL_PROBE
            || (r->profile->version == RECOVER_PROFILE_VPLL_VERSION && n &&
                w->main_id == RECOVER_VPLL_MAIN && w->pc == RECOVER_VPLL_PC &&
                ((w->substep >= RECOVER_VPLL_FIRST && w->substep <= RECOVER_VPLL_LAST) ||
                 (w->substep >= RECOVER_VPLL_REPEAT_FIRST && w->substep <= RECOVER_VPLL_REPEAT_LAST)))
#endif
            ) {
            /* SELECT readback reads the index port, not its data register. */
            if (!t->read_safe) return access_error(r, target);
            if ((r->state == REC_RESTORING && recover_restore_point(r)) ||
                logged_read(r, &a, &actual)) {
                if (r->state == REC_VERIFIED || r->state == REC_FAILED) return -1;
                fail(r, REC_BUS, access_phase(r), target);
                return access_error(r, target);
            }
            mismatch = ((actual ^ readback_value(t, dir, *value)) & w->verify_mask) != 0;
            if (mismatch && !(w->flags & REC_VERIFY_RECORD)) {
                if (r->state == REC_RUNNING) (void)gf_bus_record(r, &a, "END", old, *value, actual, -1, false);
                fail(r, REC_VERIFY, access_phase(r), target);
                return access_error(r, target);
            }
        }
        if (r->state == REC_RUNNING && gf_bus_record(r, &a, "END", old, *value, actual, 0, mismatch))
            return access_error(r, target);
        if (!guards(r, w, REC_AFTER)) return access_error(r, target);
        if (t->space == REC_VGA_INDEXED && t->port == RECOVER_CRTC &&
            t->index == RECOVER_CR44 && dir == REC_DATA) {
            if (t->head || (*value != RECOVER_CR44_HEAD0 && *value != RECOVER_CR44_HEAD1)) return access_error(r, target);
            r->head = *value == RECOVER_CR44_HEAD1;
        }
        if (node == RECOVER_NONE) bit_set(r->restored_rules, (u32)(w - r->profile->rules));
        if (node != RECOVER_NONE) select_record(r, node, r->buffers.valid_bits);
    }
    if (node != RECOVER_NONE) {
        for (u32 i = 0; i < r->plan->capture_count; i++)
            if (r->plan->captures[i].consumer_node == node) r->capture_valid &= ~(1ULL << i);
        bit_set(r->buffers.valid_bits, node);
    }
    r->accessed = true;
    return 0;
}
int recover_access(struct recover *r, u32 node, u32 dir, u32 target, u32 width, u32 *value)
{
    return access_impl(r, node, dir, target, width, value, false);
}
