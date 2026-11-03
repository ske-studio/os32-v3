/* Layer 2 component fixtures. Select exactly one GF2_HOST_B<N> before
 * including this header in the host test TU, never in the product TU.
 * Link only the component under test (B7 includes its source instead).
 * The selected fixture supplies its dependencies without linking any B.
 * Define CHECK before B4/B5/B7. The runtime header supplies libc-free assert.
 * B7 also supplies event(kind, off) and EVENT_COUNT from its oracle table.
 * Expectation tables and the B4 independent oracle stay in the tests. */
#ifndef TOOLS_TESTS_GF_COMMAND_HOST_H
#define TOOLS_TESTS_GF_COMMAND_HOST_H
#include "gf_command_runtime_host.h"
#include "../../hwdev/gf/command.h"

#if (defined(GF2_HOST_B0) + defined(GF2_HOST_B1) + defined(GF2_HOST_B2) + \
     defined(GF2_HOST_B3) + defined(GF2_HOST_B4) + defined(GF2_HOST_B5) + \
     defined(GF2_HOST_B6) + defined(GF2_HOST_B7) + defined(GF2_HOST_B8) + \
     defined(GF2_HOST_B9)) != 1
#error "Select one GF2_HOST_B0 through GF2_HOST_B9 fixture"
#endif

#if defined(GF2_HOST_B0)
enum { GF2_HOST_REGS = 64, GF2_HOST_WRITES = 256,
       GF2_HOST_PRAMIN_BYTES = 0x400000 };
struct gf2_host_reg { u32 off, value; };
struct gf2_host_write { u32 bar, off, value; };
struct gf2_host {
    struct gf_foundation f;
    struct gf2_chan ch;
    unsigned nwrite, nreg;
    struct gf2_host_write write[GF2_HOST_WRITES];
    struct gf2_host_reg reg[GF2_HOST_REGS];
    u32 pramin[GF2_HOST_PRAMIN_BYTES / sizeof(u32)], now;
    int busy_reads, fault_rc;
    bool clock_fail, wait_fail;
};
static void gf2_host_set(struct gf2_host *h, u32 reg, u32 value)
{
    for (unsigned i = 0; i < h->nreg; ++i) {
        if (h->reg[i].off == reg) { h->reg[i].value = value; return; }
    }
    assert(h->nreg < GF2_HOST_REGS);
    h->reg[h->nreg++] = (struct gf2_host_reg){reg, value};
}
static u32 gf2_host_get(struct gf2_host *h, u32 reg)
{
    for (unsigned i = 0; i < h->nreg; ++i)
        if (h->reg[i].off == reg) return h->reg[i].value;
    return 0;
}
static void gf2_host_log(struct gf2_host *h, u32 bar, u32 off, u32 value)
{
    assert(h->nwrite < GF2_HOST_WRITES);
    h->write[h->nwrite++] = (struct gf2_host_write){bar, off, value};
}
static int gf2_host_read(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    struct gf2_host *h = owner;
    assert(bar == GF_MODE_REGISTER_BAR && width == sizeof(u32));
    *out = gf2_host_get(h, off);
    if (off == 0x3250 && !(*out & 1U) && h->busy_reads) {
        *out |= 0x1000U;
        if (h->busy_reads > 0) --h->busy_reads;
    }
    return 0;
}
static int gf2_host_write(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    struct gf2_host *h = owner;
    assert(bar == GF_MODE_REGISTER_BAR && width == sizeof(u32));
    gf2_host_log(h, bar, off, value); gf2_host_set(h, off, value);
    return 0;
}
static int gf2_host_clock(void *owner, u32 *now)
{
    struct gf2_host *h = owner;
    *now = h->now; return h->clock_fail ? -1 : 0;
}
static int gf2_host_wait(void *owner, u32 us)
{
    struct gf2_host *h = owner;
    if (h->wait_fail) return -1;
    h->now += us; return 0;
}
static void gf2_host_init(struct gf2_host *h, u32 v, u32 r)
{
    assert(r <= GF2_HOST_PRAMIN_BYTES);
    h->f = (struct gf_foundation){0};
    h->ch = (struct gf2_chan){.f = &h->f, .state = GF2_INIT};
    h->nwrite = h->nreg = h->now = 0;
    h->busy_reads = h->fault_rc = 0; h->clock_fail = h->wait_fail = false;
    h->f.io.opaque = h; h->f.io.read = gf2_host_read; h->f.io.write = gf2_host_write;
    h->f.io.timing.clock_us = gf2_host_clock; h->f.io.timing.wait_us = gf2_host_wait;
    h->f.bar2 = (volatile u8 *)h->pramin;
    h->f.imem = (struct gf_imem){.vram_bytes = v, .reserved = r, .aperture_bytes = r,
        .ramht = {0x10000, 0x8000, 0}, .ramro = {0x18000, 0x200, 0},
        .ramfc = {0x20000, 32 * GF2_RAMFC_BYTES, 0}};
    for (u32 i = 0; i < r / sizeof(u32); ++i) h->pramin[i] = 0xa5a5a5a5U;
}
static struct gf2_host *gf2_host_inst(struct gf_foundation *f,
                                    const struct gf_inst *obj, u32 off)
{
    struct gf2_host *h = f->io.opaque;
    assert(f == &h->f && !(off & 3U) && !(obj->off & 3U));
    assert(obj->size >= 4 && off <= obj->size - 4);
    assert(obj->off <= f->imem.aperture_bytes &&
           obj->size <= f->imem.aperture_bytes - obj->off);
    return h;
}
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *obj,
                   u32 off, u32 *value)
{
    struct gf2_host *h = gf2_host_inst(f, obj, off);
    *value = h->pramin[(obj->off + off) / sizeof(u32)]; return GF1_OK;
}
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj,
                    u32 off, u32 value)
{
    struct gf2_host *h = gf2_host_inst(f, obj, off);
    h->pramin[(obj->off + off) / sizeof(u32)] = value;
    gf2_host_log(h, 2, obj->off + off, value); return GF1_OK;
}
int gf2_fault_check(struct gf2_chan *ch)
{ struct gf2_host *h = ch->f->io.opaque; return h->fault_rc; }
#endif

