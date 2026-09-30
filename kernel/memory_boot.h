#ifndef OS32_MEMORY_BOOT_H
#define OS32_MEMORY_BOOT_H
#include "types.h"

/* Boot-only: success permits downstream init; failure requires fail-stop.
 * Never retry or select legacy after a model bootstrap/stage attempt. */
int memory_boot_init(u32 mem_kb);

/* Physical RAM extent in KiB (top-of-RAM address / 1024), folding the old
 * loader's 512KiB probe report together with RAM at/above MEM_HIGH_RAM_BASE.
 * Call ONCE in kernel_main BEFORE paging_init, with paging off and IF=0: it
 * write-probes one dword per megabyte to confirm the BIOS work area and to
 * detect 24-bit address wrap. Returns mem_kb unchanged when no high RAM is
 * reported or confirmed. There is no artificial ceiling here; the reported
 * extent is only bounded by the top-of-4GiB ROM/MMIO band. */
u32 memory_boot_detect(u32 mem_kb);

/* KiB of physical RAM actually registered at boot: the sum of the spans this
 * unit handed to the allocator model, i.e. [0, min(top, 15MiB)) plus the
 * confirmed [MEM_HIGH_RAM_BASE, high end). 0 before memory_boot_init.
 * This is NOT memory_boot_detect / sys_get_mem_kb, which are the top-of-RAM
 * address / 1024 (K6-RAM): that number counts the PC-98 15-16MiB system space
 * as if it were RAM, so a 15MiB machine reports 17408 KiB. The two agree on
 * machines whose RAM stops below 15MiB. Like every conventional RAM size, the
 * 640KiB-1MiB VRAM/ROM window inside the first megabyte is still counted; only
 * the hole BETWEEN two RAM bands is taken out. Boot-time value, never updated
 * afterwards; device reservations do not reduce it. */
u32 memory_boot_ram_kb(void);
#endif
