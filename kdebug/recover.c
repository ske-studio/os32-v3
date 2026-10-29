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

static void discard(struct recover *r, u32 n)
{
    if (n > ~0U - r->discarded_bytes) r->discarded_bytes = ~0U;
    else r->discarded_bytes += n;
}
static u32 rx_word(const u8 *p)
{
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}
int recover_receive(struct recover *r)
{
    if (!r->transport || !r->transport->get) return 0;
    bool abort = false;
    for (u32 n = 0; n < RECOVER_RX_LIMIT; n++) {
        int ch = r->transport->get(r->transport->opaque);
        if (ch < 0) break;
        r->rx[r->rx_used++] = (u8)ch;
        while (r->rx_used && (r->rx[0] != RECOVER_ABORT_SYNC0 ||
            (r->rx_used > 1 && r->rx[1] != RECOVER_ABORT_SYNC1) ||
            r->rx_used == RECOVER_ABORT_SIZE)) {
            if (r->rx_used == RECOVER_ABORT_SIZE &&
                r->rx[0] == RECOVER_ABORT_SYNC0 && r->rx[1] == RECOVER_ABORT_SYNC1 &&
                rx_word(r->rx + RECOVER_ABORT_SESSION_OFF) == r->token.session && r->token.session &&
                rx_word(r->rx + RECOVER_ABORT_CRC_OFF) == crc32_core_final(crc32_core_update(CRC32_INIT, r->rx, RECOVER_ABORT_CRC_OFF))) {
                r->rx_used = 0; abort = true; break;
            }
            discard(r, 1);
            --r->rx_used;
            for (u32 i = 0; i < r->rx_used; i++) r->rx[i] = r->rx[i + 1];
        }
    }
    return abort;
}
/* Command boundaries discard a partial frame and all already queued bytes.
 * At 9600bps this is far more than can arrive during the bounded drain. The
 * host must await EOT before sending the next command; it never retries abort. */
static void receive_boundary(struct recover *r)
{
    discard(r, r->rx_used); r->rx_used = 0;
    if (!r->transport || !r->transport->get) return;
    for (u32 i = 0; i < RECOVER_RX_DRAIN; i++) {
        if (r->transport->get(r->transport->opaque) < 0) return;
        discard(r, 1);
    }
    fail(r, REC_CONTRACT, REC_PHASE_NONE, 0);
}

