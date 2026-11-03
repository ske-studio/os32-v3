/* One product TU + A1 UNIT=7 dependencies, no libc or GPU emulation.
 * Independent packet oracle: nv04_exa.c:399-430 / design section 7, G3. */
#define GF3_HOST_UNIT 7
#include "gf_accel_host.h"

enum { PACKETS_PER_CHUNK = 4, WORDS_PER_CHUNK = 2 + 8 + 1 + 1,
       TRACE_CHUNKS = 4, EVENT_CAPACITY = 256,
       VRAM_CAPACITY = 16 * 1024 * 1024, INSTMEM_BYTES = 64 * 1024,
       DEADLINE = 1000 };
static struct gf3_host h;
static struct gf3_host_packet packets[TRACE_CHUNKS * PACKETS_PER_CHUNK];
static u32 words[TRACE_CHUNKS * WORDS_PER_CHUNK];
static struct gf3_host_event events[EVENT_CAPACITY];

static void setup(void)
{
    gf3_host_init(&h);
    h.packets = packets; h.cpackets = sizeof(packets)/sizeof(packets[0]);
    h.words = words; h.cwords = sizeof(words)/sizeof(words[0]);
    h.events = events; h.cevents = sizeof(events)/sizeof(events[0]);
    h.ch.B = VRAM_CAPACITY;
    h.f.imem.vram_bytes = VRAM_CAPACITY + INSTMEM_BYTES;
    h.f.imem.reserved = INSTMEM_BYTES;
    h.ctx.last_rc = GF3_EINVAL;
}

static int transfer(u32 ih, u32 oh, u32 io, u32 oo, u32 ip, u32 op,
                    u32 rowbytes, u32 rows)
{
    int rc = gf3_m2mf(&h.ctx, ih, oh, io, oo, ip, op, rowbytes, rows, DEADLINE);
    assert(h.ctx.last_rc == rc);
    return rc;
}

static int normal(u32 rows)
{
    return transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                    17, VRAM_CAPACITY/2, 11, 13, 7, rows);
}

static void packet(u32 index, u16 method, const u32 *data, u16 count)
{
    assert(index < h.npackets);
    const struct gf3_host_packet *p = &packets[index];
    assert(p->subc == 1 && p->method == method && !p->noninc);
    assert(p->count == count && p->deadline == DEADLINE);
    for (u32 i = 0; i < count; ++i) assert(words[p->first+i] == data[i]);
}

static void chunk(u32 index, u32 ih, u32 oh, u32 io, u32 oo,
                  u32 ip, u32 op, u32 rowbytes, u32 rows)
{
    u32 handles[] = {ih, oh};
    u32 values[] = {io, oo, ip, op, rowbytes, rows, 0x101, 0};
    u32 zero = 0;
    u32 first = index * PACKETS_PER_CHUNK;
    packet(first, 0x184, handles, 2);
    packet(first+1, 0x30c, values, 8);
    packet(first+2, 0x100, &zero, 1);
    packet(first+3, 0x310, &zero, 1);
}

static void synchronous(u32 chunks)
{
    assert(h.npackets == chunks * PACKETS_PER_CHUNK);
    assert(h.nwords == chunks * WORDS_PER_CHUNK);
    assert(h.calls[H_FENCE] == 1 && h.calls[H_WAIT] == 1);
    u32 emitted = 0, fences = 0, waits = 0, spans = 0;
    for (u32 i = 0; i < h.nevents; ++i) {
        struct gf3_host_event *e = &events[i];
        if (e->op == H_SPAN) { assert(!emitted); ++spans; }
        if (e->op == H_EMIT) {
            assert(spans == 2 && !fences && !waits);
            assert(e->a[4] == DEADLINE); ++emitted;
        }
        if (e->op == H_FENCE) {
            assert(emitted == chunks * PACKETS_PER_CHUNK && !waits);
            assert(e->a[0] == DEADLINE); ++fences;
        }
        if (e->op == H_WAIT) {
            assert(fences == 1 && e->a[0] == h.ch.epoch && e->a[1] == h.ch.seq);
            assert(e->a[2] == DEADLINE); ++waits;
        }
    }
    assert(fences == 1 && waits == 1);
}

