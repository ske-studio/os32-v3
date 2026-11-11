/* Milestone-2 providers for common-kernel symbols whose real owner is outside
 * the core-ready closure (PPC_KBOOT_DESIGN section 5, stage H1).
 *
 * Rule: one entry per symbol actually left undefined by the kboot link, each
 * stating the true milestone-2 fact it reports. No weak catch-all, and nothing
 * here reports a service as initialized. The link map is the audit: a symbol
 * resolved from this file means its real provider is not in the kernel yet.
 *
 * | symbol                 | real owner  | milestone-2 fact reported          |
 * | ring3_call_from_user   | exec/exec.c | no user context exists: always 0   |
 */

/* Callers (mm/shm.c ownership gates) ask "is the current caller a CPL=3 app?".
 * Milestone 2 never enters user mode: a trap with MSR[PR] set is fatal in
 * arch/ppc/kernel_trap.c (PPC_TRAP_USER). So the answer is always "no".
 * Replaced by exec/ when milestone 3 brings user execution. */
int ring3_call_from_user(void)
{
    return 0;
}

/* This image has no exec closure or user context. */
struct arch_syscall;
void exec_syscall_dispatch(struct arch_syscall *call)
{
    (void)call;
    for (;;) __asm__ volatile("b .");
}
void ring3_fault_kill(void)
{
    for (;;) __asm__ volatile("b .");
}
