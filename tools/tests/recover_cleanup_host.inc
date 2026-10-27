/* Dynamic cleanup envelope against the expanded, CRC-covered GF profile. */
static u32 cleanup_test_tick, cleanup_test_puts, cleanup_test_rows;
static u32 cleanup_clock(void *v) { (void)v; return cleanup_test_tick; }
static int cleanup_ready(void *v) { (void)v; return 1; }
static int cleanup_put(void *v, u32 ch)
{ (void)v; cleanup_test_puts++; if (ch == '\n') cleanup_test_rows++; return 0; }
static int cleanup_wait(void *v, u32 us) { (void)v; (void)us; return 0; }
static const struct recover_transport cleanup_transport = {
    .uart = {cleanup_ready, cleanup_put, cleanup_wait}, .frame_us = 1042,
    .max_polls = RECOVER_CLEANUP_TRANSPORT_POLLS
};
static void cleanup_decimal(u32 value)
{
    char digits[11]; u32 n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) { char ch[2] = {digits[--n], 0}; print(ch); }
}
static void cleanup_envelope_tests(void)
{
    setup();
    profile.expected_device = RECOVER_GF_DEVICE; profile.crc = recover_profile_crc(&profile);
    core.profile = &profile; core.transport = &cleanup_transport;
    struct recover_ops clock_ops = ops; clock_ops.ticks = cleanup_clock;
    core.ops = &clock_ops; core.saved_count = profile.count;
    core.zero_hold = true;
    u32 crc = recover_profile_crc(&profile);
    core.token = (struct recover_token){.session=7, .profile_crc=crc};
    struct recover_token original = core.token;
    CHECK(!cleanup_budget_prepare(&core));
    u32 empty_bytes = cb.bytes;
    /* Three DATA values plus the shared original CRTC index. CR1F DATA
     * produces two SELECT/DATA visits. This is the actual saved set, not 32. */
    core.local_saved[0] = (struct recover_local_saved){REC_GF_T_CR44, REC_SELECT, 0};
    core.local_saved[1] = (struct recover_local_saved){REC_GF_T_CR44, REC_DATA, 0};
    core.local_saved[2] = (struct recover_local_saved){REC_GF_T_CR1F, REC_DATA, 0};
    core.local_saved[3] = (struct recover_local_saved){REC_GF_T_CR11, REC_DATA, 0};
    core.local_saved_count = 4;
    CHECK(!cleanup_budget_prepare(&core));
    struct rec_cleanup_budget actual = cb;
    CHECK(actual.bytes > empty_bytes && actual.s0_bytes == 4096 && actual.s0_ticks == 1000);
    CHECK(actual.s0_polls == 16384 && actual.seconds < 60);
    CHECK(recover_profile_crc(&profile) == crc && core.token.profile_crc == original.profile_crc &&
          core.token.session == original.session);
    print("cleanup actual4 bytes="); cleanup_decimal(cb.bytes);
    print(" pre="); cleanup_decimal(cb.bytes-cb.s0_bytes);
    print(" seconds="); cleanup_decimal(cb.seconds);
    print(" polls="); cleanup_decimal(cb.polls); print("\n");
    /* A4: 65 pre-S0 rows, maximum u32/s32 decimal widths, LF/CR boundary.
     * Four saved entries above may produce fewer rows; use the actual count
     * of accesses represented by the 48 local rows in the A4 capture. */
    for (u32 i = 0; i < 3; i++)
        core.local_saved[i] = (struct recover_local_saved){REC_GF_T_CR1F, REC_DATA, 0};
    core.local_saved_count = 3;
    CHECK(!cleanup_budget_prepare(&core));
    struct rec_cleanup_budget a4 = cb;
    cleanup_test_tick = cleanup_test_rows = cleanup_test_puts = 0;
    core.state = REC_RESTORING; core.restore_tick = 0;
    CHECK(!transport_state(&core, REC_RESTORING));
    for (u32 i = 0; i < 32; i++) {
        CHECK(!recover_record(&core, "restore", "BEGIN", ~0U, ~0U, 0, 0));
        CHECK(!recover_record(&core, "restore", "END", 0, 0, (-2147483647-1), ~0U));
    }
    CHECK(cleanup_test_rows == 65 && core.uart_bytes < cb.bytes-cb.s0_bytes && !core.lost_lines);
    print("cleanup A4 bytes="); cleanup_decimal(cb.bytes);
    print(" pre="); cleanup_decimal(cb.bytes-cb.s0_bytes);
    print(" seconds="); cleanup_decimal(cb.seconds);
    print(" polls="); cleanup_decimal(cb.polls); print("\n");
    core.in_s0 = true;
    for (u32 i=0;i<11;i++) {
        CHECK(!recover_record(&core,"screen","BEGIN",~0U,~0U,0,0));
        CHECK(!recover_record(&core,"screen","END",0,0,(-2147483647-1),~0U));
    }
    core.s0_verified=core.s1_verified=true;
    CHECK(!transport_state(&core,REC_VERIFIED));
    CHECK(cleanup_test_rows==89 && core.uart_bytes<cb.bytes && !core.failed && !core.lost_lines);
    core.in_s0=false; core.restore_polls=core.uart_polls=0;
    /* Exhaust byte allocation by one: sticky REC_LOG/lost, reserved S0 still
     * sends. Counters remain cumulative when S0 begins. */
    core.uart_bytes = cb.bytes-cb.s0_bytes; txd.active = 1;
    CHECK(recover_record(&core,"restore","END",0,0,0,0) < 0);
    CHECK(core.failed && core.reason == REC_LOG && core.lost_lines == 1 && txd.rc == REC_TX_BYTES);
    u32 bytes = core.uart_bytes;
    core.in_s0 = true;
    CHECK(!recover_record(&core,"screen","BEGIN",0,0,0,0) && core.uart_bytes > bytes);
    /* Independent poll and time boundaries, including +1, preserve S0. */
    for (u32 kind=0; kind<2; kind++) for (u32 extra=0; extra<2; extra++) {
        core.failed=false; core.reason=REC_NORMAL; core.in_s0=false;
        core.restore_polls=kind ? 0 : cb.polls-cb.s0_polls+extra;
        cleanup_test_tick=kind ? cb.ticks-cb.s0_ticks+extra : 0;
        CHECK(recover_restore_point(&core)<0 && core.failed && core.reason==REC_TIMEOUT);
        core.in_s0=true; CHECK(!recover_restore_point(&core));
    }
    /* Capacity 32 is validated for arithmetic/transport only. It must not
     * silently become the effective limit of an actual three-entry cleanup. */
    core.state=REC_PAUSED;
    for (u32 i=0;i<RECOVER_MAX_REGS;i++)
        core.local_saved[i]=(struct recover_local_saved){REC_GF_T_CR1F,REC_DATA,0};
    core.local_saved_count=RECOVER_MAX_REGS;
    CHECK(!cleanup_budget_prepare(&core) && cb.bytes>a4.bytes);
    struct rec_cleanup_budget maximum;
    CHECK(!cleanup_budget_compute(&core,&profile,true,&maximum));
    CHECK(maximum.bytes>=cb.bytes && maximum.polls<=cleanup_transport.max_polls && maximum.seconds+5<180);
    print("cleanup capacity32 bytes="); cleanup_decimal(cb.bytes);
    print(" seconds="); cleanup_decimal(cb.seconds);
    print(" polls="); cleanup_decimal(cb.polls); print("\n");
    /* Reentry must not overwrite cb; a different run initializes it again. */
    core.state=REC_RESTORING;
    u32 previous=cb.bytes; core.local_saved_count=0;
    CHECK(cleanup(&core)<0 && cb.bytes==previous);
    CHECK(!cleanup_budget_prepare(&core) && cb.bytes==empty_bytes);
    core.local_saved_count=RECOVER_MAX_REGS+1;
    CHECK(cleanup_budget_prepare(&core)<0 && cb.bytes==profile.restore_log_bytes);
    u32 ticks, polls;
    CHECK(cleanup_stage(~0U,2000,256,1042,&ticks,&polls)<0);
    CHECK(cleanup_stage(4096,2000,~0U,1042,&ticks,&polls)<0);
    CHECK(recover_profile_crc(&profile)==crc);
    core.transport=0;
}