struct rec_row { char data[RECOVER_ROW_MAX]; u32 n, overflow; };
static void row_text(struct rec_row *b, const char *s)
{
    while (*s) {
        if (b->n >= sizeof(b->data) - 1) { b->overflow = 1; return; }
        b->data[b->n++] = *s++;
    }
}
static void row_number(struct rec_row *b, const char *name, u32 v)
{
    char digits[10]; u32 n = 0;
    row_text(b, name);
    do { digits[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) { char digit[2] = {digits[--n], 0}; row_text(b, digit); }
}
static void row_start(struct recover *r, struct rec_row *b, const char *phase, const char *event)
{
    b->n = 0; b->overflow = 0;
    row_number(b, "REC sid=", r->token.session);
    row_number(b, " seq=", ++r->seq);
    row_text(b, " phase="); row_text(b, phase);
    row_text(b, " event="); row_text(b, event);
}
static int row_send(struct recover *r, struct rec_row *b)
{
    const struct recover_transport *t = r->transport;
    if (!t) return 0;
    u32 max_polls = gf_stage_c_running(r) ? GF_C_TX_POLLS : t->max_polls;
    txd.active = 1; tx_pos = tx_sent = 0; tx_where = REC_TX_BUILD;
    row_text(b, "\n");
    if (b->overflow) {
        tx_fail(r, REC_TX_ROW, REC_TX_OVERFLOW); txd.active = 0;
        r->lost_lines++; fail(r, REC_LOG, REC_PHASE_NONE, 0); return -1;
    }
    /* Close the preceding execution interval before attributing UART time. */
    bool timed = r->log_limit && r->in_call;
    if (timed) account(r);
    r->emitting = true;
    tx_where = REC_TX_NOWHERE;
    int rc = t->begin ? t->begin(t->opaque) : 0;
    if (rc) tx_fail(r, REC_TX_BEGIN, rc);
    bool owned = !rc;
    /* A truncated row must not absorb the next restore record. Charge the
     * separator to the same checked UART allocation as that next record. */
    if (!rc && r->log_partial) {
        tx_pos = tx_sent = 0; tx_where = REC_TX_SEPARATOR;
        rc = recover_uart_byte(r, &t->uart, t->opaque, '\n', t->frame_us, max_polls);
        if (!rc) r->log_partial = false;
    }
    for (u32 i = 0; !rc && i < b->n; i++) {
        tx_pos = i + 1; tx_sent = 0; tx_where = REC_TX_BYTE;
        if (recover_uart_byte(r, &t->uart, t->opaque, (u8)b->data[i], t->frame_us, max_polls)) {
            rc = -1; break;
        }
    }
    tx_pos = b->n + 1; tx_sent = 0; tx_where = REC_TX_FLUSH;
    if (owned && t->end && t->end(r, t->opaque, rc)) {
        tx_fail(r, REC_TX_EMPTY, -1); rc = -1;
    }
    txd.active = 0;
    if (timed) account(r);
    r->emitting = false;
    if (rc) r->log_partial = true;
    if (rc) { r->lost_lines++; fail(r, REC_LOG, REC_PHASE_NONE, 0); }
    return rc ? -1 : 0;
}
int recover_record(struct recover *r, const char *phase, const char *event,
                   u32 step, u32 addr, int rc, u32 elapsed)
{
    if (!r->transport) return 0;
    struct rec_row b;
    row_start(r, &b, phase, event);
    if (event[0] == 'B') {
        row_number(&b, " step=", step); row_number(&b, " addr=", addr);
    } else {
        row_text(&b, " rc=");
        if (rc < 0) row_text(&b, "-");
        row_number(&b, "", rc < 0 ? 0U - (u32)rc : (u32)rc);
        row_number(&b, " elapsed=", elapsed);
    }
    return row_send(r, &b);
}
int recover_gf_pre(struct recover *r, const struct recover_plan_node *node,
                   const char *event, bool executing, int rc)
{
    if (!r->transport) return 0;
    struct rec_row b;
    row_start(r, &b, "gf_pre", event);
    row_number(&b, " main=", node->main_id); row_number(&b, " pc=", node->pc);
    row_number(&b, " opcode=", node->opcode); row_number(&b, " exec=", executing);
    row_number(&b, " substep=", node->substep);
    if (event[0] != 'B') {
        row_text(&b, " rc="); if (rc < 0) row_text(&b, "-");
        row_number(&b, "", rc < 0 ? 0U - (u32)rc : (u32)rc);
    }
    return row_send(r, &b);
}
int recover_gf_result(struct recover *r, u32 saved_words, bool minimal,
                      bool capacity_known, bool full_capacity, u32 capacity_bytes,
                      u32 tested_bytes)
{
    if (!r->transport) return 0;
    struct rec_row b;
    row_start(r, &b, "gf_verify", "END");
    row_number(&b, " saved_words=", saved_words); row_number(&b, " minimal=", minimal);
    row_number(&b, " capacity_known=", capacity_known);
    row_number(&b, " full_capacity=", full_capacity);
    row_number(&b, " capacity_bytes=", capacity_bytes);
    row_number(&b, " tested_bytes=", tested_bytes);
    return row_send(r, &b);
}
static int gf_bus_record(struct recover *r, const struct recover_bus_access *a,
                         const char *event, u32 old, u32 value, u32 actual, int rc, bool mismatch)
{
    if (!r->transport) return 0;
    struct rec_row b;
    u32 pc = r->entered && r->plan && r->cursor < r->plan->node_count ?
             r->plan->nodes[r->cursor].pc : RECOVER_NONE;
    row_start(r, &b, "gf_bus", event);
    row_number(&b, " pc=", pc); row_number(&b, " bar=", a->bar);
    row_number(&b, " off=", a->offset); row_number(&b, " width=", a->width);
    if (event[0] == 'B') {
        row_number(&b, " old=", old); row_number(&b, " new=", value);
    } else {
        row_number(&b, " readback=", actual);
        if (mismatch) {
            const struct recover_target *t = &r->profile->targets[a->key];
            row_number(&b, " mismatch=", 1);
            row_number(&b, " space=", t->space);
            row_number(&b, " port=", t->port);
            row_number(&b, " index=", t->index);
        }
        row_text(&b, " rc="); if (rc < 0) row_text(&b, "-");
        row_number(&b, "", rc < 0 ? 0U - (u32)rc : (u32)rc);
    }
    return row_send(r, &b);
}
static int mode_vram_record(struct recover *r, const char *event, u32 dir,
                             u32 bad_offset, u32 expected, u32 actual, int rc)
{
    if (!r->transport) return 0;
    struct rec_row b;
    row_start(r, &b, "gf_bus", event);
    row_number(&b, " pc=", RECOVER_NONE);
    row_number(&b, " bar=", GF_MODE_VRAM_BAR);
    row_number(&b, " off=", GF_MODE_VRAM_OFFSET);
    row_number(&b, " len=", GF_MODE_VRAM_BYTES);
    row_number(&b, " dir=", dir);
    if (event[0] == 'E') {
        row_number(&b, " bad_off=", bad_offset);
        row_number(&b, " expected=", expected); row_number(&b, " actual=", actual);
        row_text(&b, " rc="); if (rc < 0) row_text(&b, "-");
        row_number(&b, "", rc < 0 ? 0U - (u32)rc : (u32)rc);
    }
    return row_send(r, &b);
}
static void transport_diag(struct recover *r)
{
    if (txd.attempted) return;
    txd.attempted = 1;
    struct rec_row b;
    row_start(r, &b, "diag", "TX");
    row_number(&b, " fail_seq=", txd.seq); row_number(&b, " pos=", txd.pos);
    row_text(&b, " fn="); row_text(&b, recover_diag_fn(txd.fn));
    row_text(&b, " rc="); if (txd.rc < 0) row_text(&b, "-");
    row_number(&b, "", txd.rc < 0 ? 0U - (u32)txd.rc : (u32)txd.rc);
    row_text(&b, " where="); row_text(&b, recover_diag_where(txd.where));
    row_number(&b, " sent=", txd.sent); row_number(&b, " polls=", txd.polls);
    row_number(&b, " bytes=", txd.bytes);
    (void)row_send(r, &b);
}
static int transport_state(struct recover *r, enum recover_state s)
{
    if (!r->transport) return 0;
    struct rec_row b;
    if (s == REC_VERIFIED || s == REC_FAILED) {
        if (r->discarded_bytes) {
            row_start(r, &b, "receive", "DISCARD");
            row_number(&b, " discarded_bytes=", r->discarded_bytes);
            (void)row_send(r, &b);
        }
        transport_diag(r);
        row_start(r, &b, "final", "END");
        row_text(&b, r->failed ? " result=FAIL" : r->s1_verified ? " result=OK" : " result=PARTIAL");
        row_text(&b, " target=S0");
        row_number(&b, " verified=", r->s0_verified);
        row_number(&b, " unknown=", !r->s1_verified);
        row_number(&b, " override=", r->command_override);
        row_number(&b, " saved=", r->command_saved);
        row_number(&b, " actual=", r->command_actual);
        row_number(&b, " trial=", r->reason);
        row_number(&b, " lost_lines=", r->lost_lines);
        row_number(&b, " discarded_bytes=", r->discarded_bytes);
        row_number(&b, " txd=", txd.valid);
    } else {
        static const char *const names[] = {"IDLE", "SNAPSHOTTING", "ARMED", "RUNNING", "PAUSED", "RESTORING"};
        row_start(r, &b, "state", names[s]);
        row_number(&b, " version=", r->token.profile_version);
        row_number(&b, " profile_crc=", r->token.profile_crc);
        row_number(&b, " plan_hash=", r->token.plan_hash);
        row_number(&b, " snapshot_crc=", r->token.snapshot_crc);
        if (s == REC_RESTORING) row_number(&b, " cleanup_seconds=", cb.seconds);
    }
    return row_send(r, &b);
}

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

/* Called only while a kdebug_call is active; unsigned deltas survive wrap.
 * Saturation prevents a long-running failing callback from wrapping spent. */
static void account(struct recover *r)
{
    u32 now = r->ops->ticks(r->opaque), delta = now - r->call_tick;
    r->call_tick = now;
    if (r->log_limit && r->emitting) {
        if (delta >= r->log_limit - r->tx_ticks) r->tx_ticks = r->log_limit;
        else r->tx_ticks += delta;
        return;
    }
    if (delta >= r->budget - r->spent) r->spent = r->budget;
    else r->spent += delta;
}

static bool restore_time(struct recover *r)
{
    u32 limit = cb.ticks;
    u32 polls = cb.polls;
    if (!r->in_s0) {
        limit -= cb.s0_ticks;
        polls -= cb.s0_polls;
    }
    if ((u32)(r->ops->ticks(r->opaque) - r->restore_tick) < limit &&
        r->restore_polls < polls) return true;
    tx_fail(r, REC_TX_BUDGET, r->restore_polls >= polls ? REC_TX_POLLS : REC_TX_TIME);
    fail(r, REC_TIMEOUT, REC_PHASE_RESTORE, 0);
    return false;
}

int recover_restore_point(struct recover *r)
{
    if (r->state != REC_RESTORING || !restore_time(r)) return -1;
    r->restore_polls++;
    return 0;
}

/* The initial TxRDY check is part of this character's poll, not an extra
 * poll. Each unsuccessful frame consumes one more unit. No serial driver is
 * called here; T3 supplies these bounded UART callbacks. */
static int tx_point(struct recover *r)
{
    int rc = r->state == REC_RESTORING ? recover_restore_point(r) : call_point(r, false);
    if (rc) tx_fail(r, REC_TX_BUDGET, REC_TX_POINT);
    return rc;
}
static int tx_time(struct recover *r)
{
    int rc = r->state == REC_RESTORING ? !restore_time(r) : check_deadline(r);
    if (rc) tx_fail(r, REC_TX_BUDGET, REC_TX_TIME);
    return rc;
}
int recover_uart_byte(struct recover *r, const struct recover_uart_ops *ops,
                      void *opaque, u32 byte, u32 frame_us, u32 max_polls)
{
    if (!ops || !ops->ready || !ops->put || !ops->wait_frame ||
        !frame_us || !max_polls || byte > 255) return -1;
    for (;;) {
        if (r->uart_polls >= max_polls) {
            tx_fail(r, REC_TX_BUDGET, REC_TX_POLLS); break;
        }
        if (r->state == REC_RESTORING) {
            u32 bytes = cb.bytes;
            if (!r->in_s0) bytes -= cb.s0_bytes;
            if (r->uart_bytes >= bytes) {
                tx_fail(r, REC_TX_BUDGET, REC_TX_BYTES); break;
            }
        }
        if (tx_point(r)) return -1;
        r->uart_polls++;
        int ready = ops->ready(opaque);
        if (ready < 0) { tx_fail(r, REC_TX_READY, ready); break; }
        if (tx_time(r)) return -1;
        if (ready) {
            r->uart_bytes++;
            tx_sent = 0;
            int rc = ops->put(opaque, byte);
            if (rc) { tx_fail(r, REC_TX_PUT, rc); break; }
            tx_sent = 1;
            if (tx_where != REC_TX_SEPARATOR) tx_where = REC_TX_AFTER_PUT;
            if (tx_time(r)) return -1;
            rc = ops->wait_frame(opaque, frame_us);
            if (rc) { tx_wait_fail(r, rc); break; }
            return tx_time(r);
        }
        if (tx_where != REC_TX_SEPARATOR) tx_where = REC_TX_BUSY_WAIT;
        tx_sent = 0;
        int rc = ops->wait_frame(opaque, frame_us);
        if (rc) { tx_wait_fail(r, rc); break; }
        if (tx_where != REC_TX_SEPARATOR) tx_where = REC_TX_BYTE;
    }
    fail(r, REC_LOG, REC_PHASE_NONE, 0);
    return -1;
}

int recover_uart_flush(struct recover *r, int (*empty)(void *), void *opaque,
                       u32 frame_us, u32 max_polls)
{
    if (!r->transport || !r->transport->uart.wait_frame || !empty ||
        !frame_us || !max_polls) return -1;
    while (r->uart_polls < max_polls) {
        if (tx_point(r)) return -1;
        r->uart_polls++;
        int ready = empty(opaque);
        if (ready < 0) { tx_fail(r, REC_TX_EMPTY, ready); break; }
        if (tx_time(r)) return -1;
        if (ready) return 0;
        int rc = r->transport->uart.wait_frame(opaque, frame_us);
        if (rc) { tx_wait_fail(r, rc); break; }
    }
    tx_fail(r, REC_TX_BUDGET, REC_TX_POLLS);
    fail(r, REC_LOG, REC_PHASE_NONE, 0);
    return -1;
}

int recover_delay_ms(struct recover *r, u32 ms,
                     int (*wait_us)(void *, u32), void *opaque)
{
    if (!wait_us) return -1;
    for (u32 i=0;i<ms;i++) {
        if (r->state == REC_RESTORING ? recover_restore_point(r) : recover_safe_point(r)) return -1;
        if (wait_us(opaque, RECOVER_US_PER_MS)) {
            fail(r, REC_BUS, REC_PHASE_NONE, 0); return -1;
        }
        if (r->state == REC_RESTORING ? !restore_time(r) : check_deadline(r)) return -1;
    }
    return 0;
}

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
static int mode_vram_access(struct recover *r)
{
    const struct recover_plan_node *n = &r->plan->nodes[r->cursor];
    const struct recover_bar_extent *b = &r->bars[GF_MODE_VRAM_BAR];
    if (r->state != REC_RUNNING || !r->valid || r->accessed || !r->ops->mode_vram ||
        !node_ok(r->profile,r->plan,r->cursor) || !b->valid || b->length < GF_MODE_VRAM_BYTES ||
        (unsigned long long)b->base + GF_MODE_VRAM_BYTES > RECOVER_ADDRESS_END ||
        !raw_matches(r,true)) return access_error(r,r->cursor);
    u32 dir = r->plan->args[n->arg_first + 3].value;
    u32 bad_offset = RECOVER_NONE, expected = RECOVER_NONE, actual = RECOVER_NONE;
    if (mode_vram_record(r,"BEGIN",dir,bad_offset,expected,actual,0) || recover_safe_point(r))
        return access_error(r,r->cursor);
    if (dir == REC_WRITE) r->gpu_written = true;
    int rc = r->ops->mode_vram(r->opaque,r,dir == REC_WRITE,&bad_offset,&expected,&actual);
    if (r->state != REC_RUNNING) return -1; /* safe point already cleaned up */
    bool mismatch = !rc && bad_offset != RECOVER_NONE;
    int log = mode_vram_record(r,"END",dir,bad_offset,expected,actual,rc ? rc : mismatch ? -1 : 0);
    if (rc || (mismatch && !r->verify_record) || log) {
        fail(r,log ? REC_LOG : rc ? REC_BUS : REC_VERIFY,
             REC_PHASE_VERIFY,bad_offset);
        return access_error(r,bad_offset);
    }
    r->accessed = true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
static int mode_color_record(struct recover *r, const char *event, u32 row, int rc)
{
    if (!r->transport) return 0;
    struct rec_row b;
    row_start(r,&b,"gf_bus",event);
    row_number(&b," pc=",RECOVER_NONE);
    row_number(&b," bar=",GF_MODE_REGISTER_BAR);
    row_number(&b," row=",row);
    row_number(&b," off=",row == GF_MODE_ROW_DAC ? GF_MODE_DAC_MASK : GF_MODE_AR_PORT);
    row_number(&b," len=",row == GF_MODE_ROW_DAC ? GF_MODE_DAC_BYTES : 1U);
    row_number(&b," dir=",REC_WRITE);
    if (event[0]=='E') {
        row_text(&b," rc="); if (rc<0) row_text(&b,"-");
        row_number(&b,"",rc<0 ? 0U-(u32)rc : (u32)rc);
    }
    return row_send(r,&b);
}
static int mode_color_access(struct recover *r)
{
    const struct recover_plan_node *n=&r->plan->nodes[r->cursor];
    const struct recover_bar_extent *b=&r->bars[GF_MODE_REGISTER_BAR];
    if (r->state != REC_RUNNING || !r->valid || r->accessed || !r->ops->mode_color ||
        !node_ok(r->profile,r->plan,r->cursor) || !b->valid || b->length<=GF_MODE_DAC_DATA ||
        (unsigned long long)b->base+GF_MODE_DAC_DATA>=RECOVER_ADDRESS_END || !raw_matches(r,true))
        return access_error(r,r->cursor);
    u32 row=r->plan->args[n->arg_first].value, value=r->plan->args[n->arg_first+1].value;
    if (mode_color_record(r,"BEGIN",row,0) || recover_safe_point(r)) return access_error(r,r->cursor);
    r->gpu_written=true;
    int rc=r->ops->mode_color(r->opaque,r,row,value);
    if (r->state != REC_RUNNING) return -1;
    int log=mode_color_record(r,"END",row,rc);
    if (rc || log) {
        fail(r,log ? REC_LOG : REC_BUS,REC_PHASE_VERIFY,row);
        return access_error(r,row);
    }
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
static int mode_check_record(struct recover *r,const char *phase,u32 source,u32 key,
                              u32 mask,u32 expected,u32 actual,int rc)
{
    if (!r->transport) return 0;
    struct rec_row row;
    row_start(r,&row,phase,"END");
    row_number(&row," source=",source); row_number(&row," key=",key);
    row_number(&row," mask=",mask); row_number(&row," expected=",expected);
    row_number(&row," actual=",actual);
    row_text(&row,rc ? " rc=-1" : " rc=0");
    return row_send(r,&row);
}
/* Used by the hardware callback too: 01 is valid only while executing
 * this already-entered, fixed C route operation. S0 still writes only 00. */
bool recover_mode_route_active(const struct recover *r)
{
    return r && r->state==REC_RUNNING && r->valid && r->entered && !r->accessed &&
        r->plan && r->cursor<r->plan->node_count &&
        r->node_layout[r->cursor]->flags==REC_LAYOUT_MODE_ROUTE &&
        node_ok(r->profile,r->plan,r->cursor);
}
static int mode_route_access(struct recover *r)
{
    if (!recover_mode_route_active(r) || !r->ops->route_write || !r->ops->route_read)
        return access_error(r,r->cursor);
    if (recover_record(r,"route","BEGIN",r->cursor,RECOVER_ROUTE_PORT,0,0) ||
        recover_safe_point(r)) return access_error(r,r->cursor);
    u32 actual=RECOVER_NONE;
    int rc=r->ops->route_write(r->opaque,RECOVER_ROUTE_RGB_IN);
    if (!rc && !recover_safe_point(r)) rc=r->ops->route_read(r->opaque,&actual);
    if (r->state!=REC_RUNNING) return -1;
    bool mismatch=!rc && ((actual ^ RECOVER_ROUTE_RGB_IN) & RECOVER_ROUTE_MASK);
    int log=mode_check_record(r,"route",REC_CHECK_ROUTE,RECOVER_NONE,
        RECOVER_ROUTE_MASK,RECOVER_ROUTE_RGB_IN,actual,rc || mismatch);
    if (rc || mismatch || log) {
        fail(r,log ? REC_LOG : rc ? REC_BUS : REC_VERIFY,REC_PHASE_VERIFY,RECOVER_ROUTE_PORT);
        return access_error(r,RECOVER_ROUTE_PORT);
    }
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
/* One place for C's read, masked comparison, evidence and failure exit.
 * Route and cell use fixed callbacks; no route I/O target is added. */
static int mode_check_access(struct recover *r, u32 *actual)
{
    const struct recover_plan_node *n=&r->plan->nodes[r->cursor];
    const struct recover_plan_arg *a=&r->plan->args[n->arg_first];
    if (r->state != REC_RUNNING || !r->valid || r->accessed ||
        !node_ok(r->profile,r->plan,r->cursor)) return access_error(r,r->cursor);
    *actual=RECOVER_NONE;
    u32 source=a[REC_CHECK_SOURCE].value, key=a[REC_CHECK_KEY].value;
    u32 mask=a[REC_CHECK_MASK].value, expected=a[REC_CHECK_EXPECTED].value;
    if (a[REC_CHECK_EXPECTED].kind == REC_ARG_SAVED) {
        if (expected < r->profile->count) expected=r->saved[expected];
        else {
            u32 slot=expected-r->profile->count;
            if (!(r->capture_valid & (1ULL << slot))) return access_error(r,key);
            expected=r->capture_values[slot];
        }
    }
    expected &= a[REC_CHECK_KEEP].value;
    if (recover_safe_point(r)) return -1;
    int rc;
    if (source == REC_CHECK_IDENTITY) {
        struct recover_identity id;
        rc=r->ops->identity(r->opaque,&id);
        if (!rc) *actual=same_identity(&id,&r->token.identity);
    } else if (source == REC_CHECK_ROUTE) rc=r->ops->route_read(r->opaque,actual);
    else if (source == REC_CHECK_CELL) rc=r->ops->mode_cell ? r->ops->mode_cell(r->opaque,r,actual) : -1;
    else {
        const struct recover_target *t=&r->profile->targets[key];
        if (t->space == REC_VGA_INDEXED && r->port_selected[r->index_port[key]] != key)
            return access_error(r,key);
        rc=bus_read(r,key,actual);
    }
    if (r->state != REC_RUNNING) return -1;
    bool mismatch=!rc && ((*actual ^ expected) & mask);
    int log=mode_check_record(r,"check",source,key,mask,expected,*actual,rc || mismatch);
    bool required = !r->verify_record || (n->flags & REC_NODE_CHECK_ENTRY) ||
        source != REC_CHECK_TARGET;
    if (rc || (mismatch && required) || log) {
        fail(r,log ? REC_LOG : rc ? REC_BUS : REC_VERIFY,REC_PHASE_VERIFY,key);
        return access_error(r,key);
    }
    for (u32 i=0;i<r->plan->capture_count;i++)
        if (r->plan->captures[i].consumer_node == r->cursor) r->capture_valid &= ~(1ULL << i);
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
static int mode_wait_access(struct recover *r)
{
    if (r->state != REC_RUNNING || !r->plan || !r->entered || r->accessed ||
        r->cursor >= r->plan->node_count ||
        r->node_layout[r->cursor]->flags != REC_LAYOUT_MODE_WAIT ||
        !node_ok(r->profile,r->plan,r->cursor)) return -1;
    u32 ms=r->plan->args[r->plan->nodes[r->cursor].arg_first].value;
    if (recover_record(r,"wait","BEGIN",r->cursor,ms,0,0)) return access_error(r,r->cursor);
    u32 tick=r->ops->ticks(r->opaque);
    int rc=recover_delay_ms(r,ms,r->ops->mode_wait,r->opaque);
    if (r->state != REC_RUNNING) return -1;
    int log=recover_record(r,"wait","END",r->cursor,ms,rc,r->ops->ticks(r->opaque)-tick);
    if (rc || log) return access_error(r,r->cursor);
    r->accessed=true;
    bit_set(r->buffers.valid_bits,r->cursor);
    return 0;
}
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
