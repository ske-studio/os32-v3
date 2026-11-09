/* OpenPIC CPU0 driver. Register layout: OpenBSD macppc/openpicreg.h:35-90;
 * setup: openpic.c:212-299; completion: FreeBSD openpic.c:342-351.
 * No reset wait, PCI unmask, DEC registration, or common IRQ bitmap. */
#include "mpic.h"
#include "ppc_trap.h"
#define FEATURE 0x1000U
#define CONFIG 0x1020U
#define PASSTHRU_DISABLE 0x20000000U
#define IPI_VECTOR 0x10a0U
#define SPURIOUS 0x10e0U
#define SRC_VECTOR(s) (0x10000U + 32U * (s))
#define IDEST(s) (0x10010U + 32U * (s))
#define IPI0 0x20040U
#define TPR 0x20080U
#define IACK 0x200a0U
#define EOI 0x200b0U
#define MASK 0x80000000U
#define PRIORITY_ONE 0x00010000U
static u32 base, sources;
static int order, ready, active;
static unsigned active_token;
static void (*ipi_handler)(struct ppc_frame *);
#ifdef PM_MPIC_HOST_TEST
/* Fake only the bus transaction; the complete driver runs unchanged. */
extern u32 pm_mpic_bus_read(u32 address, int little);
extern void pm_mpic_bus_write(u32 address, u32 value, int little);
#else
static u32 pm_mpic_bus_read(u32 address, int little)
{
    u32 value;
    if (little)
        __asm__ volatile("lwbrx %0,0,%1; eieio" : "=r"(value) : "r"(address) : "memory");
    else
        __asm__ volatile("lwz %0,0(%1); eieio" : "=r"(value) : "r"(address) : "memory");
    return value;
}
static void pm_mpic_bus_write(u32 address, u32 value, int little)
{
    if (little)
        __asm__ volatile("stwbrx %0,0,%1; eieio" : : "r"(value), "r"(address) : "memory");
    else
        __asm__ volatile("stw %0,0(%1); eieio" : : "r"(value), "r"(address) : "memory");
}
#endif
static u32 read_reg(u32 offset)
{ return pm_mpic_bus_read(base + offset, order); }
static void write_reg(u32 offset, u32 value)
{ pm_mpic_bus_write(base + offset, value, order); }
int pm_mpic_init(u32 va, u32 bytes, int mmio_order)
{
    ready = 0;
    active = 0;
    sources = 0;
    ipi_handler = 0;
    /* Include the PA page offset retained by the alias in the rounded bound. */
    if ((va & 3) || bytes < PM_MPIC_WINDOW_MIN ||
        bytes > PM_MPIC_WINDOW_MAX - (va & 4095U) ||
        va > 0xffffffffU - (bytes - 1)) return PM_MPIC_WINDOW;
    if (mmio_order != PM_MPIC_MMIO_LE && mmio_order != PM_MPIC_MMIO_BE)
        return PM_MPIC_ORDER;
    base = va;
    order = mmio_order == PM_MPIC_MMIO_LE;
    /* FEATURE has an 11-bit last-source field: do not truncate to our limit. */
    u32 count = ((read_reg(FEATURE) >> 16) & 0x7ffU) + 1;
    if (count > PM_MPIC_MAX_SOURCES) return PM_MPIC_SOURCES;
    sources = count;
    write_reg(TPR, 15);
    for (u32 s = 0; s < count; ++s) {
        write_reg(SRC_VECTOR(s), MASK | PRIORITY_ONE | s);
        write_reg(IDEST(s), 1);
    }
    write_reg(CONFIG, read_reg(CONFIG) | PASSTHRU_DISABLE);
    write_reg(SPURIOUS, PM_MPIC_SPURIOUS);
    u32 n;
    for (n = 0; n < PM_MPIC_DRAIN_LIMIT; ++n) {
        if ((read_reg(IACK) & 255U) == PM_MPIC_SPURIOUS) break;
        write_reg(EOI, 0);
    }
    if (n == PM_MPIC_DRAIN_LIMIT) return PM_MPIC_DRAIN;
    write_reg(IPI_VECTOR, PRIORITY_ONE | PM_MPIC_IPI_VECTOR);
    ready = 1;
    platform_boot_irq_controller();
    write_reg(TPR, 0);
    return PM_MPIC_INIT_OK;
}
void platform_boot_irq_controller(void)
{
    if (ready) ppc_trap_set_handler(0x500, pm_mpic_external);
}
void pm_mpic_set_ipi_handler(void (*handler)(struct ppc_frame *))
{ ipi_handler = handler; }
u32 pm_mpic_source_count(void) { return sources; }
int platform_irq_claim(unsigned token, struct irq_claim *out)
{
    if (!ready || !out || active || token == PM_MPIC_SPURIOUS ||
        (token >= sources && token != PM_MPIC_IPI_VECTOR)) return 0;
    out->native_token = token;
    active_token = token;
    active = 1;
    return 1;
}
void platform_irq_complete(const struct irq_claim *claim)
{
    if (!claim || !active || claim->native_token != active_token) return;
    active = 0;
    write_reg(EOI, 0);
}
void pm_mpic_external(struct ppc_frame *frame)
{
    if (!ready) return;
    unsigned token = read_reg(IACK) & 255U;
    if (token == PM_MPIC_SPURIOUS) return;
    struct irq_claim claim;
    if (platform_irq_claim(token, &claim)) {
        if (token == PM_MPIC_IPI_VECTOR && ipi_handler) ipi_handler(frame);
        platform_irq_complete(&claim);
    } else {
        /* Unexpected hardware vector still owns an IACK; release it once. */
        write_reg(EOI, 0);
    }
}
int pm_mpic_send_self_ipi(u32 n)
{
    if (!ready || n != 0) return 0;
    write_reg(IPI0, 1);
    return 1;
}