static void split_packets(void)
{
    /* Literal expected split sizes are independent of the product limit. */
    static const u32 counts[] = {1, 2047, 2048, 4095};
    for (u32 k = 0; k < sizeof(counts)/sizeof(counts[0]); ++k) {
        setup(); assert(!normal(counts[k]));
        u32 left = counts[k], row = 0, n = 0;
        while (left) {
            u32 batch = left > 2047 ? 2047 : left;
            chunk(n++, GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                  17+row*11, VRAM_CAPACITY/2+row*13, 11, 13, 7, batch);
            row += batch; left -= batch;
        }
        synchronous(n);
    }
    /* Both PCI directions, staging's last byte, unequal pitches. */
    for (u32 direction = 0; direction < 2; ++direction) {
        setup();
        u32 ih = direction ? GF3_HANDLE_PCI_DMA : GF3_HANDLE_VRAM_DMA;
        u32 oh = direction ? GF3_HANDLE_VRAM_DMA : GF3_HANDLE_PCI_DMA;
        assert(!transfer(ih, oh, 0, 0, 1024, 1024, 1024, 4));
        chunk(0, ih, oh, 0, 0, 1024, 1024, 1024, 4); synchronous(1);
    }
    /* No unneeded trailing pitch; final advance would overflow a u32. */
    setup();
    assert(!transfer(GF3_HANDLE_PCI_DMA, GF3_HANDLE_VRAM_DMA,
                     GF3_STAGING_BYTES-1, VRAM_CAPACITY-1, U32_MAX, U32_MAX, 1, 1));
    chunk(0, GF3_HANDLE_PCI_DMA, GF3_HANDLE_VRAM_DMA,
          GF3_STAGING_BYTES-1, VRAM_CAPACITY-1, U32_MAX, U32_MAX, 1, 1);
    synchronous(1);
}

static void unsubmitted(int rc, int expected)
{
    assert(rc == expected && h.ctx.last_rc == expected);
    assert(!h.npackets && !h.calls[H_FENCE] && !h.calls[H_WAIT]);
}

