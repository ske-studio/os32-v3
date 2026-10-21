/* Real T1 core, fake bus/clock/serial. ILP32, no libc, host32 qemu runner. */
#include "../../kdebug/recover.c"

#define NREG 8U
#define TRACE_MAX 256U
#define DATA0 1U
#define DATA1 2U
#define OUTPUT 3U
#define LEGACY 4U
#define IRQ 5U
#define INITIAL 0xA5U
#define MASK 0x0FU
#define BAD_KEY (~0UL)
struct trace { char op; u32 key, value; };
struct fake {
    u32 tick, tick_read, tick_write, values[NREG], route, command, bar;
    u32 n, fail_read, fail_write, bad_verify, op_ticks, config_reads;
    struct trace trace[TRACE_MAX];
    struct recover_identity id;
    bool ready, raw_fail, decode_fail, screen_fail, abort, route_fail;
    bool route_read_fail, route_stuck, log_fail, snapshot_early;
    struct recover *core;
};
static int fails;
static void output(const char *s)
{
    u32 n = 0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void decimal(u32 v)
{
    char b[12]; u32 i = sizeof(b) - 1; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    output(b + i);
}
#define CHECK(x) do { if (!(x)) { output("FAIL line "); decimal(__LINE__); \
    output(": " #x "\n"); fails++; } } while (0)
static void note(struct fake *f, char op, u32 key, u32 value)
{
    CHECK(f->n < TRACE_MAX);
    if (f->n < TRACE_MAX) f->trace[f->n++] = (struct trace){op, key, value};
}
static u32 ticks(void *v) { return ((struct fake *)v)->tick; }
static int identity(void *v, struct recover_identity *out)
{ *out = ((struct fake *)v)->id; return 0; }
static int ready(void *v) { return !((struct fake *)v)->ready; }
static int config(void *v, u32 *command, u32 *bar)
{
    struct fake *f = v; f->config_reads++; note(f, 'c', 0, 0);
    *command = f->command; *bar = f->bar; return f->raw_fail;
}
static int read_bus(void *v, u32 key, u32 width, u32 *out)
{
    struct fake *f = v; (void)width;
    note(f, 'r', key, 0); f->tick += f->tick_read;
    if (f->core->state == REC_SNAPSHOTTING && f->core->valid &&
        f->core->saved_count < f->core->profile->count)
        f->snapshot_early = true;
    if (key == f->fail_read) return -1;
    CHECK(key < NREG); *out = f->values[key];
    if (key == f->bad_verify) *out ^= 1;
    return 0;
}
static int write_bus(void *v, u32 key, u32 width, u32 value)
{
    struct fake *f = v; (void)width;
    note(f, 'w', key, value); f->tick += f->tick_write;
    if (key == f->fail_write) return -1;
    CHECK(key < NREG); f->values[key] = value; return 0;
}
static int route_read(void *v, u32 *out)
{
    struct fake *f = v; note(f, 'a', RECOVER_ROUTE_PORT, f->route);
    *out = f->route; return f->route_read_fail;
}
static int route_write(void *v, u32 value)
{
    struct fake *f = v; note(f, 'A', RECOVER_ROUTE_PORT, value);
    if (!f->route_stuck) f->route = value | 0xFCU;
    return f->route_fail;
}
static int screen_safe(void *v)
{ struct fake *f = v; note(f, 'd', 0, 0); return f->decode_fail; }
static int screen_restore(void *v)
{ struct fake *f = v; note(f, 'v', 0, 0); return f->screen_fail; }
static int abort_requested(void *v, u32 session)
{ struct fake *f = v; CHECK(session == f->core->token.session); return f->abort; }
static int event(void *v, enum recover_state state, u32 session)
{
    struct fake *f = v; (void)session; note(f, 's', state, 0);
    return f->log_fail;
}
static const struct recover_ops ops = {
    ticks, identity, ready, config, read_bus, write_bus, route_read, route_write,
    screen_safe, screen_restore, abort_requested, event
};
static struct recover_reg regs[2];
static struct recover_write quiet[3];
static struct recover_profile profile;
static struct recover r;
static struct fake f;
static void setup(bool cold)
{
    f = (struct fake){ .command = RECOVER_COMMAND_MEM, .bar = 0x10000000UL,
        .ready = true, .fail_read = BAD_KEY, .fail_write = BAD_KEY,
        .bad_verify = BAD_KEY, .route = 0xFC, .core = &r,
        .id = {1, 2, 3, 4, 5, 6, 7, 0x10000000UL} };
    for (u32 i = 0; i < NREG; i++) f.values[i] = INITIAL;
    regs[0] = (struct recover_reg){DATA0, 1, MASK, MASK, cold ? REC_COLD : REC_RW, true};
    regs[1] = (struct recover_reg){DATA1, 1, MASK, MASK, REC_RW, true};
    quiet[0] = (struct recover_write){OUTPUT, 4, 0, ~0UL};
    quiet[1] = (struct recover_write){LEGACY, 4, 0, ~0UL};
    quiet[2] = (struct recover_write){IRQ, 4, 0, ~0UL};
    profile = (struct recover_profile){ .version = 1, .regs = regs, .count = 2,
        .quiesce = quiet, .quiesce_count = 3, .trial_limit = 100,
        .opcode_limit = 20, .restore_limit = 50, .max_steps = 4,
        .max_polls = 8, .cold_required = cold };
    profile.crc = recover_profile_crc(&profile);
    recover_init(&r, &ops, &f, true, false);
}
static u32 count(char op, u32 key)
{
    u32 n = 0;
    for (u32 i = 0; i < f.n; i++)
        if (f.trace[i].op == op && f.trace[i].key == key) n++;
    return n;
}
static u32 writes(void)
{
    u32 n = 0;
    for (u32 i = 0; i < f.n; i++) if (f.trace[i].op == 'w' || f.trace[i].op == 'A') n++;
    return n;
}
static int change(struct recover *core, void *arg)
{
    (void)arg;
    CHECK(core->valid && core->state == REC_RUNNING);
    f.values[DATA0] = 0xB9; f.values[DATA1] = 0xC9;
    f.tick += f.op_ticks;
    return 0;
}
static int error_op(struct recover *core, void *arg)
{ change(core, arg); return -1; }
static int polling(struct recover *core, void *arg)
{
    (void)arg;
    for (u32 i = 0; i < profile.max_polls + 1; i++)
        if (recover_safe_point(core)) return -1;
    return 0;
}
static void begin(void)
{
    CHECK(trial_begin(&r, &profile, 123, 100) == 0);
    CHECK(r.state == REC_PAUSED && r.valid && !r.in_call);
    CHECK(!f.snapshot_early);
}
static void preconditions(void)
{
    setup(false); r.enabled = false;
    CHECK(trial_begin(&r, &profile, 123, 100) < 0);
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
    CHECK(trial_end(&r, &r.token, REC_NORMAL) < 0 && writes() == 0);
    setup(false); f.ready = false;
    CHECK(trial_begin(&r, &profile, 123, 100) < 0 && writes() == 0);
    for (u32 i = 0; i < 8; i++) {
        setup(false);
        switch (i) {
        case 0: regs[0].kind = REC_W1C; break;
        case 1: regs[0].kind = REC_UNCLASSIFIED; break;
        case 2: regs[0].read_safe = false; break;
        case 3: regs[0].width = 3; break;
        case 4: profile.trial_limit = RECOVER_HALF_TICKS; break;
        case 5: profile.opcode_limit = 0; break;
        case 6: profile.restore_limit = 0; break;
        case 7: regs[0].verify_mask = 0x100; break;
        }
        profile.crc = recover_profile_crc(&profile);
        CHECK(trial_begin(&r, &profile, 123, 100) < 0 && writes() == 0);
    }
    setup(false); profile.crc ^= 1;
    CHECK(trial_begin(&r, &profile, 123, 100) < 0 && writes() == 0);
    setup(false);
    CHECK(trial_begin(&r, &profile, 123, 0) < 0);
    CHECK(trial_begin(&r, &profile, 123, RECOVER_HALF_TICKS) < 0);
    CHECK(writes() == 0);
}
static void normal(void)
{
    setup(false); begin();
    const enum recover_state expected[] = {REC_SNAPSHOTTING, REC_ARMED, REC_RUNNING, REC_PAUSED};
    u32 j = 0;
    for (u32 i = 0; i < f.n; i++) if (f.trace[i].op == 's') {
        CHECK(j < sizeof(expected)/sizeof(expected[0]));
        if (j < sizeof(expected)/sizeof(expected[0])) CHECK(f.trace[i].key == (u32)expected[j]);
        j++;
    }
    CHECK(j == 4);
    u32 n = f.n;
    CHECK(trial_begin(&r, &profile, 123, 100) < 0 && f.n == n);
    CHECK(recover_request(&r, &r.token, REC_REQUEST_STATUS) < 0);
    CHECK(trial_step(&r, &r.token, false, change, 0) < 0 && f.n == n);
    CHECK(trial_step(&r, &r.token, true, change, 0) == 0);
    f.n = 0;
    CHECK(trial_end(&r, &r.token, REC_NORMAL) == 0);
    CHECK(r.state == REC_VERIFIED && r.s0_verified && r.s1_verified);
    CHECK(f.values[DATA0] == 0xB5 && f.values[DATA1] == 0xC5);
    const u32 keys[] = {OUTPUT, LEGACY, IRQ, DATA0, DATA1}; j = 0;
    for (u32 i = 0; i < f.n; i++) if (f.trace[i].op == 'w') {
        CHECK(j < sizeof(keys)/sizeof(keys[0]));
        if (j < sizeof(keys)/sizeof(keys[0])) CHECK(f.trace[i].key == keys[j]);
        j++;
    }
    CHECK(j == 5 && count('A', RECOVER_ROUTE_PORT) == 0);
    n = f.n;
    CHECK(trial_end(&r, &r.token, REC_ABORT) == 0 && f.n == n);
    CHECK(recover_request(&r, &r.token, REC_REQUEST_NOW) == 0 && f.n == n);
    CHECK(trial_begin(&r, &profile, 123, 100) == 0 && r.token.session == 2);
}
static void mismatches(void)
{
    for (u32 i = 0; i < 13; i++) {
        setup(false); begin(); struct recover_token t = r.token;
        switch (i) {
        case 0: t.session++; break;
        case 1: t.profile_crc++; break;
        case 2: t.profile_version++; break;
        case 3: t.snapshot_crc++; break;
        case 4: t.plan_hash++; break;
        case 5: t.identity.boot++; break;
        case 6: t.identity.image_version++; break;
        case 7: t.identity.kdebug_version++; break;
        case 8: t.identity.bdf++; break;
        case 9: t.identity.device++; break;
        case 10: t.identity.subsys++; break;
        case 11: t.identity.revision++; break;
        case 12: t.identity.bar0++; break;
        }
        u32 n = f.n;
        CHECK(trial_step(&r, &t, true, change, 0) < 0);
        CHECK(trial_end(&r, &t, REC_NORMAL) < 0 && f.n == n);
        CHECK(r.state == REC_PAUSED);
    }
    for (u32 i = 0; i < 7; i++) {
        setup(false); begin(); f.n = 0;
        switch (i) {
        case 0: f.id.boot++; break;
        case 1: f.id.image_version++; break;
        case 2: f.id.kdebug_version++; break;
        case 3: f.id.device++; break;
        case 4: regs[0].key++; break;
        case 5: r.saved[0]++; break;
        case 6: profile.version++; break;
        }
        CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
        CHECK(r.state == REC_FAILED && writes() == 0 && r.s0_verified);
    }
}
static void timing(void)
{
    setup(false); f.tick = 0xFFFFFFFAUL; f.tick_read = 2; begin();
    CHECK(r.spent == 10);
    f.tick += 100000; f.op_ticks = 7;
    CHECK(trial_step(&r, &r.token, true, change, 0) == 0 && r.spent == 17);
    f.tick += 100000;
    CHECK(trial_step(&r, &r.token, true, change, 0) == 0 && r.spent == 24);
    CHECK(recover_request(&r, &r.token, REC_REQUEST_NOW) == 0 && r.reason == REC_ABORT);
    setup(false); begin(); f.op_ticks = 20;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0 && r.state == REC_FAILED);
    CHECK(r.reason == REC_TIMEOUT && r.s0_verified);
    setup(false); profile.opcode_limit = 99; profile.crc = recover_profile_crc(&profile);
    f.tick = 0xFFFFFFFCUL; begin(); f.op_ticks = 60;
    CHECK(trial_step(&r, &r.token, true, change, 0) == 0 && r.spent == 60);
    f.tick += 99999;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0 && r.spent == 100);
    setup(false); begin();
    CHECK(trial_step(&r, &r.token, true, polling, 0) < 0 && r.reason == REC_TIMEOUT);
    setup(false); begin();
    for (u32 i = 0; i < profile.max_steps; i++) CHECK(trial_step(&r, &r.token, true, change, 0) == 0);
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0 && r.reason == REC_TIMEOUT);
    setup(false); begin(); f.tick_write = profile.restore_limit;
    CHECK(trial_end(&r, &r.token, REC_NORMAL) < 0);
    CHECK(r.state == REC_FAILED && writes() == 4); /* 3 prepare + 1 restore */
}
static void failures(void)
{
    setup(false); f.fail_read = DATA1;
    CHECK(trial_begin(&r, &profile, 123, 100) < 0);
    CHECK(r.state == REC_FAILED && !r.valid && r.saved_count == 1);
    CHECK(r.reason == REC_SNAPSHOT && count('w', DATA0) == 1 && count('w', DATA1) == 0);
    CHECK(r.s0_verified && !f.snapshot_early);
    setup(false); f.fail_write = OUTPUT;
    CHECK(trial_begin(&r, &profile, 123, 100) < 0);
    CHECK(count('w', OUTPUT) == 1 && r.state == REC_FAILED && r.s0_verified);
    setup(false); begin(); CHECK(trial_step(&r, &r.token, true, error_op, 0) < 0);
    CHECK(r.state == REC_FAILED && r.reason == REC_OPERATION && r.s0_verified);
    setup(false); begin(); f.abort = true;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0);
    CHECK(r.state == REC_VERIFIED && r.reason == REC_ABORT);
    setup(false); begin(); f.bad_verify = DATA0; f.n = 0;
    CHECK(trial_end(&r, &r.token, REC_NORMAL) < 0);
    CHECK(r.state == REC_FAILED && r.reason == REC_VERIFY && r.first_key == DATA0);
    CHECK(count('w', DATA1) == 0 && r.s0_verified);
    setup(false); begin(); f.log_fail = true;
    CHECK(trial_step(&r, &r.token, true, change, 0) < 0 && r.state == REC_FAILED);
}
static void cold_and_config(void)
{
    for (u32 reason = 0; reason < 4; reason++) {
        for (u32 cfg = 0; cfg < 4; cfg++) {
            setup(true); begin(); f.n = 0;
            if (cfg == 1) f.command = 0;
            if (cfg == 2) f.bar++;
            if (cfg == 3) f.raw_fail = true;
            int rc;
            if (reason == 0) rc = trial_end(&r, &r.token, REC_NORMAL);
            else if (reason == 1) rc = recover_request(&r, &r.token, REC_REQUEST_NOW);
            else if (reason == 2) {
                f.op_ticks = 100;
                rc = trial_step(&r, &r.token, true, change, 0);
            } else rc = trial_step(&r, &r.token, true, error_op, 0);
            CHECK(count('w', DATA0) == 0 && count('w', DATA1) == 0);
            CHECK(!r.s1_verified && r.cold_latched && r.s0_verified);
            CHECK(count('w', OUTPUT) == (cfg == 0 ? 1U : 0U));
            CHECK(writes() == (cfg == 0 ? 3U : 0U));
            CHECK(r.state == ((cfg || reason >= 2) ? REC_FAILED : REC_VERIFIED));
            CHECK((rc < 0) == (cfg || reason >= 2));
            u32 n = f.n;
            CHECK(trial_begin(&r, &profile, 123, 100) < 0 && f.n == n);
            trial_end(&r, &r.token, REC_ABORT); CHECK(f.n == n);
        }
    }
    for (u32 cfg = 0; cfg < 3; cfg++) {
        setup(false);
        if (cfg == 0) f.command = 0;
        if (cfg == 1) f.bar++;
        if (cfg == 2) f.raw_fail = true;
        CHECK(trial_begin(&r, &profile, 123, 100) < 0 && writes() == 0);
        CHECK(r.state == REC_FAILED && r.s0_verified);
    }
}
static void routes(void)
{
    for (u32 mode = 0; mode < 8; mode++) {
        setup(true); begin(); f.n = 0;
        f.route = mode == 0 ? 0xFC : 0xFF;
        r.route_write_approved = mode != 1;
        if (mode == 2) f.route_read_fail = true;
        if (mode == 3) f.route_fail = true;
        if (mode == 4) f.route_stuck = true;
        if (mode == 5) f.decode_fail = true;
        if (mode == 6) f.screen_fail = true;
        int rc = trial_end(&r, &r.token, REC_NORMAL);
        CHECK((rc == 0) == (mode == 0 || mode == 7));
        CHECK(count('A', RECOVER_ROUTE_PORT) == ((mode == 0 || mode == 1 || mode == 2) ? 0U : 1U));
        CHECK(count('v', 0) == ((mode == 0 || mode == 6 || mode == 7) ? 1U : 0U));
        bool route_seen = false, safe_seen = false;
        for (u32 i = 0; i < f.n; i++) {
            struct trace *t = &f.trace[i];
            if (t->op == 'a') route_seen = true;
            if (t->op == 'A') { CHECK(route_seen && t->value == 0); }
            if (t->op == 'd') { CHECK(route_seen); safe_seen = true; }
            if (t->op == 'v') CHECK(safe_seen);
        }
    }
    setup(true); begin(); f.n = 0; f.command = 0; f.route = 0xFF;
    r.route_write_approved = true;
    CHECK(trial_end(&r, &r.token, REC_NORMAL) < 0);
    CHECK(writes() == 1 && count('A', RECOVER_ROUTE_PORT) == 1 && r.s0_verified);
}
void _start(void)
{
    preconditions(); normal(); mismatches(); timing(); failures(); cold_and_config(); routes();
    output(fails ? "recover_host: FAIL\n" : "recover_host: PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(fails ? 1 : 0) : "memory");
    for (;;) { }
}
