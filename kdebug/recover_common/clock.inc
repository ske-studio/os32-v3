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