static void validation(void)
{
    setup(); assert(gf3_m2mf(0, 0, 0, 0, 0, 0, 0, 1, 1, DEADLINE) == GF3_EINVAL);
    h.ctx.ch = 0; unsubmitted(normal(1), GF3_EINVAL);
    setup(); h.ch.f = 0; unsubmitted(normal(1), GF3_EINVAL);
    setup(); h.ctx.ready = false; unsubmitted(normal(0), GF3_ESTATE);
    setup(); ++h.ch.epoch; unsubmitted(normal(1), GF3_ESTATE);
    setup(); h.ch.state = GF2_DRAIN; unsubmitted(normal(1), GF3_ESTATE);
    setup(); h.ch.fault.latched = true; unsubmitted(normal(0), GF2_EFAULT);
    setup(); unsubmitted(normal(0), 0);
    setup(); unsubmitted(transfer(0, 0, U32_MAX, U32_MAX, 0, 0, 0, U32_MAX), 0);
    setup(); unsubmitted(transfer(0, GF3_HANDLE_VRAM_DMA, 0, 0, 1, 1, 1, 1), GF3_EINVAL);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, 0, 0, 0, 1, 1, 1, 1), GF3_EINVAL);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  0, 100, 6, 8, 7, 1), GF3_ERANGE);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  0, 100, 8, 6, 7, 1), GF3_ERANGE);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  0, 100, U32_MAX, U32_MAX, 1, U32_MAX), GF3_ERANGE);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  VRAM_CAPACITY, 0, 1, 1, 1, 1), GF3_ERANGE);
    setup(); h.ch.B = 20; unsubmitted(normal(1), GF3_ERANGE);
    setup(); h.f.imem.vram_bytes = INSTMEM_BYTES+20;
    unsubmitted(normal(1), GF3_ERANGE);
    setup(); h.f.imem.reserved = h.f.imem.vram_bytes+1;
    unsubmitted(normal(1), GF3_ERANGE);
    /* Envelopes intersect even though individual valid row bytes do not. */
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  0, 4, 8, 8, 1, 2), GF3_EUNSUPPORTED);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  4, 0, 8, 8, 1, 2), GF3_EUNSUPPORTED);
    setup(); unsubmitted(transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                                  0, 0, 1, 1, 1, 1), GF3_EUNSUPPORTED);
    setup(); assert(!transfer(GF3_HANDLE_VRAM_DMA, GF3_HANDLE_VRAM_DMA,
                              0, 1, 1, 1, 1, 1)); synchronous(1);
    for (u32 direction = 0; direction < 2; ++direction) {
        u32 ih = direction ? GF3_HANDLE_PCI_DMA : GF3_HANDLE_VRAM_DMA;
        u32 oh = direction ? GF3_HANDLE_VRAM_DMA : GF3_HANDLE_PCI_DMA;
        for (u32 bad = 0; bad < 5; ++bad) {
            setup();
            if (bad == 0) h.ch.staging_live = false;
            if (bad == 1) h.ch.staging.va = 0;
            if (bad == 2) h.ch.staging.size = 0;
            if (bad == 3) h.ch.staging.size = GF3_STAGING_BYTES+1;
            unsubmitted(transfer(ih, oh, 0, 0, 1024, 1024, 1024, 5),
                        bad < 3 ? GF2_ENOSPC : GF3_ERANGE);
        }
    }
    for (u32 nth = 1; nth <= 2; ++nth) {
        setup(); gf3_host_fail_at(&h, H_SPAN, nth, GF3_ERANGE);
        unsubmitted(normal(1), GF3_ERANGE); assert(h.calls[H_SPAN] == nth);
    }
}

static void failures(void)
{
    /* Two chunks: every emit failure stops immediately without a fence. */
    for (u32 nth = 1; nth <= 2 * PACKETS_PER_CHUNK; ++nth) {
        setup(); gf3_host_fail_at(&h, H_EMIT, nth, GF2_ETIME);
        assert(normal(2048) == GF2_ETIME);
        assert(h.calls[H_EMIT] == nth && h.npackets == nth-1);
        assert(!h.calls[H_FENCE] && !h.calls[H_WAIT]);
    }
    setup(); gf3_host_fail_at(&h, H_FAULT, 1, GF2_EFAULT);
    unsubmitted(normal(1), GF2_EFAULT);
    setup(); gf3_host_fail_at(&h, H_FENCE, 1, GF2_EIO);
    assert(normal(2048) == GF2_EIO && !h.calls[H_WAIT]);
    assert(h.npackets == 2 * PACKETS_PER_CHUNK && h.calls[H_FENCE] == 1);
    setup(); gf3_host_fail_at(&h, H_WAIT, 1, GF2_EFAULT);
    assert(normal(2048) == GF2_EFAULT); synchronous(2);
    setup(); gf3_host_fail_at(&h, H_FAULT, 2, GF2_EFAULT);
    assert(normal(2048) == GF2_EFAULT); synchronous(2);
    setup(); gf3_host_fail_at(&h, H_CLOCK, 1, GF1_EIO);
    assert(normal(2048) == GF2_EIO); synchronous(2);
    setup(); h.now = DEADLINE-2; h.complete_after = U32_MAX;
    assert(normal(2048) == GF2_ETIME && h.now == DEADLINE);
    synchronous(2); assert(h.waits == 2);
    setup(); h.complete_after = 3;
    assert(!normal(2048)); synchronous(2); assert(h.waits == 3);
}

void _start(void)
{
    split_packets(); validation(); failures();
    gf2_host_exit(0);
}
