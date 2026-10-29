#include "recover.h"
#include "gf_stage_state.h"
#include "recover_wire.h"
#include "recover_diag.h"
#include "../hwdev/gfinit_mode.h"
#include "../lib/crc32_core.inc"

static int cleanup(struct recover *r);
static int check_deadline(struct recover *r);
static void account(struct recover *r);
static bool restore_time(struct recover *r);
static int call_point(struct recover *r, bool count_poll);
static bool bit_get(const u32 *bits, u32 n);
static void bit_set(u32 *bits, u32 n);
static void release_plan(struct recover *r);
static int bus_read(struct recover *r, u32 key, u32 *value);
static bool zero_guards(struct recover *r);
static bool raw_matches(struct recover *r, bool memory);
static void select_record(const struct recover *r, u32 node, u32 *bits);
static bool extent_ok(const struct recover_profile *p, const struct recover_target *t,
                      const struct recover_bar_extent bars[RECOVER_BAR_COUNT]);

/* auth_phase persists through pause/final records; expected_node identifies the
 * transition before trial_step emits RUNNING or charges its first safe point. */
static bool gf_stage_c_running(const struct recover *r)
{
    return r->state!=REC_RESTORING && r->profile && r->profile->expected_device==RECOVER_GF_DEVICE && r->plan &&
        (r->auth_phase==REC_MODE_C ||
         (r->expected_node<r->plan->node_count && r->plan->nodes[r->expected_node].phase==REC_MODE_C));
}

static struct rec_tx_diag txd;
static u32 tx_pos, tx_sent, tx_where;
const struct rec_tx_diag *recover_diag_get(void) { return &txd; }
const char *recover_diag_fn(u32 fn)
{
    static const char *const names[] = {"NONE", "READY", "PUT", "WAIT_FRAME",
        "PIT_WAIT", "BEGIN", "EMPTY", "BUDGET", "ROW"};
    return fn <= REC_TX_ROW ? names[fn] : names[0];
}
const char *recover_diag_where(u32 where)
{
    static const char *const names[] = {"NONE", "BYTE", "BUSY_WAIT", "AFTER_PUT",
        "FLUSH", "SEPARATOR", "BUILD"};
    return where <= REC_TX_BUILD ? names[where] : names[0];
}
static void tx_fail(struct recover *r, u32 fn, int rc)
{
    if (!txd.active || txd.valid) return;
    txd.sid = r->token.session; txd.seq = r->seq; txd.pos = tx_pos;
    txd.fn = fn; txd.rc = rc; txd.where = tx_where; txd.sent = tx_sent;
    txd.polls = r->uart_polls; txd.bytes = r->uart_bytes; txd.valid = 1;
}
static void tx_wait_fail(struct recover *r, int rc)
{
    tx_fail(r, rc <= REC_TX_PIT_CONFIG && rc >= REC_TX_PIT_SAMPLES ?
        REC_TX_PIT_WAIT : REC_TX_WAIT_FRAME, rc);
}

