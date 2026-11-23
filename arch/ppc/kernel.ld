/* CPP input: generate memmap_layout.h with tools/gen_memmap.py first.
 * OF note format: OpenBSD macppc/stand/ofwmagic.S:37-69.
 * The PT_LOAD owns BSS, ledger and the OF-time stack; never claim it twice. */
#if PM_PRODUCT_BOOT
#define PM_PRODUCT_LINKER
#include "platform/powermac/product_boot.h"
#endif
#define PM_KBOOT_LINKER
#include "platform/powermac/platform_kboot_layout.h"
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(ppc_kernel_entry)
PHDRS
{
    image PT_LOAD FLAGS(7);
#if PM_ROOT_FRAME
    root PT_LOAD FLAGS(4);
#endif
    ofw PT_NOTE FLAGS(4);
}
SECTIONS
{
    . = PM_KERNEL_BASE;
    __kernel_image_start = .;
#if PM_PRODUCT_BOOT
    .entry : { KEEP(*(.text.entry)) } :image
    . = PM_KERNEL_BASE + PM_HANDOFF_OFFSET;
    .handoff : { KEEP(*(.product.handoff)) } :image
    .product_plan : { KEEP(*(.product.plan)) } :image
#endif
    .text : { KEEP(*(.text.entry)) *(.text .text.*) } :image
#if PM_ROOT_FRAME
    .root_record ALIGN(4) (READONLY) : {
        __root_record_start = .;
        LONG(0) LONG(0) LONG(0) LONG(0)
        __root_record_end = .;
    } :image
#else
    .rodata.boot_root : {
        __root_image_start = .;
        KEEP(*(.rodata.boot_root))
        __root_image_end = .;
    } :image
#endif
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.* .sdata .sdata.*) } :image
    /* OF-PPC binding:333-346: these describe firmware memory, not the
     * client image. Let firmware choose; B2 validates final available
     * against the exact image PT_LOAD extent before takeover. */
    .note.ofw ALIGN(4) : {
        LONG(PM_KERNEL_NOTE_NAMESZ) LONG(PM_KERNEL_NOTE_DESCSZ)
        LONG(PM_KERNEL_NOTE_TYPE)
        BYTE(0x50) BYTE(0x6f) BYTE(0x77) BYTE(0x65)
        BYTE(0x72) BYTE(0x50) BYTE(0x43) BYTE(0)
        LONG(0) LONG(-1) LONG(-1)
        LONG(-1) LONG(-1)
    } :image :ofw
    __kernel_file_end = .;
    .bss ALIGN(PM_KERNEL_PAGE_SIZE) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*) *(COMMON)
        . = ALIGN(PM_KERNEL_PAGE_SIZE);
        __ledger_fixed_start = .;
        . += PM_KERNEL_LEDGER_SIZE;
        __ledger_fixed_end = .;
        __bss_end = .;
    } :image
    /* Outside the zero range: entry can clear BSS on its live stack. */
    .kernel_stack ALIGN(PM_KERNEL_STACK_ALIGN) (NOLOAD) : {
        __kernel_stack_bottom = .;
        . += PM_KERNEL_STACK_SIZE;
        __kernel_stack_top = .;
        . = ALIGN(PM_KERNEL_PAGE_SIZE);
    } :image
    __kernel_resident_end = .;
    __kernel_chain_end = __kernel_resident_end + PM_KERNEL_HEAP_SIZE +
        PM_KERNEL_KAPI_SIZE + 2 * PM_KERNEL_GUARD_SIZE + PM_KERNEL_SHM_SIZE;
    ASSERT(__kernel_resident_end - __kernel_image_start <= PM_KERNEL_IMAGE_MAX,
           "kernel image exceeds MEM_KERNEL_IMAGE_MAX")
    ASSERT(__kernel_chain_end <= PM_KERNEL_CHAIN_LIMIT,
           "resident chain exceeds fixed paging guard")
    ASSERT(__ledger_fixed_end == __bss_end &&
           __ledger_fixed_start % PM_KERNEL_PAGE_SIZE == 0 &&
           __ledger_fixed_end - __ledger_fixed_start == PM_KERNEL_LEDGER_SIZE,
           "ledger must terminate BSS")
    ASSERT(__bss_end <= __kernel_stack_bottom &&
           __kernel_stack_top % PM_KERNEL_STACK_ALIGN == 0,
           "entry stack must be outside BSS and ABI aligned")
    __kernel_load_mem_end = __kernel_resident_end;
#if PM_ROOT_FRAME
    ASSERT(root_frame_size <= LAYOUT_MEM_BOOT_STAGING_SIZE, "root exceeds boot staging area")
    .root_frame PM_ROOT_FRAME_BASE (READONLY) : {
        __root_image_start = .;
        BYTE(0)
        . += root_frame_size - 1;
        __root_file_end = .;
        __root_load_end = .;
    } :root
#endif
    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /DISCARD/ : { *(.comment .eh_frame .note.GNU-stack .note.gnu.build-id) }
}
