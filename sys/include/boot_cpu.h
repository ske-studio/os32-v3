#ifndef BOOT_CPU_H
#define BOOT_CPU_H
/* Boot stages preserve the caller's interrupt state. */
void arch_boot_segments(void);
void arch_boot_exception_stack(void);
void arch_boot_vectors(void);
void arch_boot_after_paging(void);
#endif
