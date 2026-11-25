static bool live_matches(struct recover *r)
{
    struct recover_identity id;
    return r->valid && r->token.snapshot_crc == snapshot_crc(r) &&
        r->profile->crc == r->token.profile_crc &&
        r->profile->version == r->token.profile_version &&
        recover_profile_crc(r->profile) == r->token.profile_crc &&
        !r->ops->identity(r->opaque, &id) && same_identity(&id, &r->token.identity);
}

static int check_deadline(struct recover *r)
{
    account(r);
    if (r->log_limit && r->tx_ticks >= r->log_limit) {
        fail(r, REC_LOG, REC_PHASE_NONE, 0);
        if (!r->emitting) cleanup(r);
        return -1;
    }
    if (r->spent >= r->budget ||
        (r->in_opcode && (r->log_limit ? r->spent - r->opcode_spent :
         (u32)(r->call_tick - r->opcode_tick)) >=
         (gf_stage_c_running(r) ? GF_C_EXEC_SECONDS * PIT_HZ : r->profile->opcode_limit))) {
        fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0);
        if (!r->emitting) cleanup(r);
        return -1;
    }
    return 0;
}

static int pause_call(struct recover *r)
{
    receive_boundary(r);
    if (r->failed) return cleanup(r), -1;
    if (state(r, REC_PAUSED)) return cleanup(r), -1;
    if (check_deadline(r)) return -1;
    r->in_call = false; r->in_opcode = false;
    return 0;
}

