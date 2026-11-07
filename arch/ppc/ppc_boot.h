#ifndef OS32_PPC_BOOT_H
#define OS32_PPC_BOOT_H
/* The firmware ABI and the host fake both use BE32-sized cells/pointers. */
typedef unsigned int cell;
typedef int (*ppc_ci)(void *);
_Static_assert(sizeof(cell) == 4, "firmware cell size");
_Static_assert(sizeof(void *) == 4, "firmware pointer size");
static inline cell ppc_ptr_cell(const void *p) { return (cell)p; }
static inline void *ppc_cell_ptr(cell a) { return (void *)a; }
unsigned int ppc_load8(cell va);
void ppc_store8(cell va, unsigned int value);
void ppc_sync(void);
void ppc_disable(void);
void ppc_stop(void) __attribute__((noreturn));
void powermac_boot(ppc_ci ci);
void powermac_mmu_boot(ppc_ci ci);
#endif
