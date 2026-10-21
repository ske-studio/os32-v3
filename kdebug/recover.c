#include "recover.h"
#include "../lib/crc32_core.inc"

/* CRC words have canonical little-endian encoding, never C padding/pointers. */
static u32 crc_word(u32 crc, u32 word)
{
    u8 bytes[4];
    for (u32 i = 0; i < sizeof(bytes); i++) bytes[i] = (u8)(word >> (i * 8));
    return crc32_core_update(crc, bytes, sizeof(bytes));
}

u32 recover_profile_crc(const struct recover_profile *p)
{
    u32 c = CRC32_INIT;
    c = crc_word(c, p->version);
    c = crc_word(c, p->count);
    for (u32 i = 0; i < p->count; i++) {
        const struct recover_reg *d = &p->regs[i];
        c = crc_word(c, d->key); c = crc_word(c, d->width);
        c = crc_word(c, d->write_mask); c = crc_word(c, d->verify_mask);
        c = crc_word(c, d->kind); c = crc_word(c, d->read_safe);
    }
    c = crc_word(c, p->quiesce_count);
    for (u32 i = 0; i < p->quiesce_count; i++) {
        const struct recover_write *w = &p->quiesce[i];
        c = crc_word(c, w->key); c = crc_word(c, w->width);
        c = crc_word(c, w->value); c = crc_word(c, w->verify_mask);
    }
    c = crc_word(c, p->trial_limit); c = crc_word(c, p->opcode_limit);
    c = crc_word(c, p->restore_limit); c = crc_word(c, p->max_steps);
    c = crc_word(c, p->max_polls); c = crc_word(c, p->cold_required);
    return crc32_core_final(c);
}

static u32 snapshot_crc(const struct recover *r)
{
    u32 c = crc_word(CRC32_INIT, r->token.session);
    c = crc_word(c, r->token.profile_crc);
    c = crc_word(c, r->token.profile_version);
    c = crc_word(c, r->token.plan_hash);
    const struct recover_identity *id = &r->token.identity;
    c = crc_word(c, id->boot); c = crc_word(c, id->image_version);
    c = crc_word(c, id->kdebug_version); c = crc_word(c, id->bdf);
    c = crc_word(c, id->device); c = crc_word(c, id->subsys);
    c = crc_word(c, id->revision); c = crc_word(c, id->bar0);
    c = crc_word(c, r->saved_count);
    for (u32 i = 0; i < r->saved_count; i++) c = crc_word(c, r->saved[i]);
    return crc32_core_final(c);
}

static bool same_identity(const struct recover_identity *a,
                          const struct recover_identity *b)
{
    return a->boot == b->boot && a->image_version == b->image_version &&
        a->kdebug_version == b->kdebug_version && a->bdf == b->bdf &&
        a->device == b->device && a->subsys == b->subsys &&
        a->revision == b->revision && a->bar0 == b->bar0;
}

static bool token_matches(const struct recover *r, const struct recover_token *t)
{
    return t && t->session == r->token.session &&
        t->profile_crc == r->token.profile_crc &&
        t->profile_version == r->token.profile_version &&
        t->snapshot_crc == r->token.snapshot_crc &&
        t->plan_hash == r->token.plan_hash &&
        same_identity(&t->identity, &r->token.identity);
}

static void fail(struct recover *r, enum recover_reason reason,
                 enum recover_phase phase, u32 key)
{
    if (!r->failed) {
        r->reason = reason; r->first_phase = phase; r->first_key = key;
    }
    r->failed = true;
}

static int state(struct recover *r, enum recover_state s)
{
    r->state = s;
    if (r->ops->event && r->ops->event(r->opaque, s, r->token.session)) {
        fail(r, REC_LOG, REC_PHASE_NONE, 0);
        return -1;
    }
    return 0;
}

void recover_init(struct recover *r, const struct recover_ops *ops, void *opaque,
                  bool enabled, bool route_write_approved)
{
    *r = (struct recover){ .ops = ops, .opaque = opaque, .enabled = enabled,
        .route_write_approved = route_write_approved, .state = REC_IDLE };
}

static bool finite(u32 n) { return n && n < RECOVER_HALF_TICKS; }
static bool width_ok(u32 n) { return n == 1 || n == 2 || n == 4; }
static u32 width_mask(u32 n) { return n == 4 ? ~0UL : (1UL << (n * 8)) - 1; }