#if defined(GF2_HOST_B1)
#define BASE 0x100000U
#define USER (GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE)
/* Linux i386 userspace ABI, also used by qemu-i386. Protect the ring's
 * first page, retry each faulting instruction with TF, then reprotect on
 * SIGTRAP. Only a read at the watched word increments the counter; stores
 * and accesses to other ring words execute normally. No product hooks or
 * instruction rewriting: omitting the volatile load must omit the count.
 * The dedicated page-aligned ring below keeps observer state off that page.
 * A3: -m32 -fno-pie -no-pie -nostdlib -static, push.c in a separate TU. */
#if !defined(__i386__)
#error "B1's CPU read observer requires Linux i386 (native or qemu-i386)"
#endif
enum {
    GF2_HOST_SYS_EXIT = 1, GF2_HOST_SYS_MPROTECT = 125,
    GF2_HOST_SYS_RT_SIGACTION = 174,
    GF2_HOST_SIGTRAP = 5, GF2_HOST_SIGSEGV = 11,
    GF2_HOST_SA_SIGINFO = 4, GF2_HOST_SA_RESTORER = 0x04000000,
    GF2_HOST_TF = 0x100, GF2_HOST_PAGE_WRITE = 2,
    GF2_HOST_PROT_NONE = 0, GF2_HOST_PROT_RW = 3
};
/* Prefix of the i386 rt signal ucontext through sigcontext.cr2. Segment
 * selectors occupy four bytes each, including their reserved high half. */
