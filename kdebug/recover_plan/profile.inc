/* Shared descriptor validation. No hardware tables are supplied by T1b. */
static const struct recover_rule *rule_by_id(const struct recover_profile *p, u32 id)
{
    for (u32 i = 0; i < p->rule_count; i++) {
#ifdef RECOVER_HOST_LOOKUP
        RECOVER_HOST_LOOKUP();
#endif
        if (p->rules[i].id == id) return &p->rules[i];
    }
    return 0;
}
static const struct recover_expr *expr_by_id(const struct recover_profile *p, u32 id)
{
    for (u32 i = 0; i < p->expr_count; i++)
        if (p->exprs[i].id == id) return &p->exprs[i];
    return 0;
}
static const struct recover_opcode_layout *layout_for(const struct recover_profile *p,
                                                      const struct recover_plan_node *n)
{
    for (u32 i = 0; i < p->layout_count; i++) {
#ifdef RECOVER_HOST_LOOKUP
        RECOVER_HOST_LOOKUP();
#endif
        if (p->layouts[i].opcode == n->opcode && p->layouts[i].substep == n->substep)
            return &p->layouts[i];
    }
    return 0;
}
static bool is_bar(const struct recover_target *t)
{
    return t->space == REC_BAR_MMIO || t->space == REC_BAR_VRAM ||
        t->space == REC_VGA_DIRECT || t->space == REC_VGA_INDEXED;
}
int recover_target_address(const struct recover_target *t, u32 dir,
                           struct recover_bus_access *a)
{
    if (!t || !a || !width_ok(t->width) || t->space > REC_VGA_INDEXED ||
        t->access > REC_UNCLASSIFIED || t->read_safe > 1 || dir > REC_DATA)
        return -1;
    u32 off = t->offset;
    if (t->space == REC_VGA_DIRECT || t->space == REC_VGA_INDEXED) {
        if (t->bar || t->offset || t->head >= RECOVER_HEAD_COUNT || t->width != 1) return -1;
        if (t->space == REC_VGA_INDEXED) {
            if (t->index > 255 || (t->port != RECOVER_SEQ && t->port != RECOVER_GFX &&
                t->port != RECOVER_CRTC) || dir == REC_WRITE) return -1;
        } else if (t->index != RECOVER_NONE ||
                   (t->port != RECOVER_MISC_WRITE && t->port != RECOVER_MISC_READ &&
                    t->port != RECOVER_VGA_ENABLE) ||
                   dir == REC_SELECT || dir == REC_DATA) return -1;
        off = (t->port == RECOVER_CRTC ? RECOVER_PRMCIO : RECOVER_PRMVIO) +
            t->head * RECOVER_HEAD_STRIDE + t->port;
        if (t->space == REC_VGA_INDEXED && dir != REC_SELECT) off++;
    } else {
        if (t->head || t->port || t->index != RECOVER_NONE ||
            dir == REC_SELECT || dir == REC_DATA) return -1;
        if (is_bar(t)) {
            if (t->bar >= RECOVER_BAR_COUNT) return -1;
            /* Actual PRMVIO/PRMCIO ports, not the intervening PCRTC regs.
             * envytools f102b82 nv_vga.xml nv_vga_vio/nv_vga_cio. */
            static const u32 vio[] = {RECOVER_MISC_WRITE, RECOVER_VGA_ENABLE,
                RECOVER_SEQ, RECOVER_SEQ + 1, RECOVER_MISC_READ,
                RECOVER_GFX, RECOVER_GFX + 1};
            static const u32 cio[] = {RECOVER_CRTC_MONO, RECOVER_CRTC_MONO + 1,
                RECOVER_STATUS_MONO, RECOVER_ATTR, RECOVER_ATTR + 1,
                RECOVER_MISC_WRITE, RECOVER_FEATURE_READ,
                RECOVER_CRTC, RECOVER_CRTC + 1, RECOVER_STATUS};
            if (t->bar == 0) for (u32 head = 0; head < RECOVER_HEAD_COUNT; head++) {
                for (u32 i = 0; i < sizeof(vio) / sizeof(vio[0]); i++) {
                    u32 port = RECOVER_PRMVIO + head * RECOVER_HEAD_STRIDE + vio[i];
                    if (off <= port && (unsigned long long)off + t->width > port) return -1;
                }
                for (u32 i = 0; i < sizeof(cio) / sizeof(cio[0]); i++) {
                    u32 port = RECOVER_PRMCIO + head * RECOVER_HEAD_STRIDE + cio[i];
                    if (off <= port && (unsigned long long)off + t->width > port) return -1;
                }
            }
        } else if (t->bar || (t->space == REC_PCI_CFG &&
                   (off >= RECOVER_CFG_SIZE || t->width > RECOVER_CFG_SIZE - off)) ||
                   (t->space == REC_PC98_IO &&
                    (off > RECOVER_IO_LAST || off == RECOVER_ROUTE_PORT))) return -1;
    }
    /* These paired output/decode registers have only a 32-bit canonical form. */
    if (t->space == REC_PCI_CFG || (t->space == REC_BAR_MMIO && !t->bar)) {
        u32 first = t->space == REC_PCI_CFG ? RECOVER_CFG50 : RECOVER_PBUS1850;
        if (off < first + 8 && (unsigned long long)off + t->width > first &&
            (t->width != 4 || (off != first && off != first + 4))) return -1;
    }
    if (t->space == REC_PCI_CFG && off < RECOVER_COMMAND + 2 &&
        off + t->width > RECOVER_COMMAND && (off != RECOVER_COMMAND || t->width != 2)) return -1;
    if (off % t->width || (unsigned long long)off + t->width > RECOVER_ADDRESS_END) return -1;
    *a = (struct recover_bus_access){0, dir, t->space, t->bar, off, t->width};
    return 0;
}
static bool value_ok(const struct recover_value *v, u32 width, u32 value)
{
    if ((value & ~width_mask(width)) || (value & v->mask) != v->bits) return false;
    if (v->mode == REC_LITERAL) return value == v->bits;
    if (v->mode == REC_SET) {
        for (u32 i = 0; i < v->allowed_count; i++) if (value == v->allowed[i]) return true;
        return false;
    }
    if (v->mode == REC_RANGE || v->mode == REC_EXPR)
        return value >= v->min && value <= v->max;
    return v->mode == REC_SAVED;
}
static bool zero_target(const struct recover_target *t)
{
    return (t->space == REC_PCI_CFG && (t->offset == RECOVER_CFG50 || t->offset == RECOVER_CFG54)) ||
        (t->space == REC_BAR_MMIO && !t->bar &&
         (t->offset == RECOVER_PBUS1850 || t->offset == RECOVER_PBUS1854));
}
static bool n1_target(const struct recover_target *t)
{
    return t->space == REC_VGA_INDEXED && t->port == RECOVER_SEQ && t->index == RECOVER_SR01;
}
static bool saved_target(const struct recover_profile *p, u32 key)
{
    for (u32 i = 0; i < p->count; i++)
        if (p->regs[i].key == key && p->regs[i].kind == REC_RW && p->regs[i].read_safe)
            return true;
    return false;
}
static bool index_save_safe(const struct recover_profile *p, u32 key)
{
    const struct recover_target *t = &p->targets[key];
    if (t->space != REC_VGA_INDEXED) return false;
    for (u32 i = 0; i < p->index_save_count; i++) {
        const struct recover_target *q = &p->targets[p->index_save_targets[i]];
        if (t->bar == q->bar && t->head == q->head && t->port == q->port) return true;
    }
    return false;
}
int recover_profile_ok(const struct recover_profile *p)
{
    if (!p || (p->version != RECOVER_PROFILE_VERSION
#if GF_VPLL_PROBE
        && p->version != RECOVER_PROFILE_VPLL_VERSION
#endif
        ) || p->count > RECOVER_MAX_REGS || (p->count && !p->regs) ||
        p->quiesce_count > RECOVER_MAX_REGS || (p->quiesce_count && !p->quiesce) ||
        !p->targets || !p->target_count || p->target_count > RECOVER_TARGET_MAX ||
        p->rule_count > RECOVER_RULE_MAX || (p->rule_count && !p->rules) ||
        p->expr_count > RECOVER_RULE_MAX || (p->expr_count && !p->exprs) ||
        p->layout_count > RECOVER_RULE_MAX || (p->layout_count && !p->layouts) ||
        p->index_save_count > RECOVER_TARGET_MAX || (p->index_save_count && !p->index_save_targets)) return -1;
    for (u32 i = 0; i < p->index_save_count; i++)
        if (p->index_save_targets[i] >= p->target_count ||
            p->targets[p->index_save_targets[i]].space != REC_VGA_INDEXED) return -1;
    for (u32 i = 0; i < p->target_count; i++) {
        struct recover_bus_access a;
        const struct recover_target *t = &p->targets[i];
        if (recover_target_address(t, REC_READ, &a)) return -1;
        for (u32 j = 0; j < i; j++) {
            const struct recover_target *q = &p->targets[j];
            if (t->space == q->space && t->bar == q->bar && t->offset == q->offset &&
                t->head == q->head && t->port == q->port && t->index == q->index) return -1;
        }
    }
    for (u32 i = 0; i < p->expr_count; i++) {
        const struct recover_expr *e = &p->exprs[i];
        if (e->id == RECOVER_NONE || !e->eval || e->argc > RECOVER_EXPR_ARGS ||
            (e->argc && !e->kinds)) return -1;
        for (u32 j = 0; j < e->argc; j++) if (e->kinds[j] > REC_ARG_SAVED) return -1;
        for (u32 j = 0; j < i; j++) if (p->exprs[j].id == e->id) return -1;
    }
    for (u32 i = 0; i < p->layout_count; i++) {
        const struct recover_opcode_layout *l = &p->layouts[i];
        if (l->argc > RECOVER_EXPR_ARGS + 1 || (l->argc && !l->kinds) ||
            (l->flags & ~(REC_LAYOUT_SKIP_HEAD | REC_LAYOUT_CHECK_EXPR | REC_LAYOUT_UNBOUND | REC_LAYOUT_MODE_VRAM | REC_LAYOUT_MODE_COLOR | REC_LAYOUT_MODE_CHECK | REC_LAYOUT_MODE_WAIT | REC_LAYOUT_MODE_ROUTE)) ||
            (l->flags && l->dir_substep != RECOVER_NONE) ||
            (l->dir_substep != RECOVER_NONE && l->dir_substep > REC_DATA)) return -1;
        for (u32 j = 0; j < l->argc; j++) if (l->kinds[j] > REC_ARG_SAVED) return -1;
        for (u32 j = 0; j < i; j++)
            if (p->layouts[j].opcode == l->opcode && p->layouts[j].substep == l->substep) return -1;
    }
    for (u32 i = 0; i < p->rule_count; i++) {
        const struct recover_rule *w = &p->rules[i];
        if (w->id == RECOVER_NONE || w->key >= p->target_count || w->phase > REC_MODE_C ||
            (w->phase == REC_RUN ? (w->main_id > RECOVER_MAIN_MAX || w->pc == RECOVER_NONE) :
             (w->main_id != RECOVER_NONE || w->pc != RECOVER_NONE)) ||
            w->requires_count > RECOVER_LINK_MAX || (w->requires_count && !w->requires) ||
            w->guard_count > RECOVER_LINK_MAX || (w->guard_count && !w->guards) ||
            (w->flags & ~(REC_N1 | REC_LOCAL | REC_FORCE | REC_RUN_NO_PREREAD | REC_VERIFY_RECORD)) ||
            ((w->flags & REC_RUN_NO_PREREAD) && w->phase != REC_RUN) ||
            ((w->flags & REC_VERIFY_RECORD) && (w->phase != REC_MODE_C || !w->verify_mask))) return -1;
        const struct recover_target *t = &p->targets[w->key];
        const struct recover_value *v = &w->value;
        u32 mask = width_mask(t->width);
        if (t->access == REC_UNCLASSIFIED || ((w->write_mask | w->verify_mask | v->mask | v->bits) & ~mask) ||
            (v->bits & ~v->mask) || v->mode > REC_SAVED ||
            ((w->verify_mask || w->write_mask != mask) && !t->read_safe) ||
            v->allowed_count > RECOVER_RULE_MAX || (v->allowed_count && !v->allowed) ||
            (v->mode == REC_SET && !v->allowed_count) ||
            ((v->mode == REC_RANGE || v->mode == REC_EXPR) && (v->min > v->max || v->max > mask)) ||
            (v->mode == REC_EXPR && !expr_by_id(p, v->expr_id)) ||
            (v->mode != REC_EXPR && v->expr_id != RECOVER_NONE)) return -1;
        for (u32 j = 0; j < v->allowed_count; j++)
            if (!value_ok(v, t->width, v->allowed[j])) return -1;
        bool local_index = (w->flags & REC_LOCAL) && w->substep == REC_SELECT && index_save_safe(p, w->key);
        if (v->mode == REC_SAVED && ((!local_index && (t->access != REC_RW || !t->read_safe)) ||
            (!(w->flags & REC_LOCAL) && !saved_target(p, w->key)) ||
            w->phase != REC_RESTORE)) return -1;
        if (t->space == REC_PCI_CFG && t->offset == RECOVER_COMMAND &&
            (t->width != 2 || v->mode == REC_SAVED)) return -1;
        if ((w->flags & REC_N1) && (!n1_target(t) || w->phase != REC_RUN ||
            w->main_id != RECOVER_N1_MAIN || w->pc != RECOVER_N1_PC)) return -1;
        if (w->flags & REC_N1) {
            u32 found = 0;
            for (u32 j = 0; j < p->target_count; j++) {
                const struct recover_target *g = &p->targets[j];
                if (!zero_target(g)) continue;
                if (!g->read_safe || g->width != 4) return -1;
                if (g->space == REC_PCI_CFG) found |= g->offset == RECOVER_CFG50 ? RECOVER_N1_CFG50_BIT : RECOVER_N1_CFG54_BIT;
                else found |= g->offset == RECOVER_PBUS1850 ? RECOVER_N1_PBUS1850_BIT : RECOVER_N1_PBUS1854_BIT;
            }
            if (found != RECOVER_N1_ALL_BITS) return -1;
        }
        for (u32 j = 0; j < i; j++) {
            const struct recover_rule *q = &p->rules[j];
            if (q->id == w->id || (q->key == w->key && q->phase == w->phase &&
                q->main_id == w->main_id && q->pc == w->pc && q->substep == w->substep)) return -1;
        }
        for (u32 j = 0; j < w->requires_count; j++) {
            if (w->requires[j] == w->id || !rule_by_id(p, w->requires[j])) return -1;
            for (u32 k = 0; k < j; k++) if (w->requires[k] == w->requires[j]) return -1;
        }
        for (u32 j = 0; j < w->guard_count; j++) {
            const struct recover_guard *g = &w->guards[j];
            if (g->key >= p->target_count || !p->targets[g->key].read_safe ||
                p->targets[g->key].space == REC_VGA_INDEXED ||
                (g->when != REC_BEFORE && g->when != REC_AFTER && g->when != (REC_BEFORE | REC_AFTER)) ||
                ((g->mask | g->expected) & ~width_mask(p->targets[g->key].width))) return -1;
        }
    }
    /* Kahn-style elimination, bounded to the rule table, detects any cycle. */
    u32 done[RECOVER_RULE_MAX / BITS_PER_U32] = {0}, total = 0;
    for (u32 pass = 0; pass < p->rule_count; pass++) {
        u32 before = total;
        for (u32 i = 0; i < p->rule_count; i++) {
            if (done[i / BITS_PER_U32] & (1U << (i % BITS_PER_U32))) continue;
            const struct recover_rule *w = &p->rules[i]; bool ready = true;
            for (u32 j = 0; j < w->requires_count; j++) {
                const struct recover_rule *q = rule_by_id(p, w->requires[j]);
                u32 k = (u32)(q - p->rules);
                if (!(done[k / BITS_PER_U32] & (1U << (k % BITS_PER_U32)))) ready = false;
            }
            if (ready) { done[i / BITS_PER_U32] |= 1U << (i % BITS_PER_U32); total++; }
        }
        if (total == before) break;
    }
    if (total != p->rule_count) return -1;
    for (u32 i = 0; i < p->count; i++) {
        const struct recover_reg *d = &p->regs[i];
        if (d->key >= p->target_count || p->targets[d->key].space == REC_VGA_INDEXED ||
            d->width != p->targets[d->key].width ||
            !d->read_safe || !p->targets[d->key].read_safe ||
            (d->kind != REC_RW && d->kind != REC_COLD) ||
            d->kind != p->targets[d->key].access ||
            ((d->write_mask | d->verify_mask) & ~width_mask(d->width))) return -1;
        if (d->kind == REC_RW) {
            const struct recover_rule *w = rule_by_id(p, d->restore_rule_id);
            if (!w || w->key != d->key || w->phase != REC_RESTORE ||
                (p->targets[d->key].space == REC_PCI_CFG && p->targets[d->key].offset == RECOVER_COMMAND ?
                 (w->value.mode != REC_LITERAL || w->value.bits != RECOVER_COMMAND_OVERRIDE) :
                 w->value.mode != REC_SAVED) ||
                w->write_mask != d->write_mask || w->verify_mask != d->verify_mask) return -1;
        }
    }
    for (u32 i = 0; i < p->quiesce_count; i++) {
        const struct recover_write *q = &p->quiesce[i];
        const struct recover_rule *w = rule_by_id(p, q->rule_id);
        if (!w || w->phase != REC_QUIESCE || w->key != q->key ||
            q->width != p->targets[q->key].width || q->verify_mask != w->verify_mask ||
            !value_ok(&w->value, q->width, q->value)) return -1;
    }
    return 0;
}

static bool profile_ok(const struct recover_profile *p, u32 budget)
{
    return !recover_profile_ok(p) && finite(budget) && finite(p->trial_limit) &&
        budget <= p->trial_limit && finite(p->opcode_limit) && finite(p->restore_limit) &&
        finite(p->max_steps) && finite(p->max_polls) && finite(p->restore_max_polls) &&
        p->s0_reserve_ticks < p->restore_limit && p->s0_reserve_polls < p->restore_max_polls &&
        p->s0_reserve_bytes <= p->restore_log_bytes && p->crc == recover_profile_crc(p);
}