static u32 width_mask(u32 n);
static bool zero_target(const struct recover_target *t);
#include "recover_cleanup.h"

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
    c = crc_word(c, p->expected_bdf); c = crc_word(c, p->expected_device);
    c = crc_word(c, p->count);
    for (u32 i = 0; i < p->count; i++) {
        const struct recover_reg *d = &p->regs[i];
        c = crc_word(c, d->key); c = crc_word(c, d->width);
        c = crc_word(c, d->write_mask); c = crc_word(c, d->verify_mask);
        c = crc_word(c, d->kind); c = crc_word(c, d->read_safe);
        c = crc_word(c, d->restore_rule_id);
    }
    c = crc_word(c, p->quiesce_count);
    for (u32 i = 0; i < p->quiesce_count; i++) {
        const struct recover_write *w = &p->quiesce[i];
        c = crc_word(c, w->key); c = crc_word(c, w->width);
        c = crc_word(c, w->value); c = crc_word(c, w->verify_mask);
        c = crc_word(c, w->rule_id);
    }
    c = crc_word(c, p->trial_limit); c = crc_word(c, p->opcode_limit);
    c = crc_word(c, p->restore_limit); c = crc_word(c, p->max_steps);
    c = crc_word(c, p->max_polls); c = crc_word(c, p->cold_required);
    c = crc_word(c, p->restore_max_polls);
    c = crc_word(c, p->s0_reserve_ticks);
    c = crc_word(c, p->s0_reserve_polls);
    c = crc_word(c, p->restore_log_bytes); c = crc_word(c, p->s0_reserve_bytes);
    c = crc_word(c, p->vram_reserved.base);
    c = crc_word(c, p->vram_reserved.len);
    c = crc_word(c, p->target_count);
    for (u32 i = 0; i < p->target_count; i++) {
        const struct recover_target *d = &p->targets[i];
        c = crc_word(c, d->space);
        c = crc_word(c, d->bar);
        c = crc_word(c, d->offset);
        c = crc_word(c, d->head);
        c = crc_word(c, d->port);
        c = crc_word(c, d->index);
        c = crc_word(c, d->width);
        c = crc_word(c, d->access);
        c = crc_word(c, d->read_safe);
    }
    c = crc_word(c, p->rule_count);
    for (u32 i = 0; i < p->rule_count; i++) {
        const struct recover_rule *d = &p->rules[i];
        c = crc_word(c, d->id);
        c = crc_word(c, d->key);
        c = crc_word(c, d->phase);
        c = crc_word(c, d->main_id);
        c = crc_word(c, d->pc);
        c = crc_word(c, d->substep);
        c = crc_word(c, d->write_mask);
        c = crc_word(c, d->verify_mask);
        c = crc_word(c, d->flags);
        c = crc_word(c, d->value.mode);
        c = crc_word(c, d->value.mask);
        c = crc_word(c, d->value.bits);
        c = crc_word(c, d->value.min);
        c = crc_word(c, d->value.max);
        c = crc_word(c, d->value.expr_id);
        c = crc_word(c, d->value.allowed_count);
        c = crc_word(c, d->requires_count);
        c = crc_word(c, d->guard_count);
        for (u32 j = 0; j < d->value.allowed_count; j++) c = crc_word(c, d->value.allowed[j]);
        for (u32 j = 0; j < d->requires_count; j++) c = crc_word(c, d->requires[j]);
        for (u32 j = 0; j < d->guard_count; j++) {
            const struct recover_guard *g = &d->guards[j];
            c = crc_word(c, g->key);
            c = crc_word(c, g->mask);
            c = crc_word(c, g->expected);
            c = crc_word(c, g->when);
        }
    }
    c = crc_word(c, p->expr_count);
    for (u32 i = 0; i < p->expr_count; i++) {
        const struct recover_expr *d = &p->exprs[i];
        c = crc_word(c, d->id);
        c = crc_word(c, d->argc);
        for (u32 j = 0; j < d->argc; j++) c = crc_word(c, d->kinds[j]);
    }
    c = crc_word(c, p->layout_count);
    for (u32 i = 0; i < p->layout_count; i++) {
        const struct recover_opcode_layout *d = &p->layouts[i];
        c = crc_word(c, d->opcode);
        c = crc_word(c, d->substep);
        c = crc_word(c, d->argc);
        c = crc_word(c, d->dir_substep);
        c = crc_word(c, d->flags);
        for (u32 j = 0; j < d->argc; j++) c = crc_word(c, d->kinds[j]);
    }
    c = crc_word(c, p->index_save_count);
    for (u32 i = 0; i < p->index_save_count; i++) c = crc_word(c, p->index_save_targets[i]);
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
    /* Keep the first diagnostic, but a later S0 failure must revoke re-C. */
    if (r->state == REC_RESTORING && r->gf_stage) {
        r->gf_stage->initialized = false;
        r->gf_stage->s0_ok = false;
    }
    if (reason == REC_NORMAL && (r->entered || r->expected_node != RECOVER_NONE))
        fail(r, REC_CONTRACT, REC_PHASE_NONE, r->expected_node);
    if (!r->failed) {
        r->reason = reason; r->first_phase = phase; r->first_key = key;
    }
    r->failed = true;
}

/* One boot-lifetime owner; test sessions borrow it, never replace it. */
static struct recover owner;
static const struct recover_ops *owner_s0_ops;
static void *owner_s0_opaque;
static const struct recover_profile *owner_s0_profile;
struct recover *recover_owner(void) { return &owner; }
int recover_owner_configure(const struct recover_ops *ops, void *opaque,
    const struct recover_profile *s0_profile, bool route_write_approved)
{
    if (!ops) {
        if (!owner_s0_ops || (owner.state != REC_IDLE &&
            owner.state != REC_VERIFIED && owner.state != REC_FAILED)) return -1;
        release_plan(&owner);
        owner.profile = owner_s0_profile;
        owner.ops = owner_s0_ops; owner.opaque = owner_s0_opaque;
        owner.s0_profile = owner_s0_profile;
        return 0;
    }
    bool reentry = gf_stage_reentry(&owner);
    if (owner.c_only && !reentry) return -1;
    if ((owner.state != REC_IDLE && owner.state != REC_VERIFIED &&
         !(owner.state == REC_FAILED && reentry)) ||
        ((owner.failed || owner.cold_latched) && !reentry) || !s0_profile) return -1;
    if (!owner.ops) {
        owner_s0_ops = ops; owner_s0_opaque = opaque; owner_s0_profile = s0_profile;
    }
    owner.ops = ops; owner.opaque = opaque; owner.enabled = true;
    owner.route_write_approved = route_write_approved;
    owner.s0_profile = s0_profile;
    return 0;
}

#include "recover_common/log.inc"

static bool route_test_once, route_test_approved;

int recover_route_test_session(struct recover *r, u32 session)
{
    int rc = -1;
    route_test_once = route_test_approved = true;
    if (r->state == REC_IDLE && !session && !r->token.session)
        rc = recover_now_session(r, session);
    route_test_once = route_test_approved = false;
    return rc;
}

