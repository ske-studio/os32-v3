/* 期待の出所: PPC_RESET_DESIGN §3・§7。
 * NetBSD sys/arch/macppc/dev/cuda.c:325–361,559–735,887–899,
 * pmu.c:475–545,620–624,775–784; OpenBSD pm_direct.c:294–322. */
#include "via.c" /* Exercise the private receive contract as well as restart. */

enum { STALL = 1, PENDING = 2, COLLISION = 4, REPLY = 8, FREE_STALL = 16,
       OVERSIZE = 32, REPEAT = 64, FAKE = 128, NO_DATA = 256 };

struct bus {
    u8 b, ddr, acr, ier, sr, tx[8];
    unsigned sent, frames, received, flags, polls, bytes, prep_edges;
    bool pmu, active, pending, done, armed, loaded, bad, prep_idle;
    unsigned long long ticks, frame_ended, prep_time, fake_time;
};

static u8 bus_read(void *cookie, u32 reg)
{
    struct bus *b = cookie;
    switch (reg) {
    case 0x0000:
        if ((b->flags & FAKE) && b->fake_time && b->ticks - b->fake_time >= 100) {
            b->pending = false;
            if (b->ticks - b->fake_time >= 400) {
                b->flags &= ~FAKE;
                b->pending = true;
            }
        }
        return (b->b & 0xf7) | (b->pending ? 0 : 0x08);
    case 0x0400: return b->ddr;
    case 0x1600: return b->acr;
    case 0x1c00: return b->ier | 0x80;
    case 0x1a00:
        if (b->armed && ++b->polls == 3) {
            b->done = true;
            b->armed = false;
            if (b->acr & 0x10) {
                if (b->flags & COLLISION) {
                    b->pending = true;
                    if (!(b->flags & REPEAT))
                        b->flags &= ~COLLISION;
                }
            } else {
                b->sr = 0xa0 + b->bytes++;
                if (b->bytes == 3 && !(b->flags & OVERSIZE))
                    b->pending = false;
            }
        }
        return b->done ? 0x04 : 0;
    case 0x1400:
        b->done = false;
        return b->sr;
    default:
        b->bad = true;
        return 0;
    }
}

static void arm(struct bus *b)
{
    b->polls = 0;
    b->armed = !(b->flags & STALL) && !((b->flags & NO_DATA) && !(b->acr & 0x10));
}

static void bus_write(void *cookie, u32 reg, u8 value)
{
    struct bus *b = cookie;
    switch (reg) {
    case 0x0400: b->ddr = value; return;
    case 0x1600: b->acr = value; return;
    case 0x1c00:
        b->bad |= value != (b->pmu ? 0x10 : 0x04);
        b->ier &= ~value;
        return;
    case 0x1400:
        b->done = false;
        b->bad |= !(b->acr & 0x10) || b->loaded || b->armed;
        if (b->sent < sizeof(b->tx))
            b->tx[b->sent] = value;
        ++b->sent;
        b->loaded = true;
        return;
    case 0x0000:
        break;
    default:
        b->bad = true;
        return;
    }

    u8 changed = b->b ^ value;
    b->b = value;
    if (!b->active) {
        if (!b->prep_idle) {
            if ((value & 0x30) == 0x30) {
                b->prep_idle = true;
                b->prep_time = b->ticks;
            }
        } else if (!b->pmu && (changed & 0x30)) {
            static const u8 edges[] = {0x10, 0x00, 0x20, 0x30};
            b->bad |= b->prep_edges >= 4 ||
                      (value & 0x30) != edges[b->prep_edges] ||
                      b->ticks - b->prep_time < 150;
            ++b->prep_edges;
            b->prep_time = b->ticks;
        } else if (!b->prep_edges) {
            b->prep_time = b->ticks;
        }
        return;
    }
    b->bad |= !(value & 0x40);
    if (b->pmu) {
        b->bad |= !(value & 0x20);
        if ((changed & 0x10) && !(value & 0x10)) {
            b->bad |= !b->loaded || b->acr != 0x9c;
            b->loaded = false;
            b->pending = !(b->flags & STALL);
        }
        if ((changed & value & 0x10) && !(b->flags & FREE_STALL))
            b->pending = false;
        return;
    }
    if (!(value & 0x20) && (changed & 0x30)) {
        if (b->acr & 0x10) {
            b->bad |= !b->loaded || b->done;
            b->loaded = false;
            if (changed & 0x20) {
                ++b->frames;
                b->bad |= !(value & 0x10);
                if (b->received)
                    b->bad |= b->ticks - b->frame_ended < 1500;
            }
        } else {
            b->bad |= !b->pending;
            if (changed & 0x20)
                b->bytes = 0;
        }
        arm(b);
    } else if (changed & value & 0x20) {
        if (b->bytes == 3 && !b->pending) {
            ++b->received;
            b->frame_ended = b->ticks;
            b->bytes = 0;
        } else if (!b->pending && b->frames && (b->flags & REPLY)) {
            b->flags &= ~REPLY;
            b->pending = true;
            if (b->flags & FAKE)
                b->fake_time = b->ticks;
        }
    }
}

