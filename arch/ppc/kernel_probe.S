/* Core-ready self probes (design section 9: real fault, guard, flush).
 * Each probe has one faulting instruction with a public label so the caller
 * can arm ppc_trap_onfault(pc, va, reason, resume) for exactly that access.
 * Supervisor, IR=DR=1. No stack use; r3/r4 only (SVR4 volatile). */
    .text
    .globl ppc_probe_read, ppc_probe_read_insn, ppc_probe_read_resume
/* int ppc_probe_read(u32 va): 0 = the load completed, 1 = resumed by onfault */
ppc_probe_read:
ppc_probe_read_insn:
    lwz 4,0(3)
    li 3,0
    blr
ppc_probe_read_resume:
    li 3,1
    blr

    .globl ppc_probe_copy8, ppc_probe_copy8_insn, ppc_probe_copy8_resume
/* int ppc_probe_copy8(u32 va, u8 *out) */
ppc_probe_copy8:
ppc_probe_copy8_insn:
    lbz 5,0(3)
    stb 5,0(4)
    li 3,0
    blr
ppc_probe_copy8_resume:
    li 3,1
    blr

    .globl ppc_probe_exec, ppc_probe_exec_resume
/* int ppc_probe_exec(u32 target): fetch from target (ISI at srr0 == target).
 * LR still holds the caller's return address when onfault resumes. */
ppc_probe_exec:
    mtctr 3
    bctr
ppc_probe_exec_resume:
    li 3,1
    blr

    .globl ppc_probe_syscall
/* Negative image only: sc has no kernel meaning before milestone 3. */
ppc_probe_syscall:
    sc
    blr
