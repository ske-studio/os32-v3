/* Real T2 observer, with typed module/trial/clock/serial dependencies only.
 * No channel implementation is linked: acceptance must reject bad replies. */
#include <assert.h>
#include <string.h>
#include "../../include/io.h"
#define _irq_enabled() 1
#ifndef GF2_T2B_SOURCE
#define GF2_T2B_SOURCE "../../kdebug/gf_command_test.c"
#endif
#include GF2_T2B_SOURCE

static struct gf_foundation device;
static struct recover trial;
static struct gf2_test_reply saved;
static u32 now_us, last_clock, reference_calls, recover_calls;
static bool stalled, no_wrap, same_epoch, no_fault, changed_display, deadline_wrapped;
static char serial[32768];
static unsigned serial_used;

void serial_puts_polled(const char *s)
{
    unsigned n = strlen(s);
    assert(serial_used + n < sizeof(serial));
    memcpy(serial + serial_used, s, n + 1);
    serial_used += n;
}
u32 pci_cfg_read32(u32 bus, u32 dev, u32 fn, u32 offset)
{
    assert(bus == RECOVER_GF_BUS && dev == RECOVER_GF_DEV && fn == RECOVER_GF_FN);
    assert(offset == PCI_CFG_VENDOR_ID);
    return RECOVER_GF_DEVICE;
}
struct recover *recover_owner(void) { return &trial; }
int recover_safe_point(struct recover *r)
{
    assert(r == &trial && r->state == REC_RUNNING);
    return 0;
}
int trial_step(struct recover *r, const struct recover_token *token, bool approved,
               int (*body)(struct recover *, void *), void *arg)
{
    assert(r == &trial && token == &trial.token && approved && r->state == REC_PAUSED);
    r->state = REC_RUNNING;
    int rc = body(r, arg);
    r->state = REC_PAUSED;
    return rc;
}
static int fake_clock(void *opaque, u32 *out)
{
    assert(opaque == &device);
    *out = last_clock = now_us;
    now_us += 100U;
    return 0;
}
static int fake_wait(void *opaque, u32 us)
{
    assert(opaque == &device && us <= GF2_WAIT_MAX_US);
    now_us += us;
    if (device.wanted_heads & 1U) saved.stats.vblank[0]++;
    if (device.wanted_heads & 2U) saved.stats.vblank[1]++;
    return 0;
}
static int fake_read(void *opaque, u32 bar, u32 off, u32 width, u32 *value)
{
    assert(opaque == &device && bar == GF_MODE_REGISTER_BAR && width == 4);
    assert(off == 0x2210U || off == 0x2220U);
    *value = 0;
    return 0;
}
static int fake_heads(struct gf_foundation *f, const struct gf1_test_request *q,
                      struct gf1_test_reply *reply)
{
    assert(f == &device && q->size == sizeof(*q) && q->op == GF1T_HEADS);
    f->wanted_heads = q->heads;
    *reply = (struct gf1_test_reply){0};
    return 0;
}
static int fake_command(struct gf_foundation *f, const struct gf2_test_request *q,
                        struct gf2_test_reply *reply)
{
    assert(f == &device && q->size == sizeof(*q));
    /* Independent timeout oracle, including modulo-u32 addition. */
    if (q->deadline_us - last_clock != 2000000U) return GF2_ETIME;
    if (q->deadline_us < last_clock) deadline_wrapped = true;
    int rc = GF2_OK;
    switch (q->op) {
    case GF2T_START:
        saved.state = GF2_RUN;
        break;
    case GF2T_REFERENCE:
        assert(saved.state == GF2_RUN);
        reference_calls++;
        saved.seq++;
        saved.put += 4U;
        if (!no_wrap) saved.put %= 65536U;
        saved.get = saved.put;
        saved.reference = stalled ? saved.seq - 1U : saved.seq;
        break;
    case GF2T_STOP:
        saved.state = GF2_OFF;
        break;
    case GF2T_RECOVER:
        recover_calls++;
        if (!same_epoch) saved.epoch++;
        saved.state = GF2_RUN;
        saved.latched = false;
        saved.reference = saved.seq;
        break;
    case GF2T_BAD_HANDLE:
    case GF2T_BAD_METHOD:
        saved.state = GF2_FAULT;
        saved.latched = true;
        saved.fault_epoch = saved.epoch;
        rc = no_fault ? GF2_OK : GF2_EFAULT;
        break;
    case GF2T_SNAPSHOT:
        rc = saved.latched ? GF2_EFAULT : GF2_OK;
        break;
    }
    *reply = saved;
    return rc;
}
static const struct gf2_test_ops commands = {.call = fake_command};
static const struct gf1_test_ops foundations = {.call = fake_heads, .command = &commands};
const struct gf1_test_ops *geforce_foundation_ops(void) { return &foundations; }
static int fake_row(void *opaque, u32 y, u8 *row)
{
    assert(opaque == &device && y < GF_MODE_HEIGHT);
    memset(row, changed_display && recover_calls ? 1 : 0, GF_MODE_WIDTH);
    return 0;
}
static void reset(void)
{
    device = (struct gf_foundation){0};
    device.io.opaque = &device;
    device.io.timing.clock_us = fake_clock;
    device.io.timing.wait_us = fake_wait;
    device.io.read = fake_read;
    device.wanted_heads = 3U;
    trial = (struct recover){.state = REC_PAUSED};
    trial.token.session = 19;
    saved = (struct gf2_test_reply){.state = GF2_RUN, .epoch = 41};
    now_us = 100;
    last_clock = reference_calls = recover_calls = 0;
    stalled = no_wrap = same_epoch = no_fault = changed_display = deadline_wrapped = false;
    serial_used = 0;
    serial[0] = 0;
}
static int request(u32 test)
{
    struct gf2_request q = {sizeof(q), test, 0, 0};
    int rc = gf_command_test(&q, &device, fake_row, &device);
    assert(q.rc == rc && q.session == trial.token.session);
    return rc;
}
static void t2b_reference_count(void)
{
    reset();
    assert(request(GF2_REFERENCE) == GF2_OK);
    assert(reference_calls == 100000U);
    assert(strstr(serial, "completed=100000 "));
}
static void t2b_reference_stalled(void)
{
    reset();stalled = true;
    assert(request(GF2_REFERENCE) == GF2_EIO);
    assert(saved.get == saved.put && saved.reference != saved.seq);
}
static void t2b_wrap_required(void)
{
    reset();no_wrap = true;
    assert(request(GF2_REFERENCE) == GF2_EIO);
    assert(reference_calls == 100000U && strstr(serial, " wraps=0 "));
}
static void t2b_recover_epoch(void)
{
    reset();
    assert(request(GF2_RECOVER) == GF2_OK && saved.epoch == 42);
    reset();same_epoch = true;
    assert(request(GF2_RECOVER) == GF2_EIO && recover_calls == 1);
}
static void t2b_fault_recover(void)
{
    const u32 cases[] = {GF2_BAD_HANDLE, GF2_BAD_METHOD};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset();
        assert(request(cases[i]) == GF2_OK);
        assert(recover_calls == 1 && reference_calls == 100000U && saved.epoch == 42);
        reset();no_fault = true;
        assert(request(cases[i]) == GF2_EIO);
        assert(!recover_calls && !reference_calls);
    }
}
static void t2b_display_changed(void)
{
    reset();
    assert(request(GF2_CYCLE) == GF2_OK);
    assert(reference_calls == 200000U && device.wanted_heads == 3U);
    reset();changed_display = true;
    assert(request(GF2_CYCLE) == GF2_EIO);
    assert(strstr(serial, "GF2 phase=display "));
}
static void t2b_deadline_wrap(void)
{
    reset();now_us = 0xfffffff0U;
    assert(request(GF2_START) == GF2_OK && deadline_wrapped);
}
static void t2b_fault_subchannel(void)
{
    reset();saved.state = GF2_FAULT;saved.latched = true;
    saved.gr_trap = 2U << 16;
    saved.gr_method = (5U << 16) | 0x1ffcU;
    assert(request(GF2_STATUS) == GF2_OK);
    assert(strstr(serial, " subc=5 "));
    assert(!strstr(serial, " subc=2 "));
}
int main(void)
{
    t2b_reference_count();t2b_reference_stalled();t2b_wrap_required();
    t2b_recover_epoch();t2b_fault_recover();t2b_display_changed();
    t2b_deadline_wrap();t2b_fault_subchannel();
    return 0;
}