struct gf2_host_sigcontext {
    u32 gs, fs, es, ds, edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 trapno, err, eip, cs, eflags, signal_esp, ss, fpstate, oldmask, cr2;
};
struct gf2_host_ucontext {
    u32 flags, link, stack_sp, stack_flags, stack_bytes;
    struct gf2_host_sigcontext cpu;
};
struct gf2_host_sigaction {
    void (*handler)(int, void *, void *);
    u32 flags;
    void (*restorer)(void);
    u32 mask[2];
};
static struct gf2_host_sigaction gf2_host_saved_segv, gf2_host_saved_trap;
static volatile u32 gf2_host_watch_word, gf2_host_watch_page, gf2_host_watch_reads;
static volatile bool gf2_host_watching, gf2_host_stepping;

static int gf2_host_syscall(u32 nr, u32 a, u32 b, u32 c, u32 d)
{
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc)
                     : "0"(nr), "b"(a), "c"(b), "d"(c), "S"(d)
                     : "memory", "cc");
    return rc;
}
static void __attribute__((noreturn)) gf2_host_observer_fail(void)
{
    (void)gf2_host_syscall(GF2_HOST_SYS_EXIT, 120, 0, 0, 0);
    __builtin_unreachable();
}
static void __attribute__((naked)) gf2_host_sigreturn(void)
{
    __asm__("movl $173, %eax\n\tint $0x80");
}
static int gf2_host_protect(u32 prot)
{
    return gf2_host_syscall(GF2_HOST_SYS_MPROTECT, gf2_host_watch_page,
                           MEM_PAGE_SIZE, prot, 0);
}
static int gf2_host_sigaction(int sig, const struct gf2_host_sigaction *action,
                             struct gf2_host_sigaction *old)
{
    return gf2_host_syscall(GF2_HOST_SYS_RT_SIGACTION, (u32)sig,
                           (u32)action, (u32)old, sizeof(action->mask));
}
static void gf2_host_watch_signal(int sig, void *info, void *context)
{
    struct gf2_host_ucontext *uc = context;
    (void)info;
    if (!gf2_host_watching) gf2_host_observer_fail();
    if (sig == GF2_HOST_SIGSEGV) {
        if (gf2_host_stepping || (uc->cpu.eflags & GF2_HOST_TF) ||
            (uc->cpu.cr2 & ~(MEM_PAGE_SIZE - 1U)) != gf2_host_watch_page)
            gf2_host_observer_fail();
        if (uc->cpu.cr2 == gf2_host_watch_word && !(uc->cpu.err & GF2_HOST_PAGE_WRITE))
            ++gf2_host_watch_reads;
        if (gf2_host_protect(GF2_HOST_PROT_RW)) gf2_host_observer_fail();
        gf2_host_stepping = true;
        uc->cpu.eflags |= GF2_HOST_TF;
    } else {
        if (sig != GF2_HOST_SIGTRAP || !gf2_host_stepping) gf2_host_observer_fail();
        uc->cpu.eflags &= ~GF2_HOST_TF;
        if (gf2_host_protect(GF2_HOST_PROT_NONE)) gf2_host_observer_fail();
        gf2_host_stepping = false;
    }
}
int gf2_host_va_read_begin(volatile u32 *word)
{
    const struct gf2_host_sigaction action = {
        .handler = gf2_host_watch_signal,
        .flags = GF2_HOST_SA_SIGINFO | GF2_HOST_SA_RESTORER,
        .restorer = gf2_host_sigreturn, .mask = {0, 0}
    };
    if (!word || ((u32)word & 3U) || gf2_host_watching) return -1;
    gf2_host_watch_word = (u32)word;
    gf2_host_watch_page = (u32)word & ~(MEM_PAGE_SIZE - 1U);
    gf2_host_watch_reads = 0; gf2_host_stepping = false;
    if (gf2_host_sigaction(GF2_HOST_SIGSEGV, &action, &gf2_host_saved_segv)) return -1;
    if (gf2_host_sigaction(GF2_HOST_SIGTRAP, &action, &gf2_host_saved_trap)) {
        if (gf2_host_sigaction(GF2_HOST_SIGSEGV, &gf2_host_saved_segv, 0))
            gf2_host_observer_fail();
        return -1;
    }
    gf2_host_watching = true;
    if (!gf2_host_protect(GF2_HOST_PROT_NONE)) return 0;
    gf2_host_watching = false;
    if (gf2_host_sigaction(GF2_HOST_SIGTRAP, &gf2_host_saved_trap, 0) |
        gf2_host_sigaction(GF2_HOST_SIGSEGV, &gf2_host_saved_segv, 0))
        gf2_host_observer_fail();
    return -1;
}
u32 gf2_host_va_read_count(void) { return gf2_host_watch_reads; }
void gf2_host_va_read_end(void)
{
    if (!gf2_host_watching) return;
    if (gf2_host_stepping || gf2_host_protect(GF2_HOST_PROT_RW)) gf2_host_observer_fail();
    gf2_host_watching = false;
    if (gf2_host_sigaction(GF2_HOST_SIGTRAP, &gf2_host_saved_trap, 0) |
        gf2_host_sigaction(GF2_HOST_SIGSEGV, &gf2_host_saved_segv, 0))
        gf2_host_observer_fail();
}
static u32 ring[GF2_PUSH_WORDS] __attribute__((aligned(MEM_PAGE_SIZE)));
static struct gf2_chan ch;
static struct gf_foundation f;
static struct fake {
    u32 get[8], put[8], readbacks_at_put[8], now, waits;
    unsigned gets, reads, writes, fault_after_reads;
    bool fault, read_fail, write_fail, clock_fail, wait_fail, watch_readback;
} m;

