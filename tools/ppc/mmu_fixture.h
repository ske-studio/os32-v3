#ifndef OS32_MMU_FIXTURE_H
#define OS32_MMU_FIXTURE_H
/* NetBSD 36eddebc pmap.c:788-825 => sync,sync,tlbie,sync,eieio,tlbsync,sync.
 * Event IDs belong to mmu_fake.c; explicit sequence independent of product. */
static const u32 mmu_unset_events[]={1,1,5,1,2,3,1};
/* NF:223-253 BE 2+2 cells: 128MiB; zero; PA high; size high; wrapped end. */
static const u32 mmu_memory_cells[]={0,0,0,0x08000000,0,0x1000,0,0,
    1,0,0,0x1000,0,0,1,0,0,0xfffff000,0,0x2000};
#endif
