/* Records share the owner's sequence. Failed logging stops trial accesses;
 * restoration still uses its bounded independent S0 allocation. */
static const char *log_phase(const struct recover *r)
{
    if (r->state == REC_SNAPSHOTTING) return "snapshot";
    if (r->state == REC_RESTORING) return "restore";
    return "access";
}
static int logged_read(struct recover *r, const struct recover_bus_access *a, u32 *value)
{
    if (!r->transport || r->state == REC_IDLE || r->state == REC_VERIFIED)
        return r->ops->read(r->opaque, a, value);
    u32 tick = r->ops->ticks(r->opaque);
    int log = recover_record(r, log_phase(r), "BEGIN", a->key, a->offset, 0, 0);
    if (log && r->state != REC_RESTORING) return -1;
    if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;
    if (r->state != REC_RESTORING && call_point(r, false)) return -1;
    int rc = r->ops->read(r->opaque, a, value);
    log = recover_record(r, log_phase(r), "END", a->key, a->offset, rc,
                         r->ops->ticks(r->opaque) - tick);
    return rc ? rc : log;
}
static int logged_write(struct recover *r, const struct recover_bus_access *a, u32 value, u32 old)
{
    if (!r->transport) return r->ops->write(r->opaque, a, value);
    u32 tick = r->ops->ticks(r->opaque);
    bool gf = r->state == REC_RUNNING;
    int log = gf ? gf_bus_record(r, a, "BEGIN", old, value, 0, 0, false) :
                   recover_record(r, log_phase(r), "BEGIN", a->key, a->offset, 0, 0);
    if (log && r->state != REC_RESTORING) return -1;
    if (r->state == REC_RESTORING && recover_restore_point(r)) return -1;
    if (r->state != REC_RESTORING && call_point(r, false)) return -1;
    int rc = r->ops->write(r->opaque, a, value);
    if (gf) {
        if (rc) (void)gf_bus_record(r, a, "END", old, value, RECOVER_NONE, rc, false);
        return rc;
    }
    log = recover_record(r, log_phase(r), "END", a->key, a->offset, rc,
                         r->ops->ticks(r->opaque) - tick);
    return rc ? rc : log;
}
static int logged_callback(struct recover *r, const char *phase, int (*fn)(void *))
{
    if (!r->transport) return fn(r->opaque);
    u32 tick = r->ops->ticks(r->opaque);
    (void)recover_record(r, phase, "BEGIN", 0, 0, 0, 0);
    int rc = recover_restore_point(r) ? -1 : fn(r->opaque);
    (void)recover_record(r, phase, "END", 0, 0, rc, r->ops->ticks(r->opaque) - tick);
    return rc;
}
/* value NULL writes RECOVER_ROUTE_98 (step 1); otherwise reads (step 0). */
static int route_call(struct recover *r, u32 *value)
{
    return value ? r->ops->route_read(r->opaque, value)
                 : r->ops->route_write(r->opaque, RECOVER_ROUTE_98);
}
static int logged_route(struct recover *r, u32 *value)
{
    if (!r->transport) return route_call(r, value);
    u32 tick = r->ops->ticks(r->opaque), step = value ? 0 : 1;
    (void)recover_record(r, "route", "BEGIN", step, RECOVER_ROUTE_PORT, 0, 0);
    int rc = recover_restore_point(r) ? -1 : route_call(r, value);
    (void)recover_record(r, "route", "END", step, RECOVER_ROUTE_PORT, rc, r->ops->ticks(r->opaque) - tick);
    return rc;
}

static bool config_ok(struct recover *r)
{
    if (!raw_matches(r, true)) {
        fail(r, REC_CONFIG, REC_PHASE_QUIESCE, 0);
        return false;
    }
    return true;
}

static bool write_verify(struct recover *r, const struct recover_write *w,
                         enum recover_phase phase, bool restoring)
{
    if (restoring && !restore_time(r)) return false;
    u32 value = w->value;
    r->auth_phase = phase == REC_PHASE_RESTORE ? REC_RESTORE : REC_QUIESCE;
    if (recover_access(r, RECOVER_NONE, REC_WRITE, w->key, w->width, &value)) return false;
    return !restoring || restore_time(r);
}

static bool quiesce(struct recover *r, bool restoring)
{
    if (r->measuring) {
        bool measured = true;
        u32 command = 0, bars[RECOVER_BAR_COUNT] = {0};
        if ((r->state == REC_RESTORING && recover_restore_point(r)) ||
        r->ops->raw_config(r->opaque, &command, bars) || command != RECOVER_COMMAND_OVERRIDE)
            measured = false;
        for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
            if (!r->measurement.bar[i].valid || bars[i] != r->measurement.bar[i].raw) measured = false;
        if (!measured) {
            if (r->gpu_written) fail(r, REC_CONFIG, REC_PHASE_QUIESCE, 0);
            return false;
        }
        for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) r->bars[i] = r->measurement.bar[i];
    }
    if (!config_ok(r)) return false;
    for (u32 i = 0; i < r->profile->quiesce_count; i++) {
        if (!restoring) {
            account(r);
            if (r->spent >= r->budget) {
                fail(r, REC_TIMEOUT, REC_PHASE_QUIESCE, 0); return false;
            }
        }
        if (!write_verify(r, &r->profile->quiesce[i], REC_PHASE_QUIESCE, restoring))
            return false;
    }
    return true;
}

