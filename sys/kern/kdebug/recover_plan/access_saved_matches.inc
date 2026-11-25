static bool access_saved_matches(struct recover *r, const struct recover_rule *w,
                                 u32 dir, u32 target, u32 value)
{
    const struct recover_target *t = &r->profile->targets[target];
    bool found = false;
    for (u32 i = 0; i < r->saved_count; i++) {
        const struct recover_reg *d = &r->profile->regs[i];
        if (d->key == target && d->kind == REC_RW && t->access == REC_RW &&
            d->restore_rule_id == w->id &&
            !((value ^ r->saved[i]) & w->write_mask)) found = true;
    }
    if (w->flags & REC_LOCAL) for (u32 i = 0; i < r->local_saved_count; i++) {
        const struct recover_local_saved *v = &r->local_saved[i];
        /* A classified saved DATA needs its own SELECT before replay;
         * the original port index is still restored last. */
        if (dir == REC_SELECT && v->dir_substep == REC_DATA &&
            v->key == target && value == t->index && t->access == REC_RW) found = true;
        if (v->dir_substep == dir && (v->key == target ||
            (dir == REC_SELECT && index_save_safe(r->profile, target) &&
             r->index_port[v->key] == r->index_port[target])) &&
            !((value ^ saved_write_value(t, dir, v->value)) & w->write_mask)) found = true;
        /* Cleanup first unlocks the saved lock, then restores DATA,
         * then replays its saved lock state before original indices. */
        if (v->key == target && v->dir_substep == REC_DATA &&
            cr1f_data(t, dir) && value == RECOVER_CR1F_UNLOCK) found = true;
    }
    return found;
}
