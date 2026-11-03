/* Link fence.c only. These B1/B7 stubs can move to A2 when it is available. */
#include <assert.h>
#include "../../hwdev/gf/command.h"

static u32 now, reference, get, emitted, deadline, reads, waits, faults, kicks;
static u32 general, internal, complete_after, fault_after;
static int emit_rc, kick_rc, clock_rc, read_rc, wait_rc;

int gf2_fault_check(struct gf2_chan *ch)
{
    faults++;
    if (ch->state == GF2_FAULT || (fault_after && faults >= fault_after)) {
        ch->state = GF2_FAULT; return GF2_EFAULT;
    }
    return GF2_OK;
}
int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool noninc,
             const u32 *data, u16 count, u32 dl)
{
    assert(ch->state == GF2_RUN && subc == 0 && method == 0x0050U);
    assert(!noninc && count == 1); general++;
    emitted = *data; deadline = dl; return emit_rc;
}
int gf2_push_reference(struct gf2_chan *ch, u32 seq, u32 dl)
{
    assert(ch->state == GF2_INIT || ch->state == GF2_DRAIN);
    internal++; emitted = seq; deadline = dl; return emit_rc;
}
int gf2_kick(struct gf2_chan *ch)
{
    int rc = gf2_fault_check(ch);
    if (rc) return rc;
    kicks++; return kick_rc;
}
static int read_stub(void *opaque, u32 bar, u32 off, u32 width, u32 *value)
{
    (void)opaque;
    assert(bar == 0 && width == 4);
    assert(off == 0x00c00048U || off == 0x00c00044U);
    reads++; *value = off == 0x00c00048U ? reference : get; return read_rc;
}
static int clock_stub(void *opaque, u32 *value)
{
    (void)opaque; *value = now; return clock_rc;
}
static int wait_stub(void *opaque, u32 us)
{
    (void)opaque;
    assert(us >= GF2_WAIT_MIN_US && us <= GF2_WAIT_MAX_US);
    now += us; waits++;
    if (complete_after && waits >= complete_after) reference = emitted;
    return wait_rc;
}

int main(void)
{
    struct gf_foundation f = {0};
    struct gf2_chan ch = {0};
    struct gf2_fence fence = {0}, saved;
    ch.f = &f; ch.epoch = 7;
    f.io.read = read_stub; f.io.timing.clock_us = clock_stub;
    f.io.timing.wait_us = wait_stub;
    for (unsigned i = GF2_INIT; i <= GF2_DRAIN; i++) {
        ch.state = (enum gf2_state)i;
        assert(gf2_fence_emit(&ch, &fence, 200) == GF2_OK);
        assert(fence.seq == i && emitted == i && fence.epoch == 7 && deadline == 200);
        reference = emitted;
        assert(gf2_fence_wait(&ch, fence, 200) == GF2_OK);
    }
    assert(general == 1 && internal == 2 && kicks == 3);
    ch.state = GF2_RUN; ch.seq = U32_MAX;
    assert(gf2_fence_emit(&ch, &fence, 90) == GF2_OK && fence.seq == 0);
    /* GET has caught PUT, but REFERENCE is still before the wrapped seq. */
    ch.B = 0x120000U; ch.cur = ch.put = GF2_PUSH_SKIPS + 2U;
    get = ch.B + 4U * ch.put;
    now = U32_MAX - 9U; reference = U32_MAX; waits = 0;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_ETIME);
    assert(now == 90 && waits == 2);
    now = U32_MAX - 9U; reference = U32_MAX; waits = 0; complete_after = 1;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_OK && waits == 1);
    complete_after = 0; reference = 0;
    fence.seq = U32_MAX;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_OK);
    fence.seq = 0; fence.epoch--;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_EFAULT);
    fence.epoch = ch.epoch;
    fault_after = faults + 1; reads = 0;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_EFAULT && reads == 0);
    fault_after = 0; ch.state = GF2_RUN; reference = U32_MAX;
    fault_after = faults + 2;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_EFAULT);
    fault_after = 0; ch.state = GF2_RUN; reference = 0; now = 0;
    clock_rc = -1; assert(gf2_fence_wait(&ch, fence, 90) == GF2_EIO); clock_rc = 0;
    read_rc = -1; assert(gf2_fence_wait(&ch, fence, 90) == GF2_EIO); read_rc = 0;
    reference = U32_MAX; wait_rc = -1;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_EIO); wait_rc = 0;
    now = 89; reference = U32_MAX;
    assert(gf2_fence_wait(&ch, fence, 90) == GF2_ETIME && now == 90);
    saved = fence; kicks = 0; emit_rc = GF2_ENOSPC;
    assert(gf2_fence_emit(&ch, &fence, 200) == GF2_ENOSPC && !kicks);
    emit_rc = 0; kick_rc = GF2_EIO;
    assert(gf2_fence_emit(&ch, &fence, 200) == GF2_EIO);
    assert(fence.epoch == saved.epoch && fence.seq == saved.seq);
    kick_rc = 0; fault_after = faults + 2; kicks = 0;
    assert(gf2_fence_emit(&ch, &fence, 200) == GF2_EFAULT && !kicks);
    fault_after = 0; ch.state = GF2_OFF;
    assert(gf2_fence_emit(&ch, &fence, 200) == GF2_ESTATE);
    assert(gf2_fence_wait(&ch, fence, 200) == GF2_ESTATE);
    ch.state = GF2_FAULT;
    assert(gf2_fence_emit(&ch, &fence, 200) == GF2_EFAULT);
    return 0;
}