static int read_reg(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    struct fake *p = owner;
    if (bar != GF_MODE_REGISTER_BAR || off != USER + GF2_USER_GET ||
        width != sizeof(u32) || p->read_fail) return -1;
    *out = p->get[p->reads < p->gets ? p->reads : p->gets - 1U];
    ++p->reads;
    return 0;
}
static int write_reg(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    struct fake *p = owner;
    if (bar != GF_MODE_REGISTER_BAR || off != USER + GF2_USER_PUT ||
        width != sizeof(u32) || p->writes == 8 || p->write_fail) return -1;
    if (p->watch_readback)
        p->readbacks_at_put[p->writes] = gf2_host_va_read_count();
    p->put[p->writes++] = value;
    return 0;
}
static int clock_us(void *owner, u32 *now)
{
    struct fake *p = owner;
    *now = p->now;
    return p->clock_fail ? -1 : 0;
}
static int wait_us(void *owner, u32 us)
{
    struct fake *p = owner;
    if (us < GF2_WAIT_MIN_US || us > GF2_WAIT_MAX_US || p->wait_fail) return -1;
    p->now += us;
    ++p->waits;
    return 0;
}
int gf2_fault_check(struct gf2_chan *c)
{
    if (m.fault || (m.fault_after_reads && m.reads >= m.fault_after_reads))
        c->state = GF2_FAULT;
    return c->state == GF2_FAULT ? GF2_EFAULT : GF2_OK;
}
static void fresh(enum gf2_state state)
{
    m = (struct fake){ .gets = 1 };
    f = (struct gf_foundation){ .io = { .opaque = &m, .read = read_reg,
        .write = write_reg, .timing = { .clock_us = clock_us, .wait_us = wait_us } } };
    ch = (struct gf2_chan){ .f = &f, .state = state, .B = BASE, .va = ring,
        .cur = GF2_PUSH_SKIPS, .max = GF2_PUSH_MAX,
        .free = GF2_PUSH_MAX - GF2_PUSH_SKIPS };
    for (u32 i = 0; i < GF2_PUSH_WORDS; ++i) ring[i] = 0;
}
#endif

