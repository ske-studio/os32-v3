#include "x87.h"
#include "cpu.h"
#ifdef OS32_X87_EMU_LINKED
#include "x87_emu.h"
#include "appslot.h"
static struct x87_emu_state x87_active;
#endif
#if defined(OS32_TEST_NOFPU) && !defined(OS32_X87_EMU_LINKED)
#error OS32_TEST_NOFPU requires the linked software emulator
#endif

/* FNSAVE/FRSTOR の域は 108 バイト。AppSlot の中の位置を動かさない (整列 1)。 */
_Static_assert(sizeof(struct arch_fp_context) == 108 &&
               __alignof__(struct arch_fp_context) == 1, "x87 FNSAVE area");

static int x87_present;
static volatile int probe_active;
extern char x87_probe_init[], x87_probe_status[], x87_probe_control[], x87_probe_absent[];

/* Privileged operations alone are replaced by the libc-free host fixture. */
#ifdef X87_HOST_TEST
extern u32 x87_test_irq_save(void), x87_test_cr0_read(void);
extern void x87_test_irq_restore(u32), x87_test_cr0_write(u32);
extern int x87_test_has_ac(void);
#define fp_irq_save x87_test_irq_save
#define fp_irq_restore x87_test_irq_restore
#define fp_cr0_read x87_test_cr0_read
#define fp_cr0_write x87_test_cr0_write
#define fp_has_ac x87_test_has_ac
#else
#include "io.h"
static u32 fp_irq_save(void)
{
    return irq_save();
}
static void fp_irq_restore(u32 flags)
{
    irq_restore(flags);
}
static u32 fp_cr0_read(void)
{
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}
static void fp_cr0_write(u32 cr0)
{
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}
static int fp_has_ac(void)
{
    u32 original, changed;
    /* ESP is word aligned; no memory access while AC is set except push/pop. */
    __asm__ volatile("pushfl; popl %0; movl %0, %1; xorl $0x40000, %1;"
                     "pushl %1; popfl; pushfl; popl %1; pushl %0; popfl"
                     : "=&r"(original), "=&r"(changed) : : "cc", "memory");
    return ((original ^ changed) & 0x40000u) != 0;
}
#endif

int arch_fp_present(void) { return x87_present; }

int x87_probe_nm(u32 saved_cs, u32 saved_eflags, u32 *saved_eip)
{
    if ((saved_cs & 3u) || (saved_eflags & (1u << 17)) || !probe_active)
        return 0;
    if (*saved_eip != (u32)x87_probe_init &&
        *saved_eip != (u32)x87_probe_status &&
        *saved_eip != (u32)x87_probe_control)
        return 0;
    *saved_eip = (u32)x87_probe_absent;
    return 1;
}

void x87_probe(void)
{
    u32 flags = fp_irq_save();
    u32 original = fp_cr0_read();
    int has_ac = fp_has_ac();
    u16 status = 0xffff, control = 0xffff;
    fp_cr0_write(original & ~(u32)(CR0_EM | CR0_TS));
    probe_active = 1;
    __asm__ volatile(
        "x87_probe_init: fninit\n\t"
        "x87_probe_status: fnstsw %%ax\n\t"
        "testw %%ax, %%ax; jnz 1f\n\t"
        "x87_probe_control: fnstcw %1\n\t"
        "jmp 1f\n\t"
        "x87_probe_absent: movw $0xffff, %%ax\n"
        "1:"
        : "+a"(status), "+m"(control) : : "memory", "cc");
    probe_active = 0;
    x87_present = status == 0 && (control & 0x103f) == 0x003f;
#ifdef OS32_TEST_NOFPU
    x87_present = 0;
#endif
    u32 final = original | CR0_MP;
    if (x87_present) final &= ~(u32)(CR0_EM | CR0_TS);
    else final |= CR0_EM | CR0_TS;
    if (has_ac) {
        if (x87_present) final |= CR0_NE;
        else final &= ~(u32)CR0_NE;
    }
    fp_cr0_write(final);
#ifdef OS32_X87_EMU_LINKED
    if (!x87_present) x87_emu_state_init(&x87_active, true);
#endif
    fp_irq_restore(flags);
}

void arch_fp_save(struct arch_fp_context *state)
{
    if (x87_present) __asm__ volatile("fnsave %0; fwait" : "=m"(*state) : : "memory");
#ifdef OS32_X87_EMU_LINKED
    else {
        x87_emu_state_encode(&x87_active, (u8 *)state, false, false);
        x87_emu_state_init(&x87_active, false);
    }
#endif
}

void arch_fp_restore(const struct arch_fp_context *state)
{
    if (x87_present) __asm__ volatile("frstor %0" : : "m"(*state) : "memory");
#ifdef OS32_X87_EMU_LINKED
    else x87_emu_state_decode(&x87_active, (const u8 *)state, false, false);
#endif
}

void arch_fp_init(void)
{
    if (x87_present) __asm__ volatile("fninit" : : : "memory");
#ifdef OS32_X87_EMU_LINKED
    else x87_emu_state_init(&x87_active, true);
#endif
}

int x87_emulate_nm(struct x87_emu_trap *trap)
{
#ifdef OS32_X87_EMU_LINKED
    AppSlot *slot = appslot_get(appslot_cur());
    return slot && x87_emu_dispatch(trap, slot->as, &x87_active) == X87_EMU_OK;
#else
    (void)trap;
    return 0; /* Build wiring is a separate stage; use the existing fault path. */
#endif
}