static bool profile_ok(const struct recover_profile *p, u32 budget)
{
    if (!p || !p->version || !p->regs || !p->count ||
        p->count > RECOVER_MAX_REGS || p->quiesce_count > RECOVER_MAX_REGS ||
        (p->quiesce_count && !p->quiesce) || !finite(budget) ||
        !finite(p->trial_limit) || budget > p->trial_limit ||
        !finite(p->opcode_limit) || !finite(p->restore_limit) ||
        !finite(p->max_steps) || !finite(p->max_polls)) return false;
    for (u32 i = 0; i < p->count; i++) {
        const struct recover_reg *d = &p->regs[i];
        if (!width_ok(d->width) || !d->read_safe ||
            (d->kind != REC_RW && d->kind != REC_COLD) ||
            ((d->write_mask | d->verify_mask) & ~width_mask(d->width))) return false;
    }
    for (u32 i = 0; i < p->quiesce_count; i++) {
        const struct recover_write *w = &p->quiesce[i];
        if (!width_ok(w->width) ||
            ((w->value | w->verify_mask) & ~width_mask(w->width))) return false;
    }
    return p->crc == recover_profile_crc(p);
}

/* Called only while a kdebug_call is active; unsigned deltas survive wrap.
 * Saturation prevents a long-running failing callback from wrapping spent. */
static void account(struct recover *r)
{
    u32 now = r->ops->ticks(r->opaque), delta = now - r->call_tick;
    r->call_tick = now;
    if (delta >= r->budget - r->spent) r->spent = r->budget;
    else r->spent += delta;
}

static bool restore_time(struct recover *r)
{
    if ((u32)(r->ops->ticks(r->opaque) - r->restore_tick) < r->profile->restore_limit)
        return true;
    fail(r, REC_TIMEOUT, REC_PHASE_RESTORE, 0);
    return false;
}

static bool config_ok(struct recover *r)
{
    u32 command, bar;
    if (r->ops->raw_config(r->opaque, &command, &bar) ||
        !(command & RECOVER_COMMAND_MEM) || bar != r->token.identity.bar0) {
        fail(r, REC_CONFIG, REC_PHASE_QUIESCE, 0);
        return false;
    }
    return true;
}

static bool write_verify(struct recover *r, const struct recover_write *w,
                         enum recover_phase phase, bool restoring, u32 expected)
{
    u32 actual;
    if (restoring && !restore_time(r)) return false;
    if (r->ops->write(r->opaque, w->key, w->width, w->value)) {
        fail(r, REC_BUS, phase, w->key); return false;
    }
    if (restoring && !restore_time(r)) return false;
    if (r->ops->read(r->opaque, w->key, w->width, &actual) ||
        ((actual ^ expected) & w->verify_mask)) {
        fail(r, REC_VERIFY, phase, w->key); return false;
    }
    return true;
}

static bool quiesce(struct recover *r, bool restoring)
{
    if (!config_ok(r)) return false;
    for (u32 i = 0; i < r->profile->quiesce_count; i++) {
        if (!restoring) {
            account(r);
            if (r->spent >= r->budget) {
                fail(r, REC_TIMEOUT, REC_PHASE_QUIESCE, 0); return false;
            }
        }
        if (!write_verify(r, &r->profile->quiesce[i], REC_PHASE_QUIESCE,
                          restoring, r->profile->quiesce[i].value)) return false;
    }
    return true;
}

