/* T3 wire + actual core safe points. Fake UART, frozen tick, no shell parser. */
struct t3_wire {
    u8 input[512]; u32 size, pos, available, gets, waits, puts;
    char output[16384]; u32 out;
    bool busy;
};
static struct t3_wire wire;
static int t3_get(void *v)
{
    struct t3_wire *w = v; w->gets++;
    return w->pos < w->available ? w->input[w->pos++] : -1;
}
static int t3_ready(void *v) { return !((struct t3_wire *)v)->busy; }
static int t3_put(void *v, u32 ch)
{
    struct t3_wire *w = v; w->puts++;
    if (w->out + 1 >= sizeof(w->output)) return -1;
    w->output[w->out++] = ch; w->output[w->out] = 0; return 0;
}
static int t3_wait(void *v, u32 us)
{ CHECK(us == 1042); ((struct t3_wire *)v)->waits++; return 0; }
static struct recover_transport t3_transport = {
    t3_get, {t3_ready, t3_put, t3_wait}, &wire, 1042, 16384
};
static void t3_setup(bool start)
{
    setup(false); wire = (struct t3_wire){0};
    profile.restore_log_bytes = 4096; profile.restore_max_polls = 16384;
    profile.crc = recover_profile_crc(&profile);
    if (start) { begin(); f.n = 0; }
    r.transport = &t3_transport;
}
static void t3_byte(u8 b) { CHECK(wire.size < sizeof(wire.input)); wire.input[wire.size++] = b; }
static void t3_frame(u32 sid)
{
    u8 bytes[10] = {0xa5, 0x5a}; u32 crc = ~0U;
    for (u32 i = 0; i < 4; i++) bytes[2 + i] = sid >> (8 * i);
    for (u32 i = 0; i < 6; i++) {
        crc ^= bytes[i];
        for (u32 bit = 0; bit < 8; bit++) crc = crc & 1 ? (crc >> 1) ^ 0xedb88320U : crc >> 1;
    }
    crc = ~crc;
    for (u32 i = 0; i < 4; i++) bytes[6 + i] = crc >> (8 * i);
    for (u32 i = 0; i < 10; i++) t3_byte(bytes[i]);
}
static bool t3_contains(const char *s)
{
    for (u32 i = 0; i < wire.out; i++) {
        u32 j = 0; while (s[j] && wire.output[i + j] && s[j] == wire.output[i + j]) j++;
        if (!s[j]) return true;
    }
    return false;
}
static void t3_receive_tests(void)
{
    for (u32 split = 1; split < 10; split++) {
        t3_setup(true); t3_frame(r.token.session); wire.available = split;
        CHECK(recover_receive(&r) == 0 && r.rx_used == split);
        wire.available = wire.size;
        CHECK(recover_receive(&r) == 1 && r.rx_used == 0 && r.discarded_bytes == 0);
    }
    t3_setup(true); t3_frame(r.token.session + 1); wire.available = wire.size;
    CHECK(recover_receive(&r) == 0 && r.discarded_bytes == 10);
    t3_setup(true); t3_frame(r.token.session); wire.input[9] ^= 1;
    wire.available = wire.size;
    CHECK(recover_receive(&r) == 0 && r.discarded_bytes == 10);
    /* A valid sync beginning within a corrupt candidate must survive sliding. */
    t3_setup(true); t3_byte(0xa5); t3_byte(0x5a); t3_byte(0x7f);
    t3_frame(r.token.session); wire.available = wire.size;
    CHECK(recover_receive(&r) == 1 && r.discarded_bytes == 3);
    t3_setup(true);
    const char *normal = "reboot\n";
    for (u32 i = 0; normal[i]; i++) t3_byte(normal[i]);
    t3_frame(r.token.session); t3_byte('x'); wire.available = wire.size;
    CHECK(recover_receive(&r) == 1 && r.discarded_bytes == 8 && writes() == 0);
    t3_setup(true); for (u32 i = 0; i < 100; i++) t3_byte('a');
    wire.available = wire.size;
    CHECK(recover_receive(&r) == 0 && wire.gets == 64 && wire.pos == 64);
    CHECK(r.discarded_bytes == 64);
    /* End-of-call drops partial input, never completes it in another call. */
    t3_setup(true); t3_frame(r.token.session); wire.available = 5;
    CHECK(recover_receive(&r) == 0 && r.rx_used == 5);
    wire.available = wire.size; receive_boundary(&r);
    CHECK(r.rx_used == 0 && r.discarded_bytes == 10 && wire.pos == wire.size);
    t3_frame(r.token.session); wire.available = wire.size;
    r.token.session++;
    CHECK(recover_receive(&r) == 0 && r.discarded_bytes == 20);
}
static void t3_safe_tests(void)
{
    t3_setup(true); t3_frame(r.token.session); wire.available = wire.size;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
    CHECK(r.reason == REC_ABORT && r.state == REC_VERIFIED && f.local_calls == 1);
    CHECK(r.s0_verified && f.values[DATA0] == INITIAL && f.values[DATA1] == INITIAL);
    CHECK(t3_contains("phase=final event=END result=OK") && t3_contains("lost_lines=0"));
    CHECK(wire.waits == wire.puts && wire.puts < 4096);
    /* A dead UART with frozen ticks must still leave the trial and cleanup. */
    t3_setup(true); wire.busy = true;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
    CHECK(r.state == REC_FAILED && r.lost_lines > 0 && f.local_calls == 1);
    CHECK(wire.waits > 0 && wire.waits <= 32768 && f.tick == 0);
    /* Paused now only accepts the current token; disabled means no writes. */
    t3_setup(true); CHECK(recover_now_session(&r, r.token.session + 1) < 0 && writes() == 0);
    r.enabled = false;
    CHECK(recover_now_session(&r, r.token.session) < 0 && writes() == 0);
    r.enabled = true;
    CHECK(recover_now_session(&r, r.token.session) == 0 && f.local_calls == 1);
    u32 n = f.n;
    CHECK(recover_now_session(&r, r.token.session) == 0 && f.n == n);
}
static void t3_log_tests(void)
{
    t3_setup(false); begin();
    CHECK(t3_contains("REC sid=1 seq=1 phase=state event=SNAPSHOTTING"));
    CHECK(t3_contains("event=PAUSED version=") && t3_contains(" profile_crc="));
    CHECK(t3_contains(" plan_hash=") && t3_contains(" snapshot_crc="));
    t3_byte('x'); t3_byte('y'); wire.available = wire.size;
    CHECK(trial_step(&r, &r.token, true, change, 0) == 0);
    CHECK(trial_end(&r, &r.token, REC_NORMAL) == 0);
    CHECK(t3_contains("phase=receive event=DISCARD discarded_bytes=2"));
    CHECK(t3_contains("lost_lines=0 discarded_bytes=2"));
    u32 lines = 0, at = 0;

    while (at < wire.out) {
        CHECK(wire.output[at] == 'R');
        u32 seq_at = at;
        while (seq_at + 5 < wire.out && !(wire.output[seq_at] == 's' &&
            wire.output[seq_at + 1] == 'e' && wire.output[seq_at + 2] == 'q' &&
            wire.output[seq_at + 3] == '=')) seq_at++;
        seq_at += 4;
        u32 seq = 0;
        while (seq_at < wire.out && wire.output[seq_at] >= '0' && wire.output[seq_at] <= '9')
            seq = seq * 10 + wire.output[seq_at++] - '0';
        CHECK(seq == lines + 1);
        while (at < wire.out && wire.output[at] != '\n') at++;
        if (at < wire.out) { lines++; at++; }
    }
    CHECK(lines == r.seq && lines > 4);
#ifdef RECOVER_T3_CAPTURE
    output("T3 WIRE BEGIN\n"); output(wire.output); output("T3 WIRE END\n");
#endif
    /* Queued old-session input is discarded at a new trial boundary. */
    t3_frame(r.token.session); wire.available = wire.size;
    wire.out = 0; wire.output[0] = 0; f.n = 0;
    begin();
    CHECK(r.token.session == 2 && r.rx_used == 0 && wire.pos == wire.size);
    CHECK(r.discarded_bytes == 10 && t3_contains("REC sid=2 seq=1 "));
    /* Singleton is boot-lifetime owned and cannot be replaced. */
    setup(false);
    CHECK(recover_owner() == recover_owner());
    CHECK(recover_owner_configure(&ops, &f, &profile, false) == 0);
    recover_owner()->state = REC_PAUSED;
    CHECK(recover_owner_configure(&ops, &f, &profile, true) < 0);
    CHECK(recover_owner()->opaque == &f && !recover_owner()->route_write_approved);
}
static void t3_s0_tests(void)
{
    for (u32 mode = 0; mode < 3; mode++) {
        t3_setup(false);
        struct recover_profile s0 = profile;
        s0.count = 0; s0.quiesce_count = 0;
        s0.crc = recover_profile_crc(&s0); r.s0_profile = &s0;
        if (mode == 1) f.route = 0xff;
        if (mode == 2) r.enabled = false;
        int rc = recover_now_session(&r, 0);
        if (!mode) {
            CHECK(rc == 0 && r.state == REC_VERIFIED && r.s0_verified && !r.s1_verified);
            CHECK(r.token.session == 1 && !r.plan && f.local_calls == 1);
            CHECK(t3_contains("phase=final event=END result=PARTIAL target=S0 verified=1 unknown=1"));
        } else {
            CHECK(rc < 0 && !r.s0_verified && writes() == 0);
            CHECK(count('v', 0) == 0 && count('A', RECOVER_ROUTE_PORT) == 0);
        }
    }
}
static void t3_tests(void)
{ t3_receive_tests(); t3_safe_tests(); t3_log_tests(); t3_s0_tests(); }