#if defined(GF2_HOST_B2)
static u32 words[4], writes, adds, dels, frees, add_engine, add_off;
static int alloc_rc, write_fail, add_rc, del_rc;
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out)
{
    (void)f;
    if (alloc_rc) return alloc_rc;
    if (bytes != 16 || align != 16 || zero) return GF1_EINVAL;
    out->off = 0x40000; out->size = bytes; out->next = 0;
    return GF1_OK;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *obj)
{ (void)f; (void)obj; ++frees; }
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj, u32 off, u32 v)
{
    (void)f; (void)obj;
    if (off != writes * 4 || writes >= 4) return GF1_EINVAL;
    if (write_fail && writes == (u32)(write_fail - 1)) return GF1_EIO;
    words[writes++] = v;
    return GF1_OK;
}
int gf2_ramht_add(struct gf2_chan *c, u32 handle, u8 engine, u32 off, u32 *cookie)
{
    (void)c; (void)handle; ++adds; add_engine = engine; add_off = off;
    if (add_rc) return add_rc;
    *cookie = 7; return GF2_OK;
}
int gf2_ramht_del(struct gf2_chan *c, u32 cookie)
{ (void)c; if (cookie != 7) return GF2_EINVAL; ++dels; return del_rc; }
#endif

#if defined(GF2_HOST_B3)
/* Fixed RAMHT storage; B3 owns the real probing/bitmap operations. */
static u32 table[GF2_RAMHT_ENTRIES * 2U];
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *obj,
                   u32 off, u32 *value)
{
    (void)f; (void)obj;
    *value = table[off / 4U];
    return GF1_OK;
}
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *obj,
                    u32 off, u32 value)
{
    (void)f; (void)obj;
    table[off / 4U] = value;
    return GF1_OK;
}
#endif

#if defined(GF2_HOST_B4)
enum { IMAGE_WORDS = 80 * 1024 / 4 };
static u32 actual[IMAGE_WORDS], expected[IMAGE_WORDS];
static u32 oracle_code[256], uploaded[256], upload_words;
static u32 writes, fail_write, image_bytes;

/* A2 dependency-stub contract: offsets are relative to ctx, first failure
 * is injectable, and no MMIO/IRQ/timing/platform service is required. */
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *ctx,
                    u32 off, u32 value)
{
    (void)f;
    CHECK(!(off & 3) && off < ctx->size && off < sizeof(actual));
    if (++writes == fail_write) return GF1_EIO;
    actual[off / 4] = value;
    return GF1_OK;
}
#endif

#if defined(GF2_HOST_B5)
struct event { u32 write, off, value; };
static struct event events[256];
static u32 memory[16], count, cursor, fail_io, units, overlap, words = 3;
static u32 inst_calls, fail_inst;
static int alloc_rc, fill_rc, bind_rc, ramht_rc;
static bool live, published, bound;
static unsigned frees, adds;
static struct gf_foundation foundation;
static struct gf2_chan channel;

