/* Actual GF1 status observation, with a stale production loan. */
#include "../../kdebug/gf_foundation_test.c"
static struct gf_production status_owner;
static u32 pci_reads, io_reads;
static __attribute__((noreturn)) void finish(int rc)
{
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc));
    __builtin_unreachable();
}
#define CHECK(x) do { __asm__ volatile("":::"memory"); if (!(x)) finish(1); } while (0)
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 off)
{ (void)b;(void)d;(void)f;(void)off;pci_reads++;return RECOVER_COMMAND_STOP; }
static int read_pmc(void *p,u32 bar,u32 off,u32 size,u32 *out)
{ (void)p;CHECK(!bar && off==RECOVER_PMC_ENABLE && size==4);io_reads++;*out=123;return 0; }
int recover_target_address(const struct recover_target *t,u32 op,struct recover_bus_access *a)
{ (void)t;(void)op;(void)a;finish(2); }
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
    finish(0);
}