int trial_begin(struct recover *r, const struct recover_profile *p,
                u32 plan_hash, u32 budget)
{
    bool reentry = gf_stage_reentry(r);
    if (r->c_only && (!reentry || !p || !p->cold_required ||
        p->expected_device != RECOVER_GF_DEVICE)) return -1;
    if (r->state != REC_IDLE && r->state != REC_VERIFIED &&
        !(r->state == REC_FAILED && reentry)) return -1;
    if (!r->enabled || ((r->failed || r->cold_latched) && !reentry) || !profile_ok(p, budget) ||
        !r->plan || r->bound_profile != p || plan_hash != r->bound_hash ||
        !r->ops->local_cleanup || r->ops->start_ready(r->opaque)) {
        release_plan(r); return -1;
    }
    if (p->expected_device == RECOVER_GF_DEVICE && !r->c_only) {
        struct rec_cleanup_budget maximum;
        if (cleanup_budget_compute(r, p, true, &maximum) ||
            (r->transport && maximum.polls > r->transport->max_polls)) {
            release_plan(r); return -1;
        }
    }
    struct recover_identity id;
    if (r->ops->identity(r->opaque, &id) ||
        (p->expected_device && (id.bdf != p->expected_bdf || id.device != p->expected_device)) ||
        r->next_session == ~0UL) {
        release_plan(r); return -1;
    }
    u32 raw_command, raw_bars[RECOVER_BAR_COUNT] = {0};
    bool raw_failed = r->ops->raw_config(r->opaque, &raw_command, raw_bars) != 0;
    bool cache_ok = !raw_failed && same_identity(&id, &r->cache.identity);
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++)
        if (!r->cache.bar[i].valid || r->cache.bar[i].raw != raw_bars[i]) cache_ok = false;
    if (!cache_ok) r->cache = (struct recover_bar_measurement){0};
    r->measuring = r->plan->nodes[0].phase == REC_SIZE_BARS;
    if (r->measuring) r->cache = (struct recover_bar_measurement){0};
    for (u32 i = 0; i < RECOVER_BAR_COUNT; i++) {
        r->bars[i] = r->cache.bar[i];
        if (r->measuring) r->bars[i].raw = raw_bars[i];
    }
    /* All referenced targets, including snapshot/guards/cleanup, before arm.
     * A1 may carry unused GF descriptors; only its cfg-only plan is required. */
    u32 used[RECOVER_TARGET_MAX / BITS_PER_U32] = {0};
    for (u32 i = 0; i < p->count; i++) bit_set(used, p->regs[i].key);
    for (u32 i = 0; i < r->plan->node_count; i++) {
        const struct recover_plan_node *n = &r->plan->nodes[i];
        const struct recover_opcode_layout *l = layout_for(p, n);
        if (l->flags & REC_LAYOUT_MODE_COLOR) {
            const struct recover_bar_extent *b = &r->bars[GF_MODE_REGISTER_BAR];
            if (!b->valid || b->length <= GF_MODE_DAC_DATA ||
                (unsigned long long)b->base + GF_MODE_DAC_DATA >= RECOVER_ADDRESS_END) {
                release_plan(r); return -1;
            }
        }
        if (l->flags & REC_LAYOUT_MODE_VRAM) {
            const struct recover_bar_extent *b = &r->bars[GF_MODE_VRAM_BAR];
            if (!b->valid || b->length < GF_MODE_VRAM_BYTES ||
                (unsigned long long)b->base + GF_MODE_VRAM_BYTES > RECOVER_ADDRESS_END) {
                release_plan(r); return -1;
            }
        }
        for (u32 j = 0; j < n->arg_count; j++) {
            const struct recover_plan_arg *a = &r->plan->args[n->arg_first + j];
            if ((j == 0 && l->dir_substep != RECOVER_NONE) || a->kind == REC_ARG_READ)
                if (!r->measuring || !is_bar(&p->targets[a->value])) bit_set(used, a->value);
        }
    }
    for (u32 i = 0; i < p->rule_count; i++) {
        const struct recover_rule *w = &p->rules[i];
        bool reached = !r->measuring && (w->phase == REC_RESTORE || w->phase == REC_QUIESCE);
        for (u32 j = 0; j < r->plan->node_count; j++)
            if (r->plan->nodes[j].rule_id == w->id) reached = true;
        if (!reached) continue;
        if (!r->measuring || !is_bar(&p->targets[w->key])) bit_set(used, w->key);
        for (u32 j = 0; j < w->guard_count; j++)
            if (!r->measuring || !is_bar(&p->targets[w->guards[j].key])) bit_set(used, w->guards[j].key);
        if (w->flags & REC_N1) for (u32 j = 0; j < p->target_count; j++)
            if (zero_target(&p->targets[j])) bit_set(used, j);
    }
    for (u32 i = 0; i < p->target_count; i++)
        if (bit_get(used, i) && !extent_ok(p, &p->targets[i], r->bars)) { release_plan(r); return -1; }
    r->s0_only = false;
    r->seq = 0; r->lost_lines = 0; r->discarded_bytes = 0;
    r->profile = p;
    txd = (struct rec_tx_diag){0};
    r->token = (struct recover_token){ .session = ++r->next_session,
        .profile_crc = p->crc, .profile_version = p->version,
        .plan_hash = plan_hash, .identity = id };
    r->saved_count = 0; r->local_saved_count = 0; r->valid = false; r->failed = false;
    r->access_blocked = false; r->zero_hold = false; r->gpu_written = false;
    r->measurement = (struct recover_bar_measurement){ .identity = id, .measurement_session = r->token.session };
    r->first_phase = REC_PHASE_NONE; r->first_key = 0; r->reason = REC_NORMAL;
    r->budget = budget; r->spent = 0; r->steps = 0; r->polls = 0;
    r->tx_ticks = 0; r->opcode_spent = 0;
    for (u32 i = 0; i < RECOVER_RULE_MAX / BITS_PER_U32; i++) r->restored_rules[i] = 0;
    r->s0_verified = false; r->s1_verified = false;
    r->command_override = false; r->command_saved = 0; r->command_actual = 0;
    r->uart_polls = 0; r->uart_bytes = 0;
    r->in_call = true; r->call_tick = r->ops->ticks(r->opaque);
    r->cold_latched = r->cold_latched || p->cold_required;
    receive_boundary(r);
    if (r->failed) return cleanup(r), -1;
    if (state(r, REC_SNAPSHOTTING)) return cleanup(r);
    for (u32 i = 0; i < p->count; i++) {
        const struct recover_reg *d = &p->regs[i];
        if (bus_read(r, d->key, &r->saved[i])) {
            fail(r, REC_SNAPSHOT, REC_PHASE_SAVE, d->key); return cleanup(r);
        }
        r->saved_count++;
        if (d->kind == REC_COLD) r->cold_latched = true;
        account(r);
        if (r->spent >= r->budget) {
            fail(r, REC_TIMEOUT, REC_PHASE_SAVE, d->key); return cleanup(r);
        }
    }
    r->token.snapshot_crc = snapshot_crc(r);
    r->valid = true;
    if (!r->measuring && !quiesce(r, false)) { r->access_blocked = true; return cleanup(r); }
    account(r);
    if (r->spent >= r->budget) {
        fail(r, REC_TIMEOUT, REC_PHASE_SAVE, 0); return cleanup(r);
    }
    if (state(r, REC_ARMED) || state(r, REC_RUNNING)) return cleanup(r);
    if (recover_safe_point(r)) return -1;
    return pause_call(r);
}

