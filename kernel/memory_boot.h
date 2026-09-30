#ifndef OS32_MEMORY_BOOT_H
#define OS32_MEMORY_BOOT_H
#include "types.h"

/* Boot-only: success permits downstream init; failure requires fail-stop.
 * Every configuration takes the model path (ARENA_TOP or FIXED backing,
 * TASK_T1_LEDGER §3-3); there is no legacy allocator to fall back to. */
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

/* 集積域・同梱域の規則 (TASK_T1_LEDGER §3-3 ③)。[first, end) はローダが
 * 申告した同梱エントリ (PFN、0, 0 = 申告なし)。申告があれば同梱域を
 * owner = bundle で押さえて BUNDLE の区間を登録し、同梱域の外にかかる申告は
 * 何も変えずに拒否する。集積域は展開済みなので池のまま。1 = 成功。
 * memory_boot_init が区間の表の最後に呼ぶ (T1 の時点では常に申告なし)。 */
int memory_boot_boot_areas(u32 first, u32 end);
#endif