static int rd(void *p, u32 bar, u32 off, u32 width, u32 *value)
{
    (void)p;
    CHECK(bar == 0 && width == 4 && count < 256);
    *value = 0;
    if (off == 0x1540) *value = units;
    else if (off == 0x100200) *value = 0x12345678;
    else if (off == 0x100204) *value = 0x87654321;
    else {
        CHECK(off >= 0x100600 && off < 0x1006c0);
        u32 i = (off - 0x100600) / 16, part = off & 15;
        if (part == 0) *value = overlap && i == 11 ? channel.B | 1 : i * 0x1000 + 1;
        else if (part == 4) *value = overlap && i == 11 ? channel.B : i * 0x1000 + 0xfff;
        else { CHECK(part == 8); *value = (i + 1) * 0x100; }
    }
    events[count++] = (struct event){0, off, *value};
    return count == fail_io ? -1 : 0;
}
static int wr(void *p, u32 bar, u32 off, u32 width, u32 value)
{
    (void)p;
    CHECK(bar == 0 && width == 4 && count < 256);
    if (off == 0x400720) CHECK(bound);
    events[count++] = (struct event){1, off, value};
    return count == fail_io ? -1 : 0;
}
int gf2_grctx_program(u32 code[GF2_GRCTX_PROGRAM_WORDS], u32 *n, u32 *bytes)
{ code[0] = 0x123; code[1] = 0x456; code[2] = 0x789; *n = words; *bytes = 64; return 0; }
int gf2_grctx_fill(struct gf_foundation *f, const struct gf_inst *ctx)
{ CHECK(f == &foundation && live && ctx == &channel.grctx); memory[1] = 0xfeed; return fill_rc; }
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero, struct gf_inst *out)
{
    CHECK(f == &foundation && !live && align == 16 && bytes <= sizeof(memory));
    CHECK((bytes == 64 && zero) || (bytes == 20 && !zero));
    if (alloc_rc) return alloc_rc;
    *out = (struct gf_inst){0x40000, bytes, 0};
    f->imem.used = out; live = true;
    for (unsigned i = 0; i < 16; i++) memory[i] = zero ? 0 : 0xdead;
    return 0;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *inst)
{ CHECK(live && f->imem.used == inst); f->imem.used = 0; live = false; frees++; }
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *inst, u32 off, u32 value)
{
    CHECK(live && f->imem.used == inst && !(off & 3) && off < inst->size);
    if (++inst_calls == fail_inst) return GF1_EIO;
    memory[off / 4] = value; published = false; return 0;
}
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *inst, u32 off, u32 *value)
{
    CHECK(live && f->imem.used == inst && off == 0);
    if (++inst_calls == fail_inst) return GF1_EIO;
    *value = memory[0]; published = true; return 0;
}
int gf2_fifo_bind_gr(struct gf2_chan *ch, u32 off)
{
    CHECK(ch == &channel && published && off == ch->grctx.off);
    CHECK(memory[0] == off >> 4 && memory[1] == 0xfeed);
    bound = !bind_rc; return bind_rc;
}
int gf2_ramht_add(struct gf2_chan *ch, u32 handle, u8 engine, u32 off, u32 *cookie)
{
    CHECK(ch == &channel && handle == GF2_HANDLE_LAYER3_FIRST);
    CHECK(engine == 1 && off == 0x40000 && published);
    for (unsigned i = 1; i < 5; i++) CHECK(memory[i] == 0);
    adds++; if (!ramht_rc) *cookie = 9; return ramht_rc;
}
static void reset(void)
{
    foundation = (struct gf_foundation){0}; channel = (struct gf2_chan){0};
    foundation.io.read = rd; foundation.io.write = wr; channel.f = &foundation;
    foundation.imem.vram_bytes = 0x4000000; channel.bar1.length = 0x8000000;
    channel.B = 0x3000000; units = 0x108; overlap = 0; words = 3;
    count = cursor = fail_io = frees = adds = 0;
    inst_calls = fail_inst = 0;
    alloc_rc = fill_rc = bind_rc = ramht_rc = 0; live = published = bound = false;
}
#endif

#if defined(GF2_HOST_B6)
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
#endif