int recover_now_session(struct recover *r, u32 session)
{
    if (!r->enabled || session != r->token.session) return -1;
    if (r->state != REC_IDLE) return trial_end(r, &r->token, REC_ABORT);
    if (session || !r->s0_profile || !r->ops->ticks || !r->ops->start_ready ||
        !r->ops->local_cleanup || r->ops->start_ready(r->opaque)) return -1;
    const struct recover_profile *p = r->s0_profile;
    if ((p->version != RECOVER_PROFILE_VERSION
#if GF_VPLL_PROBE
        && p->version != RECOVER_PROFILE_VPLL_VERSION
#endif
        ) || p->crc != recover_profile_crc(p) ||
        p->count || p->quiesce_count || !p->restore_limit ||
        p->restore_limit >= RECOVER_HALF_TICKS || !p->restore_max_polls ||
        !p->restore_log_bytes || p->s0_reserve_ticks || p->s0_reserve_polls ||
        p->s0_reserve_bytes) return -1;
    r->profile = p;
    txd = (struct rec_tx_diag){0};
    r->token = (struct recover_token){ .session = ++r->next_session,
        .profile_version = r->profile->version, .profile_crc = r->profile->crc };
    r->s0_only = true; r->reason = REC_ABORT;
    receive_boundary(r);
    return cleanup(r);
}

int recover_restore_session(struct recover *r)
{
    if (!r->enabled || !r->ops || !r->ops->start_ready ||
        r->ops->start_ready(r->opaque)) return -1;
    if (r->state == REC_IDLE) return recover_now_session(r, r->token.session);
    if (r->state == REC_PAUSED) return trial_end(r, &r->token, REC_ABORT);
    if (r->state != REC_VERIFIED && r->state != REC_FAILED) return -1;
    r->state = REC_PAUSED;
    txd = (struct rec_tx_diag){0};
    r->seq = 0; r->lost_lines = 0;
    r->s0_verified = false;
    if (!r->gf_stage || !r->gf_stage->initialized) {
        r->profile = r->s0_profile;
        r->s0_only = true;
        r->saved_count = 0;
    }
    return cleanup(r);
}

static int state(struct recover *r, enum recover_state s)
{
    bool restoring = r->state == REC_RESTORING;
    r->state = restoring ? REC_RESTORING : s;
    if (transport_state(r, s)) return -1;
    if (r->ops->event) {
        if (restoring) {
            if (!restore_time(r)) return -1;
            r->restore_polls++;
        }
        int rc = r->ops->event(r->opaque, s, r->token.session);
        /* A UART safe point may have completed cleanup inside the event. */
        if (r->state != (restoring ? REC_RESTORING : s)) return -1;
        if (rc) {
            fail(r, REC_LOG, REC_PHASE_NONE, 0);
            return -1;
        }
        if (restoring && !restore_time(r)) return -1;
    }
    r->state = s;
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

#include "recover_plan/profile.inc"

#include "recover_common/clock.inc"

#include "recover_common/s0.inc"

#include "recover_common/session.inc"

#include "recover_plan/bind.inc"

#include "recover_plan/access.inc"
#include "recover_gf_stage/c.inc"
#include "recover_plan/step.inc"
static bool range_ok(const struct recover_range *r)
{
    return r->len && (unsigned long long)r->base + r->len <= RECOVER_ADDRESS_END;
}
int recover_bar_probe(struct recover *r, u32 bar, u32 raw, u32 mask,
    const struct recover_range *exclude, u32 exclude_count,
    const struct recover_range *upstream, u32 upstream_count)
{
    u32 base = raw & RECOVER_BAR_MASK, length = ~(mask & RECOVER_BAR_MASK) + 1U;
    if (r->state != REC_RUNNING || r->auth_phase != REC_SIZE_BARS || !r->entered ||
        !r->measuring || bar >= RECOVER_BAR_COUNT || raw != r->bars[bar].raw || (raw & RECOVER_BAR_TYPE_MASK) || (mask & RECOVER_BAR_TYPE_MASK) || !base || !length ||
        (length & (length - 1)) || base % length ||
        (unsigned long long)base + length > RECOVER_ADDRESS_END || exclude_count > RECOVER_RANGE_MAX ||
        upstream_count > RECOVER_RANGE_MAX || (exclude_count && !exclude) || (upstream_count && !upstream)) goto bad;
    for (u32 i = 0; i < exclude_count; i++)
        if (!range_ok(&exclude[i]) || ((unsigned long long)base < (unsigned long long)exclude[i].base + exclude[i].len &&
            (unsigned long long)exclude[i].base < (unsigned long long)base + length)) goto bad;
    for (u32 i = 0; i < upstream_count; i++)
        if (!range_ok(&upstream[i]) || base < upstream[i].base ||
            (unsigned long long)base + length > (unsigned long long)upstream[i].base + upstream[i].len) goto bad;
    r->measurement.bar[bar] = (struct recover_bar_extent){raw, base, length, 1};
    return 0;
bad:
    r->cache = (struct recover_bar_measurement){0};
    r->measurement = (struct recover_bar_measurement){0};
    return access_error(r, bar);
}
