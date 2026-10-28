/* T3 wire + actual core safe points. Fake UART, frozen tick, no shell parser. */
struct t3_wire {
    u8 input[512]; u32 size, pos, available, gets, waits, puts;
    char output[16384]; u32 out;
    bool busy;
    u32 trigger, begins, row_at, mode, tick_per_wait;
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
    w->output[w->out++] = ch; w->output[w->out] = 0;
    if (w->mode == 3 && w->out == w->trigger) f.tick += profile.opcode_limit;
    if (ch == '\n') {
        bool begin_row = false;
        for (u32 i = w->row_at; i + 11 < w->out; i++)
            if (w->output[i] == 'e' && w->output[i + 6] == 'B' && w->output[i + 10] == 'N') begin_row = true;
        w->row_at = w->out;
        if (begin_row && ++w->begins == w->trigger) {
            if (w->mode == 1) w->busy = true;
            if (w->mode == 2) f.abort = true;
        }
    }
    return 0;
}
static int t3_wait(void *v, u32 us)
{ CHECK(us == 1042); struct t3_wire *w = v; w->waits++;
  if (r.state != REC_RESTORING) f.tick += w->tick_per_wait;
  return 0; }
static struct recover_transport t3_transport = {
    t3_get, {t3_ready, t3_put, t3_wait}, &wire, 1042, 16384, 0, 0
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
/* Exercise the actual logged access path, after RUNNING has been sent. */
static int t3_access(struct recover *core, void *arg)
{
    (void)arg;
    core->auth_phase = REC_RESTORE;
    u32 value = INITIAL;
    int rc = recover_access(core, RECOVER_NONE, REC_WRITE, DATA0, 1, &value);
    if (wire.mode == 1) CHECK(count('w', DATA0) == 0);
    return rc;
}
static int t3_begin_failure(struct recover *core, void *arg)
{
    (void)arg;
    /* The state line succeeded. Fail only the first BEGIN transmission. */
    wire.busy = true;
    struct recover_bus_access a = {.key = DATA0, .width = 1};
    u32 value = 0;
    CHECK(logged_read(core, &a, &value) < 0 && count('r', DATA0) == 0);
    CHECK(logged_write(core, &a, INITIAL, INITIAL) < 0 && count('w', DATA0) == 0);
    return -1;
}
static void t3_access_tests(void)
{
    t3_setup(true);
    CHECK(trial_step(&r, &r.token, true, t3_begin_failure, 0) < 0);
    CHECK(t3_contains("event=RUNNING") && r.lost_lines && f.local_calls == 0);
    /* Abort after the old-value read BEGIN, write BEGIN and verify BEGIN. */
    for (u32 at = 1; at <= 3; at++) {
        t3_setup(true); wire.mode = 2; wire.trigger = at;
        CHECK(trial_step(&r, &r.token, true, t3_access, 0) < 0);
        CHECK(r.state == REC_VERIFIED && r.reason == REC_ABORT && !r.failed);
        CHECK(f.local_calls == 1 && t3_contains("result=OK"));
    }
    /* Expiry mid-state-row must defer cleanup until the emitter unwinds. */
    t3_setup(true); wire.mode = 3; wire.trigger = 17;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
    CHECK(r.state == REC_FAILED && r.reason == REC_TIMEOUT && f.local_calls == 1);
    CHECK(r.lost_lines == 1);
    CHECK(wire.output[17] == '\n' && wire.output[18] == 'R');
    CHECK(t3_contains("phase=final event=END result=FAIL"));
}
static u32 t3_acquires, t3_releases, t3_release_ticks;
static bool t3_owned, t3_acquire_fail, t3_flush_fail;
static int t3_acquire(void *opaque)
{
    (void)opaque; CHECK(!t3_owned); t3_acquires++;
    if (t3_acquire_fail) return -1;
    t3_owned = true; return 0;
}
static int t3_release(struct recover *core, void *opaque, int rc)
{
    (void)opaque; CHECK(core == &r && t3_owned); t3_releases++;
    t3_owned = false; f.tick += t3_release_ticks; return t3_flush_fail ? -1 : rc;
}
static u32 t3_empty_calls, t3_empty_mode;
static int t3_empty(void *opaque)
{
    (void)opaque; t3_empty_calls++;
    if (t3_empty_mode == 2) f.tick += profile.opcode_limit;
    return t3_empty_mode ? 0 : t3_empty_calls == 3;
}
static void t3_flush_tests(void)
{
    for (u32 mode = 0; mode < 3; mode++) {
        t3_setup(true); r.state = REC_RUNNING; r.in_call = true; r.in_opcode = true; r.emitting = true;
        t3_empty_calls = 0; t3_empty_mode = mode;
        CHECK((recover_uart_flush(&r, t3_empty, &wire, 1042, 4) == 0) == (mode == 0));
        CHECK(wire.puts == 0 && r.uart_bytes == 0 && t3_empty_calls <= 4);
        if (mode == 0) CHECK(t3_empty_calls == 3 && wire.waits == 2);
        if (mode == 1) CHECK(r.reason == REC_LOG && t3_empty_calls == 4);
        if (mode == 2) CHECK(r.reason == REC_TIMEOUT && !wire.waits);
        r.emitting = false;
    }
    t3_setup(false); r.profile = &profile; r.state = REC_RESTORING; r.in_s0 = true;
    CHECK(!cleanup_budget_prepare(&r));
    t3_empty_calls = t3_empty_mode = 0;
    CHECK(recover_uart_flush(&r, t3_empty, &wire, 1042, 4) == 0);
    CHECK(r.uart_bytes == 0 && r.uart_polls == 3);
}
static void t3_log_budget_tests(void)
{
    for (u32 extra = 0; extra < 2; extra++) {
        t3_setup(true); r.log_limit = 10000; wire.tick_per_wait = 1;
        f.op_ticks = profile.opcode_limit - 1 + extra;
        CHECK((trial_step(&r, &r.token, true, change, 0) == 0) == !extra);
        CHECK(r.spent == f.op_ticks && r.tx_ticks > profile.opcode_limit);
        if (!extra) CHECK(r.state == REC_PAUSED && r.reason == REC_NORMAL);
        else CHECK(r.state == REC_FAILED && r.reason == REC_TIMEOUT);
    }
    /* A real bus interval preceding END belongs to execution, never TX. */
    t3_setup(true); r.log_limit = 10000; wire.tick_per_wait = 1;
    f.tick_read = profile.opcode_limit;
    CHECK(trial_step(&r, &r.token, true, t3_access, 0) < 0);
    CHECK(r.reason == REC_TIMEOUT && r.spent == profile.opcode_limit);
    /* Flush/release tail belongs to TX even after its last safe point. */
    t3_setup(true); r.log_limit = 10000; r.state = REC_RUNNING; r.in_call = true;
    t3_release_ticks = 3; t3_acquire_fail = t3_flush_fail = false;
    t3_transport.begin = t3_acquire; t3_transport.end = t3_release;
    CHECK(recover_record(&r, "test", "BEGIN", 0, 0, 0, 0) == 0);
    CHECK(r.tx_ticks == 3 && r.spent == 0);
    CHECK(recover_safe_point(&r) == 0 && r.spent == 0);
    t3_release_ticks = 0; t3_transport.begin = 0; t3_transport.end = 0;
    t3_setup(true); r.log_limit = 17; wire.tick_per_wait = 1;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
    CHECK(r.reason == REC_LOG && r.tx_ticks == 17 && r.spent == 0);
    CHECK(f.values[DATA0] == INITIAL && f.local_calls == 1);
}
static void t3_owner_tests(void)
{
    for (u32 mode = 0; mode < 4; mode++) {
        t3_setup(true); r.state = REC_RUNNING; r.in_call = true;
        t3_acquires = t3_releases = 0; t3_owned = false;
        t3_acquire_fail = mode == 1; t3_flush_fail = mode == 2; wire.busy = mode == 3;
        t3_transport.begin = t3_acquire; t3_transport.end = t3_release;
        CHECK((recover_record(&r, "test", "BEGIN", 0, 0, 0, 0) == 0) == (mode == 0));
        CHECK(t3_acquires == 1 && t3_releases == (mode == 1 ? 0U : 1U) && !t3_owned);
        if (mode == 1) CHECK(wire.puts == 0);
        t3_transport.begin = 0; t3_transport.end = 0;
    }
    t3_setup(true); r.state = REC_RUNNING; r.in_call = true;
    CHECK(t3_access(&r, 0) == 0);
    CHECK(t3_contains("phase=gf_bus event=BEGIN pc=4294967295 bar=0 off=4 width=1 old=165 new=165"));
    CHECK(t3_contains("phase=gf_bus event=END") && t3_contains("readback=165 rc=0"));
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
    CHECK(r.state == REC_FAILED && r.lost_lines > 0 && f.local_calls == 0);
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
static struct {
    struct text_fake text;
    u32 interrupt_at, order, restored, cleanup, local, s0;
    bool timeout;
} cell_trial;
static void cell_trial_interrupt(void)
{
    if (r.state!=REC_RUNNING || cell_trial.text.calls!=cell_trial.interrupt_at) return;
    CHECK(cell_trial.text.cell==0xedcb && !f.local_calls);
    if (cell_trial.timeout) f.tick+=profile.opcode_limit;
    else f.abort=true;
}
static int cell_trial_read(void *v,u32 *value)
{
    int rc=text_read(v,value);
    cell_trial_interrupt();
    return rc;
}
static int cell_trial_write(void *v,u32 value)
{
    int rc=text_write(v,value);
    if (r.state==REC_RUNNING && value==0x1234) cell_trial.restored=++cell_trial.order;
    cell_trial_interrupt();
    return rc;
}
static int cell_trial_event(void *v,enum recover_state state,u32 session)
{
    if (state==REC_RESTORING) {
        CHECK(cell_trial.text.cell==0x1234 && cell_trial.restored==1);
        cell_trial.cleanup=++cell_trial.order;
    }
    return event(v,state,session);
}
static int cell_trial_local(void *v)
{
    CHECK(cell_trial.text.cell==0x1234 && cell_trial.cleanup==2);
    cell_trial.local=++cell_trial.order;
    return local_cleanup(v);
}
static int cell_trial_screen(void *v)
{
    (void)v;
    CHECK(r.in_s0 && cell_trial.text.cell==0x1234 && cell_trial.local==3);
    cell_trial.s0=++cell_trial.order;
    return recover_screen_98(&r,&text_ops,&cell_trial.text);
}
static int cell_trial_run(struct recover *core,void *arg)
{
    (void)arg;
    struct recover_screen_ops screen_ops=text_ops;
    screen_ops.read_cell=cell_trial_read; screen_ops.write_cell=cell_trial_write;
    return recover_cell_98(core,&screen_ops,&cell_trial.text);
}
static void t3_cell_interrupt_tests(void)
{
    /* Real trial_step -> cell -> cleanup -> S0, sharing the same TVRAM word.
     * Inject immediately after test W and test R, with and without UART.
     * Failed W/R callbacks must also restore before accepting interruption. */
    for(u32 transport=0;transport<2;transport++)
    for(u32 at=2;at<=3;at++) for(u32 timeout=0;timeout<2;timeout++)
    for(u32 fault=0;fault<2;fault++) {
        t3_setup(true);
        if (!transport) r.transport=0;
        cell_trial.text=(struct text_fake){.cell=0x1234,.fail_at=fault ? at:0};
        cell_trial.interrupt_at=at; cell_trial.timeout=timeout;
        cell_trial.order=cell_trial.restored=cell_trial.cleanup=cell_trial.local=cell_trial.s0=0;
        struct recover_ops trial_ops=ops;
        trial_ops.event=cell_trial_event; trial_ops.local_cleanup=cell_trial_local;
        trial_ops.screen_restore_verify=cell_trial_screen; r.ops=&trial_ops;
        CHECK(trial_step(&r,&r.token,true,cell_trial_run,0)<0);
        CHECK(cell_trial.text.cell==0x1234 && cell_trial.restored==1);
        CHECK(cell_trial.cleanup==2 && cell_trial.local==3 && cell_trial.s0==4);
        CHECK(f.local_calls==1 && r.s0_verified);
        CHECK(r.reason==(timeout ? REC_TIMEOUT:REC_ABORT));
        CHECK(r.state==(timeout ? REC_FAILED:REC_VERIFIED));
        CHECK(cell_trial.text.writes==4 && cell_trial.text.redraws==1);
    }
    output("RUN cell abort/timeout after W/R: restore -> cleanup -> S0 PASS\n");
}
static void t3_s0_tests(void)
{
    t3_cell_interrupt_tests();
    t3_setup(false); r.profile = &profile; r.state = REC_RESTORING; r.in_s0 = true;
    CHECK(!cleanup_budget_prepare(&r));
    struct text_fake text = {.cell = 0x1234};
    CHECK(recover_screen_98(&r, &text_ops, &text) == 0);
    /* Preserve the complete pre-2c2 S0 screen trace, including the unusual
     * restore-read step 7 before redraw step 6. */
    static const char steps[]="01234576";
    const char *marker="phase=screen event=BEGIN step=";
    u32 seen=0,ends=0;
    for(u32 i=0;i<wire.out;i++) {
        u32 j=0;while(marker[j] && wire.output[i+j]==marker[j])j++;
        if(!marker[j]) { CHECK(seen<sizeof(steps)-1 && wire.output[i+j]==steps[seen]);seen++; }
        const char *end="phase=screen event=END rc=0";
        j=0;while(end[j] && wire.output[i+j]==end[j])j++;
        if(!end[j])ends++;
    }
    CHECK(seen==8 && ends==8 && text.calls==8 && text.writes==2 && text.redraws==1);
    CHECK(text.cell==0x1234);
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
/* Route-test uses the real core and bounded wire, with the same S0 profile as now. */
static void route_test_tests(void)
{
    for (u32 mode = 0; mode < 10; mode++) {
        t3_setup(false);
        struct recover_profile s0 = profile;
        s0.count = s0.quiesce_count = 0;
        s0.crc = recover_profile_crc(&s0); r.s0_profile = &s0;
        if (mode == 1) f.route = 0xfd;
        if (mode == 2) f.route_readback = 0xfd;
        if (mode == 3) f.ready = false;
        if (mode == 4) f.route_read_fail = true;
        if (mode == 5) f.route_fail = true;
        if (mode == 6) f.route_ticks = s0.restore_limit;
        if (mode == 7) r.state = REC_PAUSED;
        if (mode == 8) r.token.session = 1;
        int rc = recover_route_test_session(&r, mode == 9 ? 1 : 0);
        CHECK(!route_test_once && !route_test_approved && !r.route_write_approved);
        CHECK(count('A', RECOVER_ROUTE_PORT) == (mode <= 2 || mode == 5 ? 1U : 0U));
        if (mode < 2) {
            CHECK(rc == 0 && r.s0_verified && !r.s1_verified && !r.lost_lines);
            u32 at = 0;
            for (u32 i = 0; i < f.n; i++) if (f.trace[i].key == RECOVER_ROUTE_PORT) {
                CHECK(at < 3 && f.trace[i].op == (at == 1 ? 'A' : 'a'));
                if (at == 1) CHECK(f.trace[i].value == 0);
                at++;
            }
            CHECK(at == 3);
        } else {
            CHECK(rc < 0 && !r.s0_verified && !count('v', 0));
            if (mode == 2) CHECK(r.reason == REC_VERIFY && r.state == REC_FAILED);
            if (mode == 3 || mode >= 7) CHECK(f.n == 0);
        }
        /* Re-enter cleanup after completion: the write must never be repeated. */
        u32 n = count('A', RECOVER_ROUTE_PORT);
        if (mode < 3 || mode == 5) (void)cleanup(&r);
        CHECK(count('A', RECOVER_ROUTE_PORT) == n);
        for (u32 non98 = 0; non98 < 2; non98++) {
            t3_setup(false); r.s0_profile = &s0; f.route = non98 ? 0xfd : 0xfc;
            rc = recover_now_session(&r, 0);
            CHECK(!count('A', RECOVER_ROUTE_PORT));
            CHECK(non98 ? rc < 0 && r.reason == REC_CONTRACT : rc == 0 && r.s0_verified);
        }
    }
}
static void t3_tests(void)
{ route_test_tests(); t3_receive_tests(); t3_access_tests(); t3_owner_tests(); t3_flush_tests(); t3_log_budget_tests(); t3_safe_tests(); t3_log_tests(); t3_s0_tests(); }
