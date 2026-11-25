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
static int gf_bus_record_at(struct recover *r, const struct recover_bus_access *a, u32 pc,
                         const char *event, u32 old, u32 value, u32 actual, int rc, bool mismatch)
{
    if (!r->transport) return 0;
    struct rec_row b;
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
static int gf_bus_record(struct recover *r, const struct recover_bus_access *a,
                         const char *event, u32 old, u32 value, u32 actual, int rc, bool mismatch)
{
    u32 pc = r->entered && r->plan && r->cursor < r->plan->node_count ?
             r->plan->nodes[r->cursor].pc : RECOVER_NONE;
    return gf_bus_record_at(r,a,pc,event,old,value,actual,rc,mismatch);
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