static void screen(struct recover *r)
{
    u32 route;
    if (!restore_time(r)) return;
    if (recover_restore_point(r) || logged_route(r, &route)) {
        fail(r, REC_BUS, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
    }
    bool once = route_test_once && route_test_approved;
    if (once) route_test_once = route_test_approved = false;
    if (once || (route & RECOVER_ROUTE_MASK)) {
        if (!once && !r->route_write_approved) {
            fail(r, REC_CONTRACT, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
        }
        if (!r->ops->route_write) {
            fail(r, REC_CONTRACT, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
        }
        if (!restore_time(r)) return;
        if (recover_restore_point(r) || logged_route(r, 0) ||
            recover_restore_point(r) || logged_route(r, &route) || (route & RECOVER_ROUTE_MASK)) {
            fail(r, REC_VERIFY, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
        }
    }
    if (!restore_time(r)) return;
    if (recover_restore_point(r) || logged_callback(r, "decode", r->ops->screen_safe)) {
        fail(r, REC_VERIFY, REC_PHASE_SCREEN, 0); return;
    }
    if (!restore_time(r)) return;
    if (recover_restore_point(r) || logged_callback(r, "screen", r->ops->screen_restore_verify)) {
        fail(r, REC_VERIFY, REC_PHASE_SCREEN, 0); return;
    }
    if (restore_time(r)) r->s0_verified = true;
}

static int cleanup(struct recover *r)
{
    /* Events can unwind through several callers after a safe point cleans up.
     * Only the first entry owns restoration and its deadline/poll budget. */
    if (r->state == REC_RESTORING || r->state == REC_VERIFIED ||
        r->state == REC_FAILED) return -1;
    r->in_call = false; r->in_opcode = false;
    r->cursor = RECOVER_NONE; r->entered = false;
    r->auth_phase = REC_RESTORE;
    r->state = REC_RESTORING;
    if (cleanup_budget_prepare(r)) fail(r, REC_CONTRACT, REC_PHASE_RESTORE, 0);
    r->restore_tick = r->ops->ticks(r->opaque);
    for (u32 i = 0; i < RECOVER_RULE_MAX / BITS_PER_U32; i++) r->restored_rules[i] = 0;
    r->restore_polls = 0; r->uart_polls = 0; r->uart_bytes = 0; r->in_s0 = false;
    state(r, REC_RESTORING);
    if (recover_restore_point(r) || r->ops->local_cleanup(r->opaque)) {
        fail(r, REC_BUS, REC_PHASE_RESTORE, 0); r->access_blocked = true;
    }
    bool safe = !r->s0_only && !r->access_blocked && quiesce(r, true);
    /* A partial snapshot is usable only for its completed safe entries. Never
     * execute trial writes unless valid; preparation failure still cleans up. */
    if (safe) {
        for (u32 i = 0; i < r->saved_count; i++) {
            const struct recover_reg *d = &r->profile->regs[i];
            bool command = r->profile->targets[d->key].space == REC_PCI_CFG &&
                           r->profile->targets[d->key].offset == RECOVER_COMMAND;
            if (d->kind == REC_COLD || (r->profile->cold_required && !command)) continue;
            u32 actual;
            if (!restore_time(r)) break;
            if (bus_read(r, d->key, &actual)) {
                fail(r, REC_BUS, REC_PHASE_RESTORE, d->key); break;
            }
            struct recover_write w = { d->key, d->width,
                (actual & ~d->write_mask) | (r->saved[i] & d->write_mask),
                d->verify_mask, d->restore_rule_id };
            if (r->profile->targets[d->key].space == REC_PCI_CFG &&
                r->profile->targets[d->key].offset == RECOVER_COMMAND) {
                r->command_saved = r->saved[i]; r->command_override = true;
                w.value = RECOVER_COMMAND_OVERRIDE;
            }
            if (!write_verify(r, &w, REC_PHASE_RESTORE, true)) break;
            if (r->command_override && r->profile->targets[d->key].space == REC_PCI_CFG &&
                r->profile->targets[d->key].offset == RECOVER_COMMAND)
                r->command_actual = RECOVER_COMMAND_OVERRIDE;
        }
    }
    if (r->zero_hold && !zero_guards(r)) fail(r, REC_VERIFY, REC_PHASE_VERIFY, 0);
    if (r->measuring) {
        u32 command = 0, bars[RECOVER_BAR_COUNT] = {0};
        bool complete = !r->failed && !recover_restore_point(r) &&
            !r->ops->raw_config(r->opaque, &command, bars) && restore_time(r) &&
            command == RECOVER_COMMAND_OVERRIDE;
        for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
            if (!r->measurement.bar[i].valid || bars[i] != r->measurement.bar[i].raw) complete = false;
        if (complete) r->cache = r->measurement;
        else r->cache = (struct recover_bar_measurement){0};
    }
    /* Finish the first allocation before releasing the reserved S0 budget. */
    restore_time(r);
    r->in_s0 = true;
    screen(r);
    receive_boundary(r);
    r->s1_verified = !r->s0_only && !r->failed && !r->cold_latched && r->valid && !r->command_override;
    state(r, r->failed ? REC_FAILED : REC_VERIFIED);
    /* A final log failure cannot leave a successful state. */
    if (r->failed) { r->state = REC_FAILED; r->s1_verified = false; }
    if (r->failed || r->reason != REC_NORMAL) {
        if (r->measuring) r->cache = (struct recover_bar_measurement){0};
    }
    r->measurement = (struct recover_bar_measurement){0}; r->measuring = false;
    r->local_saved_count = 0;
    if (r->gf_stage && r->gf_stage->initialized) {
        r->gf_stage->s0_ok = r->s0_verified && !r->access_blocked &&
            (r->reason == REC_NORMAL || r->reason == REC_ABORT ||
             (r->reason == REC_VERIFY && !r->c_only));
        if (!r->gf_stage->s0_ok) r->gf_stage->initialized = false;
        r->gf_stage->entry_ok = false;
    }
    release_plan(r);
    return r->failed ? -1 : 0;
}