static int call_point(struct recover *r, bool count_poll)
{
    /* Events run inside the call while snapshotting, arming and pausing too. */
    if (!r->in_call || (r->state != REC_SNAPSHOTTING && r->state != REC_ARMED &&
        r->state != REC_RUNNING && r->state != REC_PAUSED)) return -1;
    if (check_deadline(r)) return -1;
    if (count_poll && r->in_opcode && ++r->polls > (gf_stage_c_running(r) ? GF_C_EXEC_POLLS : r->profile->max_polls)) {
        fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0); return cleanup(r), -1;
    }
    if (r->emitting) return 0;
    bool wire_abort = recover_receive(r);
    bool abort = wire_abort || (r->ops->abort_requested &&
        r->ops->abort_requested(r->opaque, r->token.session));
    if (check_deadline(r)) return -1;
    if (abort) {
        r->reason = REC_ABORT; cleanup(r); return -1;
    }
    return 0;
}

int recover_safe_point(struct recover *r)
{
    if (r->state != REC_RUNNING) return -1;
    return call_point(r, true);
}

int trial_step(struct recover *r, const struct recover_token *t, bool approved,
               int (*operation)(struct recover *, void *), void *arg)
{
    if (!r->enabled || r->state != REC_PAUSED || !approved || !operation ||
        !token_matches(r, t)) return -1;
    r->call_tick = r->ops->ticks(r->opaque);
    if (!live_matches(r)) {
        r->access_blocked = true;
        fail(r, REC_CONTRACT, REC_PHASE_NONE, 0); return cleanup(r);
    }
    /* N2: start C with its own finite limits, before RUNNING can emit UART.
     * The bound profile and its local/S0 reservation remain unchanged. */
    if (gf_stage_c_running(r) && r->expected_node<r->plan->node_count &&
        r->plan->nodes[r->expected_node].phase==REC_MODE_C) {
        r->budget=GF_C_EXEC_SECONDS * PIT_HZ; r->spent=0;
        r->log_limit=GF_C_TX_SECONDS * PIT_HZ; r->tx_ticks=0;
        r->uart_polls=0;
    }
    r->opcode_tick = r->ops->ticks(r->opaque);
    r->opcode_spent = r->spent;
    r->in_call = true; r->in_opcode = true; r->polls = 0;
    if (state(r, REC_RUNNING)) return cleanup(r);
    if (++r->steps > r->profile->max_steps) {
        fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0); return cleanup(r);
    }
    if (recover_safe_point(r)) return -1;
    int rc = operation(r, arg);
    if (r->state != REC_RUNNING) return -1; /* safe point already cleaned up */
    if (rc) {
        fail(r, r->reason == REC_TIMEOUT ? REC_TIMEOUT : REC_OPERATION, REC_PHASE_NONE, 0);
        return cleanup(r);
    }
    if (recover_safe_point(r)) return -1;
    return pause_call(r);
}

int trial_end(struct recover *r, const struct recover_token *t,
              enum recover_reason reason)
{
    if (!r->enabled || !token_matches(r, t) || !r->token.session) return -1;
    if (r->state == REC_VERIFIED || r->state == REC_FAILED)
        return r->state == REC_FAILED ? -1 : 0;
    if (r->state != REC_RUNNING && r->state != REC_PAUSED) return -1;
    if (!live_matches(r)) {
        r->access_blocked = true;
        fail(r, REC_CONTRACT, REC_PHASE_NONE, 0);
    }
    if (reason == REC_NORMAL && (r->entered || r->expected_node != RECOVER_NONE))
        fail(r, REC_CONTRACT, REC_PHASE_NONE, r->expected_node);
    if (!r->failed) {
        r->reason = reason;
        if (reason != REC_NORMAL && reason != REC_ABORT)
            fail(r, reason, REC_PHASE_NONE, 0);
    }
    return cleanup(r);
}

int recover_request(struct recover *r, const struct recover_token *t,
                    enum recover_request request)
{
    if (!r->enabled) return -1;
    if (request == REC_REQUEST_NOW) return trial_end(r, t, REC_ABORT);
    if (request != REC_REQUEST_STATUS || r->state == REC_PAUSED ||
        r->state == REC_RUNNING || r->state == REC_RESTORING) return -1;
    return 0;
}
