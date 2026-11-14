/* Shared host vectors: provider selection and privileged operations are seams. */
#include "types.h"
#include "kernel/irq/tick.h"
volatile u32 tick_count;
static u32 irq_state = 1;
u32 irq_save(void) { u32 old = irq_state; irq_state = 0; return old; }
void irq_restore(u32 old) { irq_state = old; }
u32 ppc_irq_save(void) { return irq_save(); }
void ppc_irq_restore(u32 old) { irq_restore(old); }
void _enable(void) { irq_state = 1; }
void _halt(void) {}
void ppc_enable(void) { irq_state = 1; }
#include IDLE_SOURCE
void _idle(void) { arch_cpu_idle(); }
static unsigned int work_calls;
void idle_work_run(void) { ++work_calls; }
int tick_register(unsigned int slot, tick_fn fn) { (void)slot; (void)fn; return 0; }
#include PROVIDER_SOURCE

static int time3(void)
{
    static const u32 inputs[] = { 0, 3, 0xfffffffdu, 0xffffffffu };
    static const u32 expected[] = { 0, 3, 0xfffffffdu, 0xffffffffu };
    unsigned int i;
    for (i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        tick_count = inputs[i];
        if (link_platform_now() != expected[i]) return 1;
    }
    return 0;
}
#ifdef MPIC_SOURCE
#include "platform/powermac/mpic.h"
#include "arch/ppc/ppc_trap.h"
static u32 regs[PM_MPIC_WINDOW_MAX / 4];
static unsigned eois, deliveries;
u32 pm_mpic_bus_read(u32 address, int little)
{ (void)little; return regs[address / 4]; }
void pm_mpic_bus_write(u32 address, u32 value, int little)
{ (void)little; regs[address / 4] = value; if (address == 0x200b0) ++eois; }
void ppc_trap_set_handler(u32 vector, void (*fn)(struct ppc_frame *))
{ (void)vector; (void)fn; }
#include MPIC_SOURCE
static void delivered(struct ppc_frame *frame) { (void)frame; ++deliveries; }
static int unchanged(struct irq_claim *c)
{ return c->line == 17 && c->native_token == 18 && c->deliver == 19 && c->needs_finish == 20; }
#define VERIFY(expr) do { if (!(expr)) return __LINE__; } while (0)
static int irq1(void)
{
    struct irq_claim c = {17, 18, 19, 20}, other = {17, 18, 19, 20};
    VERIFY(platform_irq_claim(0, &c) < 0 && unchanged(&c));
    regs[0x1000 / 4] = 3U << 16;
    regs[0x200a0 / 4] = PM_MPIC_SPURIOUS;
    VERIFY(pm_mpic_init(0, PM_MPIC_WINDOW_MAX, PM_MPIC_MMIO_BE) == 0);
    eois = 0;
    VERIFY(platform_irq_claim(0, 0) < 0 && unchanged(&c));
    VERIFY(platform_irq_claim(PM_MPIC_SPURIOUS, &c) < 0 && unchanged(&c));
    VERIFY(platform_irq_claim(4, &c) < 0 && unchanged(&c));
    VERIFY(platform_irq_claim(2, &other) == 1);
    VERIFY(other.line == 2 && other.native_token == 2 && other.deliver == 1 && other.needs_finish == 1);
    VERIFY(platform_irq_claim(1, &c) < 0 && unchanged(&c));
    platform_irq_complete(0);
    c = other; c.native_token = 1; platform_irq_complete(&c);
    c = other; c.needs_finish = 0; platform_irq_complete(&c);
    VERIFY(eois == 0);
    platform_irq_complete(&other); platform_irq_complete(&other);
    VERIFY(eois == 1);
    pm_mpic_set_ipi_handler(delivered);
    pm_mpic_external(0);
    VERIFY(deliveries == 0 && eois == 1);
    regs[0x200a0 / 4] = 4;
    pm_mpic_external(0);
    VERIFY(deliveries == 0 && eois == 2);
    regs[0x200a0 / 4] = PM_MPIC_IPI_VECTOR;
    VERIFY(platform_irq_claim(PM_MPIC_IPI_VECTOR, &c) == 1);
    VERIFY(c.line == PM_MPIC_IPI_VECTOR && c.native_token == PM_MPIC_IPI_VECTOR && c.deliver == 1 && c.needs_finish == 1);
    platform_irq_complete(&c);
    pm_mpic_external(0);
    VERIFY(deliveries == 1 && eois == 4);
    return 0;
}
#endif
#ifdef IDLE_SOURCE
static int sync2(void)
{
    irq_state = 0;
    _idle();
    if (!irq_state) return 2;
    irq_state = 0;
    link_platform_idle();
    if (!irq_state || work_calls != 1) return 3;
    link_platform_idle();
    if (!irq_state || work_calls != 2) return 4;
    return 0;
}
#endif
void _start(void)
{
    int rc = time3();
#ifdef IDLE_SOURCE
    if (!rc) rc = sync2();
#endif
#ifdef MPIC_SOURCE
    if (!rc) rc = irq1();
#endif
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
