/* Native kernel layout; independent bootstrap PM_LINK_BASE is not applicable.
 * C includes memmap.h; ld uses tools/gen_memmap.py's memmap_layout.h first.
 * Preprocess kernel.ld with -I<generated directory> -I. . */
#ifndef OS32_KBOOT_LAYOUT_H
#define OS32_KBOOT_LAYOUT_H
#if defined(PM_KBOOT_LINKER) || defined(__ASSEMBLER__)
#include "memmap_layout.h"
#define PM_KERNEL_BASE LAYOUT_KERNEL_LOAD_ADDR
#define PM_KERNEL_IMAGE_MAX LAYOUT_MEM_KERNEL_IMAGE_MAX
#define PM_KERNEL_PAGE_SIZE LAYOUT_MEM_PAGE_SIZE
#define PM_KERNEL_LEDGER_SIZE LAYOUT_MEM_LEDGER_META_SIZE
#define PM_KERNEL_HEAP_SIZE LAYOUT_KHEAP_SIZE
#define PM_KERNEL_KAPI_SIZE LAYOUT_MEM_KAPI_SIZE
#define PM_KERNEL_GUARD_SIZE LAYOUT_MEM_GUARD_SIZE
#define PM_KERNEL_SHM_SIZE LAYOUT_MEM_SHM_SIZE
#define PM_KERNEL_CHAIN_LIMIT LAYOUT_MEM_FIXED_PAGING_GUARD_BASE
#else
#include "memmap.h"
#define PM_KERNEL_BASE KERNEL_LOAD_ADDR
#define PM_KERNEL_IMAGE_MAX MEM_KERNEL_IMAGE_MAX
#define PM_KERNEL_PAGE_SIZE MEM_PAGE_SIZE
#define PM_KERNEL_LEDGER_SIZE MEM_LEDGER_META_SIZE
#define PM_KERNEL_HEAP_SIZE KHEAP_SIZE
#define PM_KERNEL_KAPI_SIZE MEM_KAPI_SIZE
#define PM_KERNEL_GUARD_SIZE MEM_GUARD_SIZE
#define PM_KERNEL_SHM_SIZE MEM_SHM_SIZE
#define PM_KERNEL_CHAIN_LIMIT MEM_FIXED_PAGING_GUARD_BASE
#endif
#define PM_KERNEL_STACK_SIZE 0x8000
#define PM_KERNEL_STACK_ALIGN 16
#define PM_KERNEL_NOTE_NAMESZ 8
#define PM_KERNEL_NOTE_DESCSZ 20
#define PM_KERNEL_NOTE_TYPE 0x1275
#ifndef PM_KBOOT_LINKER
#ifndef __ASSEMBLER__
extern char __kernel_image_start[], __kernel_file_end[];
extern char __bss_start[], __bss_end[];
extern char __kernel_stack_bottom[], __kernel_stack_top[];
#endif
#endif
#endif
