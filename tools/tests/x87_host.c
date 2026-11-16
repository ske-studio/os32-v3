#include "x87.h"

/* Only CR0/IF/AC are privileged hooks; probe and saves use actual x87. */
u32 x87_test_irq_save(void) { return 0x200; }
void x87_test_irq_restore(u32 f) { (void)f; }
static u32 cr0;
u32 x87_test_cr0_read(void) { return cr0; }
void x87_test_cr0_write(u32 f) { cr0 = f; }
int x87_test_has_ac(void) { return 1; }

static struct arch_fp_context parent[3], parked;
static unsigned int value[2];
#ifdef OS32_TEST_NOFPU
#include "x87_emu.h"
#include "x87_nm.h"
#include "appslot.h"
#include "paging.h"
#include "gdt.h"
static struct addrspace owner;
static AppSlot slot;
int appslot_cur(void) { return 1; }
AppSlot *appslot_get(int id) { return id == 1 ? &slot : 0; }
int res_owner_get(void) { return 1; }
u32 paging_current_cr3(void) { return owner.pd_phys; }
int as_access_page(const struct addrspace *as, u32 va, int write, u32 *pa)
{
    (void)write;
    if (as != &owner) return 0;
    *pa = va;
    return 1;
}
static int emulate(u8 op, u8 modrm, void *memory)
{
    u8 code[] = {op, modrm};
    struct x87_emu_trap trap;
    struct x87_nm_frame frame = {0};
    slot.as = &owner;
    slot.cpl3 = 1;
    owner.owner = 8; /* ledger handle, deliberately unlike appslot 1 */
    owner.generation = 1;
    owner.pd_phys = 0x1000;
    frame.cs = USER_CS; frame.user_ss = USER_DS;
    frame.ds = frame.es = frame.fs = frame.gs = USER_DS;
    frame.eip = (u32)code; frame.eax = (u32)memory;
    frame.eflags = 0x202;
    x87_nm_read(&frame, &trap);
    return x87_emulate_nm(&trap) && trap.eip == (u32)code + 2;
}
static void control(unsigned short c) { if (!emulate(0xd9, 0x28, &c)) __builtin_trap(); }
static unsigned short get_control(void) {
    unsigned short c = 0;
    if (!emulate(0xd9, 0x38, &c)) __builtin_trap();
    return c;
}
static void push_one(void) { if (!emulate(0xd9, 0xe8, 0)) __builtin_trap(); }
static int pop_one(void) {
    return emulate(0xdd, 0x18, value) && value[0] == 0 && value[1] == 0x3ff00000;
}
#else
static void control(unsigned short c) { __asm__ volatile("fldcw %0" : : "m"(c)); }
static unsigned short get_control(void) { unsigned short c; __asm__ volatile("fnstcw %0" : "=m"(c)); return c; }
static void push_one(void) { __asm__ volatile("fld1"); }
static int pop_one(void) {
    __asm__ volatile("fstpl %0" : "=m"(value));
    return value[0] == 0 && value[1] == 0x3ff00000;
}
#endif
static int run(void) {
    x87_probe();
#ifdef OS32_TEST_NOFPU
    /* Real host x87 probe succeeds, but the forced build must select emulation. */
    if (arch_fp_present() || (cr0 & 12) != 12) return 4;
    if (get_control() != 0x037f) return 5; /* first FNSTCW through real dispatch */
#endif
    arch_fp_init(); control(0x077f); push_one();
    arch_fp_save(&parent[0]); arch_fp_init();
    control(0x0b7f); push_one();
    arch_fp_save(&parent[1]); arch_fp_init();
    control(0x037f); push_one();
    arch_fp_save(&parked);
    arch_fp_restore(&parent[1]);
    if (get_control() != 0x0b7f || !pop_one()) return 1;
    arch_fp_restore(&parked);
    if (get_control() != 0x037f || !pop_one()) return 2;
    arch_fp_restore(&parent[0]);
    if (get_control() != 0x077f || !pop_one()) return 3;
    return 0;
}
void _start(void) { int rc = run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
