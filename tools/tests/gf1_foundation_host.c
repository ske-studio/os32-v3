/* C0 sequencing oracle. B components are contract fakes; no native I/O. */
#define IO_H
#include "types.h"
#include "hwdev/gf/foundation.h"
static void fail(int line)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(line % 250 + 1));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static unsigned host_if, saves, restores;
static unsigned irq_save(void)
{ unsigned v = host_if; host_if = 0; saves++; return v; }
static void irq_restore(unsigned v)
{ CHECK(!host_if); host_if = v; restores++; }
#ifndef GF1_FOUNDATION_SOURCE
#define GF1_FOUNDATION_SOURCE "../../hwdev/gf/foundation.c"
#endif
#include GF1_FOUNDATION_SOURCE

enum step { MASTER=1, MASK, MC, IMEM, BIND, ATTACH, ENABLE, UNBIND,
            ENGINE, PMC_READ, DETACH, DISABLE, FREE, UNMAP };
static struct gf_foundation f;
static struct gf_inst variable[2];
static unsigned trace[128], count, fail_step, fail_second, io_count, io_fault;
static unsigned mask_pos, masks, command;
static bool bound, engine_done, master_partial, imem_partial, mc_partial;
static u32 pmc;
static unsigned pmc_reads;
static int step(unsigned s, int rc)
{
    CHECK(count < sizeof(trace) / sizeof(trace[0]));
    trace[count++] = s;
    if (s == fail_step) { fail_step = fail_second; fail_second = 0; return rc; }
    return 0;
}
/* Independent expected mask/ACK stream, including the PMC read flush.
 * All MMIO must observe Command already matching expected_command. */
