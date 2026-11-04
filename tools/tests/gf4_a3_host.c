/* A3 wire/session slice: UNIT 14 supplies helpers but no dependency stubs.
 * Compile this TU alone, -m32 -nostdlib, with include/ on the include path.
 * The local slot callbacks model C3/C2's public contract, not hardware.
 * A2 owns registration/CHECK inputs and mutation execution at integration. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#ifndef GF4_A3_SOURCE
#define GF4_A3_SOURCE "../../kdebug/gf_display_test.c"
#endif
#include GF4_A3_SOURCE

enum session_event { SESSION_SAVE, SESSION_RESTORE, SESSION_RELEASE };
/* One start + initial restore + final restore/release, with retry room. */
#define SESSION_EVENTS 16U
#define SESSION_NOW 123U
#define SESSION_FAILURE (-777)
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_test_ops normal, replacement;
static struct gf4_restore_ops management, replacement_management;
static const struct gf4_test_ops *normal_slot;
static const struct gf4_restore_ops *management_slot;
static enum session_event events[SESSION_EVENTS];
static u32 nevents, normal_reads, management_reads;
static i32 save_rc, restore_rc, release_rc, clock_rc;
static bool publish, swap_after_restore, swap_after_clock;

const struct gf4_test_ops *geforce_display_ops(void)
{
    ++normal_reads;
    return normal_slot;
}
const struct gf4_restore_ops *geforce_display_restore_ops(void)
{
    ++management_reads;
    return management_slot;
}
static void event(enum session_event e, struct gf4_ctx *ctx,
                  const struct gf4_saved *saved)
{
    assert(ctx == &host.ctx && saved == &display_session.saved);
    assert(nevents < SESSION_EVENTS);
    events[nevents++] = e;
}
static int clock_read(void *owner, u32 *now)
{
    assert(owner == &host);
    *now = SESSION_NOW;
    if (swap_after_clock) {
        normal_slot = &replacement;
        management_slot = &replacement_management;
    }
    return clock_rc;
}
static i32 save_call(struct gf4_ctx *ctx, struct gf4_saved *saved, u32 dl)
{
    event(SESSION_SAVE, ctx, saved);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    if (save_rc) return save_rc;
    saved->version = GF4_SAVED_VERSION; saved->size = sizeof(*saved);
    ctx->diagnostic_saved = saved; ctx->saved_closed = true;
    normal_slot = NULL;
    return 0;
}
static i32 restore_call(struct gf4_ctx *ctx, const struct gf4_saved *saved, u32 dl)
{
    event(SESSION_RESTORE, ctx, saved);
    assert(dl == SESSION_NOW + GF3_TIMEOUT_US);
    assert(saved->version == GF4_SAVED_VERSION && saved->size == sizeof(*saved));
    assert(ctx->diagnostic_saved == saved && display_session.valid);
    if (swap_after_restore) management_slot = &replacement_management;
    if (restore_rc) return restore_rc;
    ctx->saved_closed = false;
    normal_slot = publish ? &normal : NULL;
    return 0;
}
static i32 release_call(struct gf4_ctx *ctx, const struct gf4_saved *saved)
{
    event(SESSION_RELEASE, ctx, saved);
    assert(!ctx->saved_closed && display_session.valid);
    if (release_rc) return release_rc;
    ctx->diagnostic_saved = NULL;
    return 0;
}
static void reset(void)
{
    gf4_host_init(&host, &state);
    gf2_host_memset(&display_session, 0, sizeof(display_session));
    normal = (struct gf4_test_ops){.ctx=&host.ctx, .display_save=save_call};
    management = (struct gf4_restore_ops){.ctx=&host.ctx,
        .restore=restore_call, .saved_release=release_call};
    replacement = (struct gf4_test_ops){.ctx=&host.ctx};
    replacement_management = (struct gf4_restore_ops){.ctx=&host.ctx};
    normal_slot = &normal; management_slot = &management;
    host.base.f.io.timing.clock_us = clock_read;
    nevents = normal_reads = management_reads = 0;
    save_rc = restore_rc = release_rc = clock_rc = 0;
    publish = true; swap_after_restore = swap_after_clock = false;
}
static struct display_request request(void)
{
    /* Literal bit oracle from A3-case: connector 0xa5, head1, phase0. */
    struct display_request q;
    assert(!display_decode(0x1a501U, &q));
    assert(q.test == DISPLAY_MODE && q.connector == 0xa5 && q.head == 1 && !q.phase);
    return q;
}
static void wire_and_admission(void)
{
    reset();
    struct display_request q = request(), before = q;
    for (u32 bit = 18; bit < BITS_PER_U32; ++bit) {
        assert(display_decode(1U << bit, &q) == GF4_EINVAL);
        assert(!gf2_host_memcmp(&q, &before, sizeof(q)));
    }
    for (u32 c = 9; c <= U8_MAX; ++c)
        assert(display_decode(c, &q) == GF4_EINVAL);
    for (u32 c = 0; c <= 8; ++c) {
        assert(!display_decode(0x3ff00U | c, &q));
        assert(q.test == c && q.connector == U8_MAX && q.head == 1 && q.phase);
    }
    assert(display_admit(&q) == 0); /* RESTORE at any time. */
    q = request(); q.phase = true;
    assert(display_admit(&q) == GF4_ESTATE && !nevents);
    q.phase = false; assert(!display_begin(&q));
    assert(nevents == 2 && events[0] == SESSION_SAVE && events[1] == SESSION_RESTORE);
    assert(display_session.valid && display_session.running && normal_slot);
    assert(display_admit(&q) == GF4_EBUSY);
    q.phase = true; assert(!display_admit(&q));
    ++q.test; assert(display_admit(&q) == GF4_EBUSY); --q.test;
    ++q.connector; assert(display_admit(&q) == GF4_EBUSY); --q.connector;
    q.head = 0; assert(display_admit(&q) == GF4_EBUSY);
    q.test = DISPLAY_RESTORE; assert(!display_admit(&q));
    assert(!display_restore(true) && !display_session.valid && !host.ctx.diagnostic_saved);
    assert(nevents == 4 && events[2] == SESSION_RESTORE && events[3] == SESSION_RELEASE);
    assert(!display_restore(true) && nevents == 4);
}
static void start_failures(void)
{
    reset(); struct display_request q = request();
    normal_slot = NULL; assert(display_begin(&q) == GF4_ESTATE && !nevents);
    normal_slot = &normal; host.ctx.saved_closed = true;
    assert(display_begin(&q) == GF4_ESTATE && !nevents);
    host.ctx.saved_closed = false; clock_rc = SESSION_FAILURE;
    assert(display_begin(&q) == SESSION_FAILURE && !nevents);
    clock_rc = 0; swap_after_clock = true;
    assert(display_begin(&q) == GF4_ESTATE && !nevents);
    swap_after_clock = false; normal_slot = &normal; management_slot = &management;
    save_rc = SESSION_FAILURE;
    assert(display_begin(&q) == SESSION_FAILURE && !display_session.valid);
    assert(nevents == 1 && events[0] == SESSION_SAVE);
    reset(); q = request(); restore_rc = SESSION_FAILURE;
    assert(display_begin(&q) == SESSION_FAILURE && display_session.valid);
    assert(!display_session.running && !normal_slot && host.ctx.diagnostic_saved);
    q.phase = true; assert(display_admit(&q) == GF4_ESTATE);
    restore_rc = 0; assert(!display_restore(true) && !display_session.valid);
    reset(); q = request(); publish = false;
    assert(display_begin(&q) == GF4_ESTATE && display_session.valid);
    assert(!display_session.running && nevents == 2);
    publish = true; assert(!display_restore(true));
}
static void restore_retries(void)
{
    reset(); struct display_request q = request(); assert(!display_begin(&q));
    static struct gf4_saved before;
    gf2_host_memcpy(&before, &display_session.saved, sizeof(before));
    management_slot = NULL;
    assert(display_restore(true) == GF4_ESTATE && nevents == 2);
    management_slot = &management; clock_rc = SESSION_FAILURE;
    assert(display_restore(true) == SESSION_FAILURE && nevents == 2);
    clock_rc = 0; swap_after_clock = true;
    assert(display_restore(true) == GF4_ESTATE && nevents == 2);
    swap_after_clock = false; management_slot = &management; restore_rc = SESSION_FAILURE;
    assert(display_restore(true) == SESSION_FAILURE && nevents == 3);
    assert(events[2] == SESSION_RESTORE && display_session.valid);
    restore_rc = 0; swap_after_restore = true;
    assert(display_restore(true) == GF4_ESTATE && nevents == 4);
    assert(display_session.valid && host.ctx.diagnostic_saved);
    swap_after_restore = false; management_slot = &management; release_rc = SESSION_FAILURE;
    assert(display_restore(true) == SESSION_FAILURE && nevents == 6);
    assert(events[5] == SESSION_RELEASE && display_session.valid);
    assert(!gf2_host_memcmp(&before, &display_session.saved, sizeof(before)));
    release_rc = 0; assert(!display_restore(true));
    assert(!display_session.valid && !display_session.running && !host.ctx.diagnostic_saved);
}
void _start(void)
{
    wire_and_admission(); start_failures(); restore_retries();
    gf2_host_exit(0);
}
