/* Actual row/UART paths: first failure, exact byte position and raw rc. */
static u32 diag_mode, diag_at, diag_puts, diag_releases;
static int diag_ready(void *v) { (void)v; return diag_mode == 1 ? -19 : diag_mode == 4 ? 0 : 1; }
static int diag_put(void *v, u32 ch)
{
    (void)v; (void)ch; diag_puts++;
    if (diag_mode == 9) return diag_puts == 1 ? -22 : t3_put(&wire, ch);
    return (diag_mode == 2 && diag_puts == diag_at) || diag_mode == 6 ? -22 : 0;
}
static int diag_wait(void *v, u32 us)
{ (void)v; (void)us; return diag_mode == 7 ? -17 : diag_mode == 3 || diag_mode == 4 ? -13 : 0; }
static int diag_begin(void *v) { (void)v; return diag_mode == 5 ? -25 : 0; }
static int diag_end(struct recover *core, void *v, int rc)
{ (void)core; (void)v; diag_releases++; return rc; }
static struct recover_transport diag_transport = {
    0, {diag_ready, diag_put, diag_wait}, 0, 1042, 16384, diag_begin, diag_end
};
static void diag_setup(u32 mode)
{
    t3_setup(true); r.state = REC_RUNNING; r.in_call = true;
    r.transport = &diag_transport; txd = (struct rec_tx_diag){0};
    diag_mode = mode; diag_at = 1; diag_puts = diag_releases = 0;
}
static void diag_tests(void)
{
    for (u32 mode = 1; mode <= 7; mode++) {
        if (mode == 6) continue;
        diag_setup(mode);
        u32 seq = r.seq + 1;
        CHECK(recover_record(&r, "access", "BEGIN", 0, 0, 0, 0) < 0);
        CHECK(txd.valid && txd.sid == r.token.session && txd.seq == seq);
        CHECK(txd.pos == (mode == 5 ? 0U : 1U));
        CHECK(txd.fn == (mode == 1 ? REC_TX_READY : mode == 2 ? REC_TX_PUT :
            mode == 5 ? REC_TX_BEGIN : mode == 7 ? REC_TX_WAIT_FRAME : REC_TX_PIT_WAIT));
        CHECK(txd.rc == (mode == 1 ? -19 : mode == 2 ? -22 : mode == 5 ? -25 : mode == 7 ? -17 : -13));
        CHECK(txd.where == (mode == 5 ? REC_TX_NOWHERE : mode == 4 ? REC_TX_BUSY_WAIT :
            (mode == 3 || mode == 7) ? REC_TX_AFTER_PUT : REC_TX_BYTE));
        CHECK(txd.sent == (mode == 3 || mode == 7));
        CHECK(diag_puts == (mode == 2 || mode == 3 || mode == 7));
        CHECK(diag_releases == (mode != 5) && r.lost_lines == 1 && !txd.active);
        struct rec_tx_diag first = txd;
        diag_mode = 2; diag_puts = 0;
        CHECK(recover_record(&r, "restore", "END", 0, 0, 0, 0) < 0);
        CHECK(txd.seq == first.seq && txd.rc == first.rc && txd.pos == first.pos);
    }
    /* First byte, last byte (LF) and an interior event byte are not retried. */
    for (u32 pos = 1; pos <= 3; pos++) {
        diag_setup(0); struct rec_row b;
        row_start(&r, &b, "access", "BEGIN");
        u32 at = pos == 1 ? 1 : pos == 2 ? b.n : b.n + 1;
        diag_mode = 2; diag_at = at;
        CHECK(row_send(&r, &b) < 0 && txd.pos == at && diag_puts == at && txd.sent == 0);
    }
    diag_setup(2); r.log_partial = true;
    CHECK(recover_record(&r, "restore", "END", 0, 0, 0, 0) < 0);
    CHECK(txd.pos == 0 && txd.where == REC_TX_SEPARATOR);
    /* 383 bytes including LF fit; one extra byte is rejected before begin. */
    for (u32 extra = 0; extra < 2; extra++) {
        diag_setup(0); struct rec_row b = { .n = 0, .overflow = 0 };
        for (u32 i = 0; i < RECOVER_ROW_MAX - 2 + extra; i++) row_text(&b, "x");
        CHECK((row_send(&r, &b) == 0) == !extra);
        CHECK(extra ? txd.fn == REC_TX_ROW && txd.rc == -35 && !diag_puts && r.lost_lines == 1 :
            !txd.valid && diag_puts == RECOVER_ROW_MAX - 1);
    }
    diag_setup(0); r.uart_polls = diag_transport.max_polls;
    CHECK(recover_record(&r, "access", "BEGIN", 0, 0, 0, 0) < 0);
    CHECK(txd.fn == REC_TX_BUDGET && txd.rc == -31);
    /* A failed diag is reflected when final is built afterwards, once only. */
    diag_setup(6); r.s0_verified = r.s1_verified = true;
    CHECK(transport_state(&r, REC_VERIFIED) < 0 && txd.attempted && txd.valid);
    CHECK(r.lost_lines == 2);
    diag_setup(9); r.s0_verified = r.s1_verified = true;
    u32 diag_seq = r.seq + 1;
    CHECK(transport_state(&r, REC_VERIFIED) == 0);
    CHECK(txd.seq == diag_seq);
    CHECK(t3_contains("phase=final event=END result=FAIL") && t3_contains("lost_lines=1 discarded_bytes=0 txd=1"));
    diag_setup(0); r.transport = &t3_transport;
    CHECK(transport_state(&r, REC_VERIFIED) == 0);
    CHECK(t3_contains("phase=diag event=TX fail_seq=0 pos=0 fn=NONE rc=0 where=NONE sent=0 polls=0 bytes=0\n"));
    CHECK(t3_contains(" txd=0\n"));
    /* A fresh session clears diagnostics; initialization/owner detach need not. */
    txd.valid = 1; txd.rc = -22;
    t3_setup(true); CHECK(!txd.valid && !txd.attempted);
    pit_fake.valid = 0; CHECK(recover_hal_wait_frame(0, 1042) == REC_TX_PIT_CONFIG);
    pit_fake.valid = 1; pit_step = 0; pit_count_fake = pit_fake.reload + 1;
    CHECK(recover_hal_wait_frame(0, 1042) == REC_TX_PIT_COUNT);
    pit_count_fake = 1;
    CHECK(recover_hal_wait_frame(0, 1042) == REC_TX_PIT_SAMPLES);
}