static void screen(struct recover *r)
{
    u32 route;
    if (!restore_time(r)) return;
    if (r->ops->route_read(r->opaque, &route)) {
        fail(r, REC_BUS, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
    }
    if (route & RECOVER_ROUTE_MASK) {
        if (!r->route_write_approved) {
            fail(r, REC_CONTRACT, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
        }
        if (!restore_time(r)) return;
        if (r->ops->route_write(r->opaque, RECOVER_ROUTE_98) ||
            r->ops->route_read(r->opaque, &route) || (route & RECOVER_ROUTE_MASK)) {
            fail(r, REC_VERIFY, REC_PHASE_ROUTE, RECOVER_ROUTE_PORT); return;
        }
    }
    if (!restore_time(r)) return;
    if (r->ops->screen_safe(r->opaque)) {
        fail(r, REC_VERIFY, REC_PHASE_SCREEN, 0); return;
    }
    if (!restore_time(r)) return;
    if (r->ops->screen_restore_verify(r->opaque)) {
        fail(r, REC_VERIFY, REC_PHASE_SCREEN, 0); return;
    }
    if (restore_time(r)) r->s0_verified = true;
}

static int cleanup(struct recover *r)
{
    r->in_call = false; r->in_opcode = false;
    r->restore_tick = r->ops->ticks(r->opaque);
    state(r, REC_RESTORING);
    bool safe = !r->access_blocked && quiesce(r, true);
    /* A partial snapshot is usable only for its completed safe entries. Never
     * execute trial writes unless valid; preparation failure still cleans up. */
    if (safe && !r->profile->cold_required) {
        for (u32 i = 0; i < r->saved_count; i++) {
            const struct recover_reg *d = &r->profile->regs[i];
            if (d->kind == REC_COLD) continue;
            u32 actual;
            if (!restore_time(r)) break;
            if (r->ops->read(r->opaque, d->key, d->width, &actual)) {
                fail(r, REC_BUS, REC_PHASE_RESTORE, d->key); break;
            }
            struct recover_write w = { d->key, d->width,
                (actual & ~d->write_mask) | (r->saved[i] & d->write_mask),
                d->verify_mask };
            if (!write_verify(r, &w, REC_PHASE_RESTORE, true, r->saved[i])) break;
        }
    }
    screen(r);
    r->s1_verified = !r->failed && !r->cold_latched && r->valid;
    state(r, r->failed ? REC_FAILED : REC_VERIFIED);
    /* A final log failure cannot leave a successful state. */
    if (r->failed) r->state = REC_FAILED;
    return r->failed ? -1 : 0;
}

static bool live_matches(struct recover *r)
{
    struct recover_identity id;
    return r->valid && r->token.snapshot_crc == snapshot_crc(r) &&
        r->profile->crc == r->token.profile_crc &&
        r->profile->version == r->token.profile_version &&
        recover_profile_crc(r->profile) == r->token.profile_crc &&
        !r->ops->identity(r->opaque, &id) && same_identity(&id, &r->token.identity);
}

int trial_begin(struct recover *r, const struct recover_profile *p,
                u32 plan_hash, u32 budget)
{
    if (!r->enabled || (r->state != REC_IDLE && r->state != REC_VERIFIED) ||
        r->cold_latched || !profile_ok(p, budget) ||
        r->ops->start_ready(r->opaque)) return -1;
    struct recover_identity id;
    if (r->ops->identity(r->opaque, &id) || r->next_session == ~0UL) return -1;
    r->profile = p;
    r->token = (struct recover_token){ .session = ++r->next_session,
        .profile_crc = p->crc, .profile_version = p->version,
        .plan_hash = plan_hash, .identity = id };
    r->saved_count = 0; r->valid = false; r->failed = false;
    r->access_blocked = false;
    r->first_phase = REC_PHASE_NONE; r->first_key = 0; r->reason = REC_NORMAL;
    r->budget = budget; r->spent = 0; r->steps = 0; r->polls = 0;
    r->s0_verified = false; r->s1_verified = false;
    r->in_call = true; r->call_tick = r->ops->ticks(r->opaque);
    r->cold_latched = p->cold_required;
    if (state(r, REC_SNAPSHOTTING)) return cleanup(r);
    for (u32 i = 0; i < p->count; i++) {
        const struct recover_reg *d = &p->regs[i];
        if (r->ops->read(r->opaque, d->key, d->width, &r->saved[i])) {
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
    if (!quiesce(r, false)) { r->access_blocked = true; return cleanup(r); }
    account(r);
    if (r->spent >= r->budget) {
        fail(r, REC_TIMEOUT, REC_PHASE_SAVE, 0); return cleanup(r);
    }
    if (state(r, REC_ARMED) || state(r, REC_RUNNING)) return cleanup(r);
    if (recover_safe_point(r)) return -1;
    r->in_call = false;
    if (state(r, REC_PAUSED)) return cleanup(r);
    return 0;
}

int recover_safe_point(struct recover *r)
{
    if (!r->in_call || r->state != REC_RUNNING) return -1;
    account(r);
    if (r->spent >= r->budget ||
        (r->in_opcode && ((u32)(r->call_tick - r->opcode_tick) >=
          r->profile->opcode_limit || ++r->polls > r->profile->max_polls))) {
        fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0); return cleanup(r), -1;
    }
    if (r->ops->abort_requested &&
        r->ops->abort_requested(r->opaque, r->token.session)) {
        r->reason = REC_ABORT; cleanup(r); return -1;
    }
    return 0;
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
    r->opcode_tick = r->ops->ticks(r->opaque);
    r->in_call = true; r->in_opcode = true; r->polls = 0;
    if (state(r, REC_RUNNING)) return cleanup(r);
    if (++r->steps > r->profile->max_steps) {
        fail(r, REC_TIMEOUT, REC_PHASE_NONE, 0); return cleanup(r);
    }
    if (recover_safe_point(r)) return -1;
    int rc = operation(r, arg);
    if (r->state != REC_RUNNING) return -1; /* safe point already cleaned up */
    if (rc) { fail(r, REC_OPERATION, REC_PHASE_NONE, 0); return cleanup(r); }
    if (recover_safe_point(r)) return -1;
    r->in_call = false; r->in_opcode = false;
    if (state(r, REC_PAUSED)) return cleanup(r);
    return 0;
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
