/* Link the real command_test.c as a separate TU. Dependencies below model
 * C0/B ownership and completion; no product source is included or replaced.
 * Test names match REVIEW_r1's proposed runtime mutation targets. */
#include "gf_command_runtime_host.h"
#include "../../hwdev/gf/command.h"
#include "../../hwdev/gf/gf2_test.h"
#include "../../hwdev/gf/accel_service.h"
/* This layer-2 oracle has no registered layer 3. Real production lifecycle
 * is covered by gf_accel_wire_host with the actual service implementation. */
struct gf3_service *gf3_service_get(struct gf2_chan *ch) { (void)ch; return 0; }
i32 gf3_service_start(struct gf3_service *s, u32 dl)
{ (void)s;(void)dl; __builtin_trap(); }
i32 gf3_service_stop(struct gf3_service *s) { (void)s; __builtin_trap(); }
i32 gf3_service_recover(struct gf3_service *s, u32 dl)
{ (void)s;(void)dl; __builtin_trap(); }
#ifdef NDEBUG
#error "T2a1's oracle requires assertions"
#endif

enum event_kind { FENCE_EMIT, FENCE_WAIT, PAUSE, OBJECT, RESUME, EMIT, KICK, WAIT };
static struct event { enum event_kind kind; u32 a, b; } events[64];
static unsigned nevents;
static struct gf_foundation diagnostic_f, production_f;
static struct gf2_chan *diagnostic_ch, production_ch;
static struct gf2_object *object;
static struct recover_bar_extent bars[RECOVER_BAR_COUNT];
static const struct gf_instmem_map_ops map_ops = {0}, other_map_ops = {0};
static const struct gf2_dma_ops dma_ops = {0};
static struct gf2_test_reply reply;
static struct t2a1_model {
    u32 now, deadline, waits, fault_after;
    unsigned reads, snapshots, faults, starts, stops, recovers, creates, programs;
    int fence_emit_rc, fence_wait_rc, kick_rc, stop_rc;
    bool inject_fault, kicked, paused, live;
} m;