#if defined(GF2_HOST_B7)
#define IO_H /* fault.c borrows these IRQ operations instead of include/io.h. */
static unsigned host_flags = 0x202U;
static unsigned irq_save(void)
{ unsigned v = host_flags; host_flags &= ~0x200U; return v; }
static void irq_restore(unsigned v)
{ CHECK(!(host_flags & 0x200U)); host_flags = v; }
static u32 event(u32 kind, u32 off);
static struct gf_foundation foundation;
static struct gf2_chan channel;
static u32 cursor, fail_at, snapshots, log_bytes;
static int enable_rc, serial_gated;
static char output[1024];
static int mmio_read(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    CHECK(owner == &foundation && bar == 0 && width == 4);
    u32 value = event(0, off);
    if (cursor == fail_at) return -1;
    *out = value; return 0;
}
static int mmio_write(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    CHECK(owner == &foundation && bar == 0 && width == 4);
    CHECK(value == event(1, off));
    return cursor == fail_at ? -1 : 0;
}
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *out)
{ CHECK(f == &foundation); snapshots++; *out = f->stats; }
int gf_irq_enable(struct gf_foundation *f, bool fifo, bool graph, unsigned heads)
{
    CHECK(f == &foundation && !fifo && !graph && heads == 3);
    CHECK(!(host_flags & 0x200U)); (void)event(2, 0);
    f->wanted_fifo = f->wanted_graph = 0; f->wanted_heads = heads;
    return enable_rc;
}
int serial_gate_active(void) { return serial_gated; }
void serial_puts_polled(const char *s)
{
    CHECK(cursor == EVENT_COUNT && channel.state == GF2_FAULT);
    while (*s) { CHECK(log_bytes + 1 < sizeof(output)); output[log_bytes++] = *s++; }
    output[log_bytes] = 0;
}
static void reset(void)
{
    foundation = (struct gf_foundation){0}; channel = (struct gf2_chan){0};
    foundation.io.opaque = &foundation;
    foundation.io.read = mmio_read; foundation.io.write = mmio_write;
    foundation.wanted_fifo = foundation.wanted_graph = ~0U;
    channel.f = &foundation; channel.state = GF2_RUN; channel.heads = 3; channel.epoch = 9;
    cursor = fail_at = snapshots = log_bytes = 0; output[0] = 0;
    enable_rc = serial_gated = 0; host_flags = 0x202U;
}
#endif

#if defined(GF2_HOST_B8)
static struct recover_bar_extent mapped_extent;
static u32 mapped_bytes, storage[GF2_PUSH_WORDS];
static int map_fail, unmap_fail, alloc_fail, free_fail, leaked;
static int map_stub(void *owner, const struct recover_bar_extent *bar,
                    u32 bytes, volatile u8 **out)
{
    (void)owner; if (map_fail) return map_fail;
    mapped_extent = *bar; mapped_bytes = bytes;
    *out = (volatile u8 *)storage; return 0;
}
static int unmap_stub(void *owner, volatile u8 *ptr, u32 bytes)
{
    (void)owner; assert(ptr == (volatile u8 *)storage);
    assert(bytes == mapped_bytes); return unmap_fail;
}
static int alloc_stub(u32 bytes, u32 align, u32 mask, struct gf2_dma_buf *b)
{
    assert(bytes == GF2_STAGING_BYTES && align == GF2_STAGING_ALIGN);
    assert(mask == GF2_STAGING_MASK);
    if (alloc_fail) return alloc_fail;
    *b = (struct gf2_dma_buf){0x1000U, bytes, storage}; return 0;
}
static int free_stub(struct gf2_dma_buf *b) { (void)b; return free_fail; }
static void leak_stub(struct gf2_dma_buf *b) { (void)b; leaked++; }
static const struct gf_instmem_map_ops map_ops = {map_stub, unmap_stub};
static const struct gf2_dma_ops dma_ops = {alloc_stub, free_stub, leak_stub};
#endif

#if defined(GF2_HOST_B9)
#include "../../mm/dma_pool.h"
static int alloc_rc, free_rc, leak_calls;
static u32 requested_bytes, requested_align, requested_mask;
static struct dma_buf seen;
static int storage;

int dma_alloc(u32 bytes, u32 align, u32 mask, struct dma_buf *out)
{
    requested_bytes = bytes; requested_align = align; requested_mask = mask;
    if (alloc_rc) return alloc_rc;
    *out = (struct dma_buf){ .pa = 0x12345000U, .va = &storage, .size = bytes };
    return 0;
}
int dma_free(const struct dma_buf *buf) { seen = *buf; return free_rc; }
int dma_mark_leaked(const struct dma_buf *buf) { seen = *buf; ++leak_calls; return 0; }
#endif

#endif
