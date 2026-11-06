/* Actual GF1 status observation, with a stale production loan. */
#include "../../kdebug/gf_foundation_test.c"
static struct gf_production status_owner;
static u32 pci_reads, io_reads;
static u16 command = RECOVER_COMMAND_STOP;
static u32 hardware_pmc = 123;
static __attribute__((noreturn)) void finish(int rc)
{
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc));
    __builtin_unreachable();
}
#define CHECK(x) do { __asm__ volatile("":::"memory"); if (!(x)) finish(1); } while (0)
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 off)
{ (void)b;(void)d;(void)f;(void)off;pci_reads++;return command; }
static int read_pmc(void *p,u32 bar,u32 off,u32 size,u32 *out)
{ (void)p;CHECK(!bar && off==RECOVER_PMC_ENABLE && size==4);io_reads++;*out=hardware_pmc;return 0; }
int recover_target_address(const struct recover_target *t,u32 op,struct recover_bus_access *a)
{ (void)t;(void)op;(void)a;finish(2); }
static int write_mc(void *p,u32 bar,u32 off,u32 size,u32 value)
{ (void)p;(void)value;CHECK(!bar && size==4);(void)off;return 0; }
static int read_mc(void *p,u32 bar,u32 off,u32 size,u32 *out)
{
    if (off==RECOVER_PMC_ENABLE) return read_pmc(p,bar,off,size,out);
    CHECK(off==GF_PRODUCTION_PFB_AMOUNT);*out=0x10000000U;return 0;
}
/* Snapshot adapter follows the module bridge's expected-state contract. */
static int status_snapshot(struct gf_foundation *f,const struct gf1_test_request *q,
                           struct gf1_test_reply *reply)
{
    CHECK(q->op==GF1T_SNAPSHOT);
    *reply=(struct gf1_test_reply){.command=f->expected_command,.pmc=f->expected_pmc};
    return 0;
}
static u32 ticks(void *p) { (void)p;return 0; }
int recover_safe_point(struct recover *r) { (void)r;finish(3); }
int recover_hal_wait_frame(void *p,u32 us) { (void)p;(void)us;finish(3); }
void _start(void)
{
    struct gf1_observer o={.f=&status_owner.foundation,.production=true,.test=GF1_STATUS};
    struct gf1_phase phase={0};
    CHECK(actual(&o,&phase)==GF1_EINVAL && !pci_reads && !io_reads);
    status_owner.display.io.read=read_pmc;
    CHECK(actual(&o,&phase)==GF1_EINVAL && !pci_reads && !io_reads);
    status_owner.prepared=true;status_owner.display.io.read=0;
    CHECK(actual(&o,&phase)==GF1_EINVAL && !pci_reads && !io_reads);
    status_owner.display.io.read=read_pmc;
    CHECK(!actual(&o,&phase) && pci_reads==1 && io_reads==1 && phase.pmc==123);
    /* Entry: MC has run, no leave/readback has repaired expected_pmc yet. */
    struct gf_foundation *f=&status_owner.foundation;
    f->io=(struct gf_io){.read=read_mc,.write=write_mc};
    hardware_pmc=0x17111113U;
    f->expected_pmc=0xffffffffU;
    CHECK(gf_mc_init(f)==0);
    command=f->expected_command=RECOVER_COMMAND_RUN;
    f->running=f->bm=f->mapped=f->registered=true;
    const struct gf1_test_ops ops={.call=status_snapshot};
    const struct recover_ops base={.ticks=ticks};
    o.ops=&ops;o.base=&base;o.phase=&phase;
    CHECK(observe(0,&o)==GF1_OK && phase.snapshot && phase.pmc_valid);
    CHECK(phase.reply.pmc==hardware_pmc && phase.pmc==hardware_pmc);
    /* Status must continue detecting drift rather than learning it. */
    hardware_pmc^=1U;
    CHECK(observe(0,&o)==GF1_EIO);
    finish(0);
}