static void log_event(enum event_kind kind, u32 a, u32 b)
{
    assert(nevents < sizeof(events) / sizeof(events[0]));
    events[nevents++] = (struct event){kind, a, b};
}
static void expect(unsigned i, enum event_kind kind, u32 a, u32 b)
{
    assert(i < nevents && events[i].kind == kind);
    assert(events[i].a == a && events[i].b == b);
}
static int read_reg(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    assert(owner == &diagnostic_f || owner == &production_f);
    assert(!((struct gf_foundation *)owner)->revoked);
    assert(bar == 0 && width == 4);
    assert(off == 0x00c00040U || off == 0x00c00044U || off == 0x00c00048U);
    *out = off; ++m.reads; return 0;
}
static int clock_us(void *owner, u32 *out)
{
    assert(owner == &diagnostic_f && !diagnostic_f.revoked);
    *out = m.now; return 0;
}
static int wait_us(void *owner, u32 us)
{
    assert(owner == &diagnostic_f && !diagnostic_f.revoked);
    assert(us >= 1 && us <= 50 && m.waits < 8);
    log_event(WAIT, us, 0); m.now += us; ++m.waits; return 0;
}
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *out)
{
    assert(!f->revoked); *out = f->stats; ++m.snapshots;
}
const struct gf2_dma_ops *geforce_dma_ops(void) { return &dma_ops; }
int gf2_grctx_program(u32 code[GF2_GRCTX_PROGRAM_WORDS], u32 *words, u32 *bytes)
{ code[0] = 0x123; *words = 3; *bytes = 64; ++m.programs; return 0; }
int gf2_start(struct gf2_chan *ch, struct gf_foundation *f)
{
    assert(!f->revoked && ch->state == GF2_OFF); ++m.starts;
    if (f == &diagnostic_f) {
        diagnostic_ch = ch;
        assert(ch->f == f && ch->map_ops == &map_ops && ch->map_owner == f);
        assert(ch->dma_ops == &dma_ops);
        assert(!gf2_host_memcmp(&ch->bar1, &bars[GF_MODE_VRAM_BAR], sizeof(ch->bar1)));
    } else assert(ch == &production_ch && f == &production_f);
    ch->f = f; ch->state = GF2_RUN; ++ch->epoch;
    ch->fault = (struct gf2_fault){0};
    ch->grctx = (struct gf_inst){.off = 0x40000, .size = 64};
    f->running = true; return 0;
}
static void release_object(struct gf2_chan *ch)
{
    if (m.live && ch == diagnostic_ch) {
        assert(ch->f->imem.used == &object->inst);
        ch->f->imem.used = object->inst.next;
        object->inst.size = 0; /* C0 frees instances, retaining handle value. */
        m.live = false;
    }
}
int gf2_stop(struct gf2_chan *ch)
{
    assert(!ch->f->revoked); ++m.stops;
    if (m.stop_rc) { ch->state = GF2_FAULT; return m.stop_rc; }
    release_object(ch); ch->state = GF2_OFF; ch->f->running = false; return 0;
}
int gf2_recover(struct gf2_chan *ch)
{
    assert(!ch->f->revoked); ++m.recovers;
    release_object(ch); ++ch->epoch; ch->state = GF2_RUN;
    ch->fault = (struct gf2_fault){0}; return 0;
}
int gf2_fence_emit(struct gf2_chan *ch, struct gf2_fence *out, u32 deadline)
{
    assert(!ch->f->revoked && deadline == m.deadline);
    log_event(FENCE_EMIT, deadline, 0);
    *out = (struct gf2_fence){ch->epoch, ++ch->seq}; return m.fence_emit_rc;
}
int gf2_fence_wait(struct gf2_chan *ch, struct gf2_fence fence, u32 deadline)
{
    assert(!ch->f->revoked && deadline == m.deadline);
    assert(fence.epoch == ch->epoch && fence.seq == ch->seq);
    log_event(FENCE_WAIT, deadline, 0); return m.fence_wait_rc;
}
int gf2_fifo_pause(struct gf2_chan *ch)
{
    assert(ch == diagnostic_ch && !m.paused && nevents >= 2);
    expect(nevents - 1, FENCE_WAIT, m.deadline, 0);
    m.paused = true; log_event(PAUSE, 0, 0); return 0;
}
void gf2_fifo_resume(struct gf2_chan *ch)
{ assert(ch == diagnostic_ch && m.paused); m.paused = false; log_event(RESUME, 0, 0); }
int gf2_gr_object_new(struct gf2_chan *ch, u32 handle, u16 class_id,
                      struct gf2_object *out)
{
    assert(ch == diagnostic_ch && m.paused && !m.live);
    assert(handle == 0x80000100U && class_id == 0x0039U);
    assert(!object || object == out); object = out;
    *out = (struct gf2_object){.handle = handle, .class_id = class_id,
        .inst = {.off = 0x40100, .size = 20, .next = ch->f->imem.used}};
    ch->f->imem.used = &out->inst; m.live = true; ++m.creates;
    log_event(OBJECT, handle, class_id); return 0;
}
int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool noninc,
             const u32 *data, u16 count, u32 deadline)
{
    assert(ch->state == GF2_RUN && !ch->f->revoked && !m.paused);
    assert(subc == 0 && !noninc && count == 1 && deadline == m.deadline);
    log_event(EMIT, method, *data); return 0;
}
int gf2_kick(struct gf2_chan *ch)
{
    assert(ch->state == GF2_RUN && !ch->f->revoked);
    log_event(KICK, 0, 0); m.kicked = true; return m.kick_rc;
}
int gf2_fault_check(struct gf2_chan *ch)
{
    assert(!ch->f->revoked); ++m.faults;
    if (m.inject_fault && m.kicked && m.waits >= m.fault_after) {
        ch->state = GF2_FAULT;
        ch->fault = (struct gf2_fault){.latched = true, .epoch = ch->epoch,
            .gr_class = 0x39, .first = {.fifo = 7}, .latest = {.fifo = 9}};
    }
    return ch->state == GF2_FAULT ? GF2_EFAULT : GF2_OK;
}
static int invoke(struct gf_foundation *f, enum gf2_test_op op)
{
    const struct gf2_test_request q = {sizeof(q), op, m.deadline};
    int rc = gf2_module_ops.call(f, &q, &reply);
    assert(rc == reply.rc); return rc;
}
static void clear_events(void)
{ nevents = 0; m.waits = m.faults = 0; m.kicked = false; }
static void fresh(void)
{
    if (diagnostic_ch) {
        diagnostic_f.revoked = false; m.stop_rc = 0;
        assert(gf2_stop(diagnostic_ch) == 0);
    }
    m = (struct t2a1_model){.now = 100, .deadline = 200,
                           .inject_fault = true, .fault_after = 1};
    diagnostic_f = (struct gf_foundation){.map_ops = &map_ops,
        .map_owner = &diagnostic_f, .stats = {.fifo = 11, .graph = 13},
        .io = {.opaque = &diagnostic_f, .read = read_reg,
            .timing = {.clock_us = clock_us, .wait_us = wait_us}}};
    production_f = diagnostic_f; production_f.io.opaque = &production_f;
    production_ch = (struct gf2_chan){.f = &production_f};
    gf2_test_bind_production(&production_f, &production_ch);
    gf2_host_memset(bars, 0, sizeof(bars));
    bars[GF_MODE_VRAM_BAR] = (struct recover_bar_extent){
        0x90000008, 0x90000000, 0x4000000, 1};
    assert(gf2_module_ops.prepare(&diagnostic_f, bars, RECOVER_BAR_COUNT) == 0);
    assert(invoke(&diagnostic_f, GF2T_START) == 0);
    assert(m.starts == 1 && reply.state == GF2_RUN && reply.ctx_words == 3);
    clear_events();
}
static void t2a1_bad_handle(void)
{
    fresh(); assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_EFAULT);
    expect(0, EMIT, 0, 0xdeadbeefU); expect(1, KICK, 0, 0);
    assert(nevents == 3 && !m.creates);
}
static void t2a1_bad_method(void)
{
    fresh(); assert(invoke(&diagnostic_f, GF2T_BAD_METHOD) == GF2_EFAULT);
    expect(0, FENCE_EMIT, 200, 0); expect(1, FENCE_WAIT, 200, 0);
    expect(2, PAUSE, 0, 0); expect(3, OBJECT, 0x80000100U, 0x0039);
    expect(4, RESUME, 0, 0); expect(5, EMIT, 0, 0x80000100U);
    expect(6, EMIT, 0x1ffc, 0); expect(7, KICK, 0, 0);
    assert(nevents == 9 && m.creates == 1 && m.live);
}
static void t2a1_recover_recreate(void)
{
    fresh(); m.inject_fault = false; m.deadline = 101;
    assert(invoke(&diagnostic_f, GF2T_BAD_METHOD) == GF2_ETIME);
    struct gf2_object *saved = object;
    unsigned creates = m.creates; u32 epoch = reply.epoch;
    clear_events(); m.deadline = m.now + 1;
    assert(invoke(&diagnostic_f, GF2T_BAD_METHOD) == GF2_ETIME);
    assert(m.creates == creates && nevents == 4);
    expect(0, EMIT, 0, 0x80000100U); expect(1, EMIT, 0x1ffc, 0);
    assert(invoke(&diagnostic_f, GF2T_RECOVER) == 0 && m.recovers == 1);
    assert(!m.live && reply.epoch == epoch + 1 && saved->handle == 0x80000100U);
    clear_events(); m.deadline = m.now + 1;
    assert(invoke(&diagnostic_f, GF2T_BAD_METHOD) == GF2_ETIME);
    assert(m.creates == creates + 1 && m.live && object == saved);
    expect(3, OBJECT, 0x80000100U, 0x0039);
}
static void t2a1_production_reject(void)
{
    fresh();
    production_ch.state = GF2_RUN; production_ch.epoch = 27;
    production_f.running = true;
    struct gf2_chan before = production_ch;
    gf2_test_bind_production(&production_f, &production_ch);
    assert(!gf2_host_memcmp(&before, &production_ch, sizeof(before)));
    assert(gf2_module_ops.prepare(&production_f, bars, RECOVER_BAR_COUNT) == GF2_EINVAL);
    for (enum gf2_test_op op = GF2T_BAD_HANDLE; op <= GF2T_BAD_METHOD; ++op) {
        assert(invoke(&production_f, op) == GF2_ESTATE);
        assert(!nevents && !m.faults && !m.creates);
        assert(!gf2_host_memcmp(&before, &production_ch, sizeof(before)));
    }
}
static void t2a1_failed_stop_prepare(void)
{
    fresh(); m.inject_fault = false; m.deadline = 101;
    assert(invoke(&diagnostic_f, GF2T_BAD_METHOD) == GF2_ETIME);
    m.stop_rc = GF2_ESTOP;
    assert(invoke(&diagnostic_f, GF2T_STOP) == GF2_ESTOP && m.stops == 1);
    struct gf2_chan before = *diagnostic_ch;
    struct gf2_object obj_before = *object;
    struct gf_foundation replacement = diagnostic_f;
    replacement.map_ops = &other_map_ops; replacement.map_owner = &replacement;
    assert(gf2_module_ops.prepare(&replacement, bars, RECOVER_BAR_COUNT) == GF2_ESTATE);
    assert(!gf2_host_memcmp(&before, diagnostic_ch, sizeof(before)));
    assert(!gf2_host_memcmp(&obj_before, object, sizeof(obj_before)) && m.live);
    assert(diagnostic_f.imem.used == &object->inst);
}
static void t2a1_revoked_saved_only(void)
{
    fresh(); assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_EFAULT);
    struct gf2_test_reply saved = reply;
    assert(saved.latched && saved.gr_class == 0x39 && saved.first.fifo == 7);
    assert(saved.latest.fifo == 9 && saved.stats.fifo == 11);
    assert(saved.put == 0x00c00040U && saved.get == 0x00c00044U);
    assert(saved.reference == 0x00c00048U);
    diagnostic_f.revoked = true;
    struct t2a1_model before = m; clear_events(); before.waits = before.faults = 0;
    before.kicked = false;
    for (enum gf2_test_op op = GF2T_START; op <= GF2T_BAD_METHOD; ++op) {
        assert(invoke(&diagnostic_f, op) == GF2_EFAULT);
        assert(!gf2_host_memcmp(&reply, &saved, sizeof(saved)));
        assert(!gf2_host_memcmp(&m, &before, sizeof(m)) && !nevents);
    }
}
static void t2a1_wait_for_fault(void)
{
    fresh(); m.fault_after = 3; m.deadline = 1000;
    assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_EFAULT);
    assert(m.waits == 3 && m.faults == 5 && nevents == 5);
    for (unsigned i = 2; i < 5; ++i) expect(i, WAIT, 50, 0);
}
static void t2a1_reference_completion(void)
{
    fresh(); m.fence_wait_rc = GF2_ETIME;
    assert(invoke(&diagnostic_f, GF2T_REFERENCE) == GF2_ETIME);
    assert(nevents == 2); expect(0, FENCE_EMIT, 200, 0); expect(1, FENCE_WAIT, 200, 0);
    clear_events(); m.fence_emit_rc = GF2_EIO;
    assert(invoke(&diagnostic_f, GF2T_REFERENCE) == GF2_EIO && nevents == 1);
    clear_events(); m.fence_emit_rc = m.fence_wait_rc = 0;
    assert(invoke(&production_f, GF2T_REFERENCE) == GF2_ESTATE && !nevents);
    assert(invoke(&diagnostic_f, GF2T_REFERENCE) == 0 && nevents == 2);
}
static void t2a1_deadline_wrap(void)
{
    fresh(); m.now = 0xfffffff0U; m.deadline = 0x10;
    assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_EFAULT);
    assert(m.waits == 1 && m.now == 0x10); expect(2, WAIT, 32, 0);
    fresh(); m.now = m.deadline;
    assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_ETIME && !m.waits);
}
static void t2a1_bad_packet_kick(void)
{
    fresh(); m.kick_rc = GF2_EIO;
    assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_EIO);
    assert(nevents == 2 && !m.waits && m.faults == 1); expect(1, KICK, 0, 0);
}
static void t2a1_dispatch(void)
{
    fresh();
    assert(invoke(&diagnostic_f, GF2T_SNAPSHOT) == 0 && m.faults == 1);
    assert(invoke(&diagnostic_f, GF2T_STOP) == 0 && m.stops == 1);
    assert(reply.state == GF2_OFF);
    clear_events();
    assert(invoke(&diagnostic_f, GF2T_BAD_HANDLE) == GF2_ESTATE);
    assert(invoke(&diagnostic_f, GF2T_BAD_METHOD) == GF2_ESTATE);
    assert(!nevents && !m.faults);
    assert(gf2_module_ops.prepare(&diagnostic_f, bars, RECOVER_BAR_COUNT) == 0);
    assert(invoke(&diagnostic_f, GF2T_START) == 0 && m.starts == 2);
    struct gf2_test_request q = {0, GF2T_START, 0};
    assert(gf2_module_ops.call(&diagnostic_f, &q, &reply) == GF2_EINVAL);
    q.size = sizeof(q); q.op = (enum gf2_test_op)-1;
    assert(gf2_module_ops.call(&diagnostic_f, &q, &reply) == GF2_EINVAL);
    assert(gf2_module_ops.call(&diagnostic_f, 0, &reply) == GF2_EINVAL);
    assert(gf2_module_ops.call(&diagnostic_f, &q, 0) == GF2_EINVAL);
}
void _start(void)
{
    t2a1_bad_handle(); t2a1_bad_method(); t2a1_recover_recreate();
    t2a1_production_reject(); t2a1_failed_stop_prepare();
    t2a1_revoked_saved_only(); t2a1_wait_for_fault();
    t2a1_reference_completion(); t2a1_deadline_wrap();
    t2a1_bad_packet_kick(); t2a1_dispatch();
    assert(gf2_stop(diagnostic_ch) == 0); gf2_host_exit(0);
}