static unsigned long long bus_ticks(void *cookie)
{
    struct bus *b = cookie;
    b->ticks += 10;
    return b->ticks;
}

static int exercise(enum via_kind kind, unsigned flags, int expected,
                    unsigned sent, unsigned received, bool reply, bool wrap)
{
    struct bus b = {
        .b = 0x60, .ddr = 0x49, .acr = 0x90, .ier = 0x17,
        .pmu = kind == VIA_PMU, .done = true,
        .ticks = wrap ? ~0ULL - 750 : 0
    };
    struct via_controller v = {kind, {&b, bus_read, bus_write, bus_ticks, 1000000}};
    if (via_prepare(&v) || b.bad || b.ddr != (b.pmu ? 0x51 : 0x71) ||
        b.b != 0x78 || (!b.pmu && (b.prep_edges != 4 || b.acr != 0x8c || b.done || b.ier != 0x13)))
        return 1;

    b.active = true;
    b.flags = flags;
    b.pending = (flags & PENDING) != 0;
    b.done = true; /* Stale completion, with no new byte ready yet. */
    unsigned long long start = b.ticks;
    u8 in[] = {0, 0, 0x55};
    u32 got = 99;
    const u8 cmd[] = {0x01, 0x11};
    int rc = reply ? via_command(&v, cmd, sizeof(cmd), in, 2, &got) : via_restart(&v);
    if (rc != expected)
        return 2;
    if (b.bad || (b.b & 0x70) != 0x70 || (!b.pmu && (b.acr & 0x10)) ||
        b.ier != (b.pmu ? 0x07 : 0x13) || b.loaded)
        return 3;
    if (flags & REPEAT)
        return b.sent < 2 || b.sent != b.frames ||
               b.ticks - start < 1000000 || b.ticks - start > 1000020 ? 7 : 0;
    if (b.sent != sent || b.received != received)
        return 4;
    if (sent && (b.tx[0] != (b.pmu ? 0xd0 : 0x01) ||
                 (sent == 2 && b.tx[1] != 0x11) ||
                 (sent == 3 && (b.tx[1] != 0x01 || b.tx[2] != 0x11))))
        return 5;
    if (reply && (got != 2 || in[0] != 0xa0 || in[1] != 0xa1 || in[2] != 0x55))
        return 6;
    unsigned long long elapsed = b.ticks - start;
    return (flags & (STALL | FREE_STALL)) && (elapsed < 100000 || elapsed > 102000) ? 7 : 0;
}

static int probe(void)
{
    static const struct {
        enum via_kind kind;
        unsigned flags;
        int expected;
        unsigned sent, received;
        bool reply, wrap;
    } cases[] = {
        {VIA_CUDA, 0, 0, 2, 0, false, true},
        {VIA_PMU, 0, 0, 1, 0, false, false},
        {VIA_CUDA, PENDING, 0, 2, 1, false, false},
        {VIA_CUDA, COLLISION, 0, 3, 1, false, false},
        {VIA_CUDA, PENDING | REPLY, 0, 2, 2, true, false},
        {VIA_CUDA, STALL, -5, 1, 0, false, false},
        {VIA_PMU, STALL, -3, 1, 0, false, false},
        {VIA_PMU, PENDING | STALL, -2, 0, 0, false, false},
        {VIA_PMU, FREE_STALL, -4, 1, 0, false, false},
        {VIA_CUDA, PENDING | OVERSIZE, -7, 0, 0, false, false},
        {VIA_CUDA, COLLISION | REPEAT, -5, 0, 0, false, false},
        {VIA_CUDA, REPLY, 0, 2, 0, false, false},
        {VIA_CUDA, REPLY | FAKE, 0, 2, 1, true, false},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int rc = exercise(cases[i].kind, cases[i].flags, cases[i].expected,
                          cases[i].sent, cases[i].received, cases[i].reply, cases[i].wrap);
        if (rc)
            return 10 * (i + 1) + rc;
    }
    /* Each half of the receive predicate must independently suppress reception. */
    for (unsigned i = 0; i < 2; ++i) {
        struct bus b = {.b = 0x60, .ddr = 0x49, .acr = 0x90, .ier = 0x17};
        struct via_controller v = {VIA_CUDA, {&b, bus_read, bus_write, bus_ticks, 1000000}};
        u8 in[2] = {0};
        u32 got = 99;
        const u8 cmd[] = {1, 0x11};
        if (via_prepare(&v)) return 141;
        b.active = true;
        b.flags = REPLY | NO_DATA;
        if (via_command(&v, cmd, 2, i ? in : NULL, i ? 0 : 2, &got) ||
            got || b.received || !b.pending || b.bad) return 142 + i;
    }
    return 0;
}

void _start(void)
{
    int code = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) {}
}
