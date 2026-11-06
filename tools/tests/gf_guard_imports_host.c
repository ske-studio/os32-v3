/* Exercise the generated i386 import jumps with distinct provider results. */
#include "geforce_imports.h"
static u32 seen;
static int lookup(u32 base, u32 bytes, u32 *owner, u32 *rid)
{
    if (base != 0x25000000U || bytes != 8192U) return -1;
    *owner = 17; *rid = 23; seen |= 1; return 0;
}
static int map(u32 owner, u32 rid, u32 first, u32 end)
{
    if (owner != 17 || rid != 23 || first != 0x25000 || end != first + 2) return -1;
    seen |= 2; return 41;
}
static int unmap(u32 owner, u32 rid, u32 first, u32 end)
{
    if (owner != 17 || rid != 23 || first != 0x25000 || end != first + 2) return -1;
    seen |= 4; return 43;
}
void _start(void)
{
    u32 owner = 0, rid = 0;
    int rc = 1;
    geforce_import_slots[LOOKUP_SLOT] = (unsigned long)lookup;
    geforce_import_slots[MAP_SLOT] = (unsigned long)map;
    geforce_import_slots[UNMAP_SLOT] = (unsigned long)unmap;
    __asm__ volatile("" : : : "memory");
    if (!geforce_resource_lookup(0x25000000U, 8192U, &owner, &rid) &&
        paging_device_map(owner, rid, 0x25000, 0x25002) == 41 &&
        paging_device_unmap(owner, rid, 0x25000, 0x25002) == 43 && seen == 7)
        rc = 0;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