static const struct { u32 write, off, value; } mask_ops[] = {
    {1,0x140,0}, {0,0x140,0},
    {1,0x2140,0}, {1,0x2100,0xffffffff},
    {1,0x40013c,0}, {1,0x400100,0xffffffff},
    {1,0x600140,0}, {1,0x600100,1},
    {1,0x602140,0}, {1,0x602100,1},
    {1,0x8140,0}, {1,0x8100,0xffffffff},
    {1,0x9140,0}, {1,0x9100,0xffffffff},
    {1,0x1140,0}, {1,0x1100,0xffffffff},
    {1,0xb140,0}, {1,0xb100,0xffffffff}
};
static int io_access(bool write, void *arg, u32 bar, u32 off, u32 width, u32 *v)
{
    CHECK(arg == &f && bar == 0 && width == 4);
    if (f.bm || command == RECOVER_COMMAND_STOP) CHECK(f.expected_command == command);
    if (!write && off == 0x200) {
        CHECK(engine_done && !f.running && !bound && host_if);
        pmc_reads++;
        *v = pmc; /* Even a failed adapter read must not publish its output. */
        return step(PMC_READ, -1);
    }
    if (write && off == 0x140) {
        if (count && trace[count - 1] == UNBIND)
            CHECK(!host_if && !bound && !f.running);
        mask_pos = 0;
        CHECK(!step(MASK, 0));
    }
    CHECK(mask_pos < sizeof(mask_ops) / sizeof(mask_ops[0]));
    CHECK(mask_ops[mask_pos].write == write && mask_ops[mask_pos].off == off);
    if (write) CHECK(*v == mask_ops[mask_pos].value);
    else *v = 0;
    if (++io_count == io_fault) return -1;
    if (++mask_pos == sizeof(mask_ops) / sizeof(mask_ops[0])) masks++;
    return 0;
}
static int rd(void *arg, u32 bar, u32 off, u32 width, u32 *v)
{ return io_access(false, arg, bar, off, width, v); }
static int wr(void *arg, u32 bar, u32 off, u32 width, u32 v)
{ return io_access(true, arg, bar, off, width, &v); }
int gf_master_enable(struct gf_foundation *p)
{
    CHECK(p == &f && host_if);
    int rc = step(MASTER, GF1_EPCI);
    if (rc && !master_partial) return rc;
    command = RECOVER_COMMAND_RUN;
    if (rc) {
        /* Failed readback: B0 has not published bm/expected_command yet.
         * Keep the fake MMIO permissive for rollback, as the permanent IRQ
         * adapter is not the foreground PCI-checking bus adapter. */
        return rc;
    }
    f.expected_command = command; f.bm = true; engine_done = false;
    return 0;
}
int gf_mc_init(struct gf_foundation *p)
{
    CHECK(p == &f && masks && mask_pos == 18 && f.bm);
    int rc = step(MC, GF1_EIO);
    if (!rc || mc_partial) f.expected_pmc = pmc = 0xffffffff;
    return rc;
}
int gf_instmem_init(struct gf_foundation *p)
{
    CHECK(p == &f && f.bm && f.expected_pmc == 0xffffffff);
    int rc = step(IMEM, GF1_ENOSPC);
    if (rc && !imem_partial) return rc;
    f.mapped = true; f.bar2 = (volatile u8 *)&variable;
    f.imem.aperture_bytes = 0x180000;
    f.imem.vbios = (struct gf_inst){0,0x10000,&f.imem.ramht};
    f.imem.ramht = (struct gf_inst){0x10000,0x8000,&f.imem.ramro};
    f.imem.ramro = (struct gf_inst){0x18000,0x8000,&f.imem.ramfc};
    f.imem.ramfc = (struct gf_inst){0x20000,0x20000,0};
    f.imem.used = &f.imem.vbios;
    return rc;
}
void gf_tick_poll(void *arg) { CHECK(arg == &f); }
int geforce_tick_bind(void (*poll)(void *), void *arg)
{
    if (!poll) {
        CHECK(!arg && !host_if && !f.running);
        int rc = step(UNBIND, -1);
        if (!rc) bound = false;
        return rc;
    }
    CHECK(poll == gf_tick_poll && arg == &f && f.mapped && !f.running);
    int rc = step(BIND, -1);
    if (!rc) bound = true;
    return rc;
}
int gf_irq_attach(struct gf_foundation *p)
{
    CHECK(p == &f && bound && !f.running && host_if);
    int rc = step(ATTACH, GF1_EIRQ);
    if (!rc) f.registered = true;
    return rc;
}
int gf_irq_enable(struct gf_foundation *p, bool fifo, bool graph, unsigned heads)
{
    CHECK(p == &f && f.registered && bound && f.running && !host_if);
    CHECK(fifo && graph && heads == 1);
    return step(ENABLE, GF1_EIO);
}
int gf_engine_stop(struct gf_foundation *p)
{
    CHECK(p == &f && !f.running && !bound && host_if && mask_pos == 18);
    int rc = step(ENGINE, GF1_ESTOP);
    if (!rc) { engine_done = true; pmc |= 0x1102; }
    return rc;
}
int gf_irq_detach(struct gf_foundation *p)
{
    CHECK(p == &f && engine_done && !bound && !f.running && host_if);
    CHECK(pmc_reads && f.expected_pmc == pmc);
    int rc = step(DETACH, GF1_EIRQ);
    if (!rc) f.registered = false;
    return rc;
}
int gf_master_disable(struct gf_foundation *p)
{
    CHECK(p == &f && engine_done && !f.registered && host_if);
    int rc = step(DISABLE, GF1_EPCI);
    if (!rc) { command = RECOVER_COMMAND_STOP; f.expected_command = command; f.bm = false; }
    return rc;
}
void gf_inst_free(struct gf_foundation *p, struct gf_inst *obj)
{
    CHECK(p == &f && !f.bm && !f.registered && !bound && engine_done);
    if (obj == &f.imem.vbios || obj == &f.imem.ramht ||
        obj == &f.imem.ramro || obj == &f.imem.ramfc) return;
    CHECK(f.imem.vbios.size && f.imem.ramfc.size);
    CHECK(!step(FREE, 0));
    struct gf_inst **link = &f.imem.used;
    while (*link && *link != obj) link = &(*link)->next;
    CHECK(*link == obj);
    *link = obj->next; obj->next = 0;
}
int gf_instmem_unmap(struct gf_foundation *p)
{
    CHECK(p == &f && f.mapped && !f.bm && !f.registered && !bound && !f.running);
    CHECK(!f.imem.used && !f.imem.vbios.size && !f.imem.ramht.size &&
          !f.imem.ramro.size && !f.imem.ramfc.size);
    int rc = step(UNMAP, GF1_EMAP);
    if (!rc) { f.mapped = false; f.bar2 = 0; f.imem.aperture_bytes = 0; }
    return rc;
}
static void reset(void)
{
    f = (struct gf_foundation){0};
    f.io.opaque = &f; f.io.read = rd; f.io.write = wr;
    f.expected_command = command = RECOVER_COMMAND_STOP;
    host_if = 0x200; saves = restores = 0;
    count = fail_step = fail_second = io_count = masks = mask_pos = 0;
    io_fault = ~0U;
    pmc = f.expected_pmc = 0xabcde000; pmc_reads = 0; mc_partial = false;
    bound = engine_done = master_partial = imem_partial = false;
}
static void clean(void)
{
    CHECK(!f.running && !bound && !f.registered && !f.bm && !f.mapped);
    CHECK(!f.imem.used && !f.bar2 && !f.imem.aperture_bytes);
    CHECK(command == RECOVER_COMMAND_STOP && f.expected_command == command);
    CHECK(host_if && saves == restores);
    CHECK(pmc_reads && f.expected_pmc == pmc);
}
static void sequence(const unsigned *want, unsigned n)
{
    CHECK(count == n);
    for (unsigned i = 0; i < n; i++) CHECK(trace[i] == want[i]);
}
void _start(void)
{
    f=(struct gf_foundation){.running=true,.bm=true,.registered=true,.mapped=true,
        .expected_command=RECOVER_COMMAND_RUN,.expected_pmc=0xabcde000U};
    host_if=1;
    gf_foundation_revoke(&f);
    CHECK(f.revoked && !f.running && f.bm && f.registered && f.mapped);
    CHECK(f.expected_command==RECOVER_COMMAND_RUN && f.expected_pmc==0xabcde000U);
    CHECK(host_if==1 && saves==restores && !count && !io_count);
    host_if=0;
    gf_foundation_revoke(&f);
    CHECK(f.revoked && !f.running && !host_if && saves==restores);
    static const unsigned start[] = {MASTER,MASK,MC,IMEM,BIND,ATTACH,ENABLE};
    static const unsigned stop[] = {UNBIND,MASK,ENGINE,PMC_READ,DETACH,DISABLE,FREE,FREE,UNMAP};
    static const unsigned failed[] = {MASTER,MC,IMEM,BIND,ATTACH,ENABLE};
    static const int errors[] = {GF1_EPCI,GF1_EIO,GF1_ENOSPC,GF1_EIRQ,GF1_EIRQ,GF1_EIO};
    for (unsigned pass = 0; pass < 2; pass++) {
        reset();
        for (unsigned restart = 0; restart < 2; restart++) {
            count = 0;
            CHECK(gf_foundation_start(&f) == 0 && !f.last_rc);
            CHECK(f.running && bound && f.bm && f.registered && f.mapped);
            sequence(start, sizeof(start)/sizeof(start[0]));
            /* Caller-owned variables after fixed regions; both must be freed. */
            variable[0] = (struct gf_inst){0x40000,0x1000,&variable[1]};
            variable[1] = (struct gf_inst){0x41000,0x1000,0};
            f.imem.ramfc.next = &variable[0];
            count = 0;
            CHECK(gf_foundation_stop(&f) == 0 && !f.last_rc);
            sequence(stop, sizeof(stop)/sizeof(stop[0]));
            clean();
            CHECK(!variable[0].next && !variable[1].next);
            CHECK(gf_foundation_stop(&f) == 0);
            clean();
        }
    }
    for (unsigned i = 0; i < sizeof(failed)/sizeof(failed[0]); i++) {
        for (unsigned partial = 0; partial < 2; partial++) {
            reset(); fail_step = failed[i]; master_partial = imem_partial = mc_partial = partial;
            CHECK(gf_foundation_start(&f) == errors[i] && f.last_rc == errors[i]);
            clean();
            CHECK(gf_foundation_start(&f) == 0);
            CHECK(gf_foundation_stop(&f) == 0); clean();
        }
    }
    /* Each mask/ACK operation can fail, both before MC and during stop. */
    for (unsigned i = 1; i <= 18; i++) {
        reset(); io_fault = i;
        CHECK(gf_foundation_start(&f) == GF1_EIO && f.last_rc == GF1_EIO);
        clean();
        reset(); CHECK(gf_foundation_start(&f) == 0);
        io_fault = io_count + i;
        CHECK(gf_foundation_stop(&f) == GF1_EIO && f.last_rc == GF1_EIO);
        CHECK(!bound && !f.running && f.bm && f.registered && f.mapped);
        CHECK(gf_foundation_stop(&f) == 0); clean();
    }
    /* PMC readback after reset is fallible and must retain expected state,
     * BM, IRQ and map until an explicit stop retry succeeds. */
    reset(); CHECK(gf_foundation_start(&f) == 0);
    f.expected_pmc = 0x12340000;
    fail_step = PMC_READ;
    CHECK(gf_foundation_stop(&f) == GF1_EIO && f.last_rc == GF1_EIO);
    CHECK(f.expected_pmc == 0x12340000 && pmc_reads == 1);
    CHECK(!f.running && !bound && f.bm && f.registered && f.mapped);
    CHECK(gf_foundation_stop(&f) == 0); clean();
    /* MC fails before writing PMC; reset's actual final value is authoritative. */
    reset(); fail_step = MC;
    CHECK(gf_foundation_start(&f) == GF1_EIO);
    CHECK(pmc == 0xabcdf102 && f.expected_pmc == pmc && pmc_reads == 1);
    clean();
    reset(); fail_step = MC; fail_second = PMC_READ;
    CHECK(gf_foundation_start(&f) == GF1_EIO);
    CHECK(f.expected_pmc == 0xabcde000 && pmc == 0xabcdf102);
    CHECK(f.bm && !f.mapped && !f.registered && !bound && !f.running);
    CHECK(gf_foundation_stop(&f) == 0); clean();
    static const unsigned stop_fail[] = {UNBIND,ENGINE,DETACH,DISABLE,UNMAP};
    static const int stop_errors[] = {GF1_EIRQ,GF1_ESTOP,GF1_EIRQ,GF1_EPCI,GF1_EMAP};
    for (unsigned i = 0; i < sizeof(stop_fail)/sizeof(stop_fail[0]); i++) {
        reset(); CHECK(gf_foundation_start(&f) == 0);
        fail_step = stop_fail[i];
        CHECK(gf_foundation_stop(&f) == stop_errors[i] && f.last_rc == stop_errors[i]);
        CHECK(!f.running && f.mapped);
        CHECK(f.registered == (i <= 2) && f.bm == (i <= 3));
        CHECK(bound == (i == 0));
        CHECK(f.bar2 && f.imem.aperture_bytes);
        CHECK(gf_foundation_stop(&f) == 0); clean();
        /* Rollback failure also preserves ownership for an explicit retry. */
        reset(); fail_step = ENABLE; fail_second = stop_fail[i];
        CHECK(gf_foundation_start(&f) == stop_errors[i] && f.last_rc == stop_errors[i]);
        CHECK(!f.running && f.mapped);
        CHECK(gf_foundation_stop(&f) == 0); clean();
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
