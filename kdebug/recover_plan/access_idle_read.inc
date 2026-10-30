static int access_idle_read(struct recover *r, u32 target, u32 width, u32 *value)
{
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
