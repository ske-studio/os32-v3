/* Private guest observation record, not a public KAPI. Words are LE u32:
 * +0 tag, +4 completion (zero until reached), +8 target VA,
 * +12 ARMD (last store before the target write), +16 two-character label,
 * +20 allocated block address. Observe before another allocation reuses it.
 * shm_free_owned leaves bytes intact; shm_alloc clears them on reuse.
 *
 * PM acceptance table (one launch per row; tags/labels are LE ASCII):
 * Launch             | tag  | label | target                                  | PF error | expected
 * nop                | NOP! | OK    | 0                                       | none     | DONE, kill +0
 * ring3_hello        | RNG3 | R3    | 0                                       | none     | DONE, kill +0
 * ring3_fault        | FLT? | RF    | KERNEL_LOAD_ADDR                        | 7        | kill +1
 * ring3_guard (A)    | GRD? | RG    | MEM_APP_STACK_TOP-MEM_EXEC_STACK_SIZE-MEM_GUARD_SIZE | 6 | kill +1
 * ring3_guard shlib (B) | SLB? | RG | MEM_SHLIB_BASE                          | 7 (*)    | kill +1
 * ring3_guard cirrus (C) | VIS? | RG | Cirrus linear window, offset 0         | 7 or 6   | kill +1
 * ring3_guard pegc (D) | PEG? | RG  | PEGC_LINEAR_BASE, display surface       | 7 or 6   | kill +1
 * ring3_guard bb (E) | BB?? | RG    | MEM_GFX_BB_BASE                          | none     | SURV, kill +0
 * shm_reuse_child lockwrite first/last | SLK? | SL | locked block + 0/0x3000 | 7 | kill +1
 * These two lockwrite launches keep the marker in a separate writable block.
 * (*) Load a shared library before B to verify RO with error=7. Without a
 * mapped library it still faults, but cannot prove RO (NP gives error=6).
 * C/D: mapped supervisor display gives error=7; absent hardware/NP gives 6.
 * A's fixed target must be checked against the actual stack guard at h
 * acceptance (T2c variable stacks may place the guard elsewhere).
 * E remains USER/writable for every backend in e9, including fallback to
 * PC-9801; denial and a legitimate CLIENT lease control belong to e11.
 *
 * Use the newly deployed kernel.map's __bss_end to derive the SHM base:
 * S = align_up(__bss_end,OS32_PAGE_SIZE)+0x32000;
 * B = S + i*OS32_SHM_BLOCK_SIZE (i=0..13).
 * No absolute SHM address is fixed here. Immediately after the program exits,
 * before another SHM allocation, find B with the expected label at B+16 and
 * self-address B at B+20. Compare B+0/tag and B+8/target with the table.
 * Fault cases require B+12=ARMD, B+4=0 (never SURV), and kernel.map's
 * fault_kill_count to increase by one relative to the pre-launch value.
 * The serial line must be [ring3] #PF (CPL=3 / syscall) addr=<target> EIP=...
 * with addr matching B+8, not the marker block. B additionally prints
 * [shlib band, WRITE]. Error codes above describe the expected PF frame;
 * the current serial line does not print error_code.
 * Surviving rows require B+4=DONE/SURV as listed, no PF and no kill increment;
 * nop/ring3_hello keep B+8=B+12=0. All rows must return to the shell, and ver
 * must respond afterwards (report its first line). Missing markers, allocation
 * failure (exit 2), wrong addr, or unexpected survival are not passes.
 */
#ifndef RING3_MARKER_H
#define RING3_MARKER_H
#include "os32_kapi_slots.h"
#define R3_DONE 0x454E4F44UL
#define R3_SURV 0x56525553UL
#define R3_ARMED 0x444D5241UL

/* int 80 uses [esp+4] for arg 0, with a dummy return word at [esp]. */
static __inline__ __attribute__((always_inline)) unsigned long
r3_call(unsigned long slot, unsigned long arg)
{
    __asm__ __volatile__("pushl %1\n\tpushl $0\n\tint $0x80\n\taddl $8, %%esp"
                         : "+a"(slot) : "r"(arg) : "memory", "cc");
    return slot;
}

static __inline__ __attribute__((always_inline, noreturn)) void
r3_exit(unsigned long status)
{
    (void)r3_call(KAPI_SLOT_SYS_EXIT, status);
    for (;;) __asm__ __volatile__("" ::: "memory");
}

static __inline__ __attribute__((always_inline)) volatile unsigned long *
r3_marker(unsigned long label)
{
    volatile unsigned long *mark = (volatile unsigned long *)
        r3_call(KAPI_SLOT_SYS_SHM_ALLOC, 1);
    if (!mark) r3_exit(2); /* No marker is a failure, never a fault-test pass. */
    mark[0] = 0;
    mark[1] = 0;
    mark[2] = 0;
    mark[3] = 0;
    mark[4] = label;
    mark[5] = (unsigned long)mark;
    return mark;
}

static __inline__ __attribute__((always_inline)) void
r3_arm(volatile unsigned long *mark, unsigned long tag, unsigned long target)
{
    mark[0] = tag;
    mark[2] = target;
    mark[3] = R3_ARMED;
}
#endif
