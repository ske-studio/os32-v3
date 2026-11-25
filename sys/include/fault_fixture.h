#ifndef FAULT_FIXTURE_H
#define FAULT_FIXTURE_H
/* Destructive test injection; unsupported architectures do not select it. */
void arch_fixture_ud(void);
void arch_fixture_gp(void);
#endif
