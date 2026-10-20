ENTRY(kentry)

/* 値は include/memmap.h から生成した $(BUILD_OUT)/memmap_layout.h を CPP で読む。
   正典との一致は tools/gen_memmap.py --check が見る ([C4])。 */
KERNEL_LOAD_ADDR = LAYOUT_KERNEL_LOAD_ADDR;
MEM_KSTACK_TOP = LAYOUT_MEM_KSTACK_TOP;
MEM_KERNEL_IMAGE_MAX = LAYOUT_MEM_KERNEL_IMAGE_MAX;
MEM_DMA_POOL_BASE = LAYOUT_MEM_DMA_POOL_BASE;
MEM_FIXED_PAGING_BASE = LAYOUT_MEM_FIXED_PAGING_BASE;
MEM_FIXED_PD_BASE = LAYOUT_MEM_FIXED_PD_BASE;
MEM_FIXED_BOOT_PT_BASE = LAYOUT_MEM_FIXED_BOOT_PT_BASE;
MEM_FIXED_BOOT_PT_COUNT = LAYOUT_MEM_FIXED_BOOT_PT_COUNT;
MEM_FIXED_APERTURE_PT_BASE = LAYOUT_MEM_FIXED_APERTURE_PT_BASE;
MEM_FIXED_MMIO_TOP_PT_BASE = LAYOUT_MEM_FIXED_MMIO_TOP_PT_BASE;
MEM_FIXED_PAGING_END = LAYOUT_MEM_FIXED_PAGING_END;
MEM_SHELL_HEAP_BASE = LAYOUT_MEM_SHELL_HEAP_BASE;
MEM_SHELL_HEAP_END = LAYOUT_MEM_SHELL_HEAP_END;
MEM_SHELL_HEAP_SIZE = LAYOUT_MEM_SHELL_HEAP_SIZE;
MEM_SHELL_BAND_END = LAYOUT_MEM_SHELL_BAND_END;
MEM_POOL_BASE = LAYOUT_MEM_POOL_BASE;
MEM_SHELL_STACK_TOP = LAYOUT_MEM_SHELL_STACK_TOP;
MEM_SHELL_LOAD_ADDR = LAYOUT_MEM_SHELL_LOAD_ADDR;
MEM_KERNEL_BAND_END = LAYOUT_MEM_KERNEL_BAND_END;
MEM_LEDGER_META_END = LAYOUT_MEM_LEDGER_META_END;
MEM_STACK_GUARD = LAYOUT_MEM_STACK_GUARD;
MEM_DMA_POOL_END = LAYOUT_MEM_DMA_POOL_END;
MEM_FIXED_PAGING_USED_END = LAYOUT_MEM_FIXED_PAGING_USED_END;
MEM_FIXED_PAGING_FRAME_END = LAYOUT_MEM_FIXED_PAGING_FRAME_END;
MEM_LAYOUT_LOAD_END = LAYOUT_MEM_LAYOUT_LOAD_END;

SECTIONS
{
    /* カーネルは 0x100000 (1MB) にロードされる */
    . = KERNEL_LOAD_ADDR;

    .text : {
        /* kernel/kentry.o が確実に関数の先頭になるようにする */
        kernel/kentry.o(.text)
        *(.text)
        *(.text.*)
    }

    .data : {
        *(.data)
        *(.data.*)
        *(.rodata)
        *(.rodata.*)
    }

    .bss ALIGN(4) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
    }

    /* 台帳の FIXED 型 backing (2 頁)。core BSS の末尾、__bss_end まで。 */
    .ledger_fixed ALIGN(LAYOUT_MEM_PAGE_SIZE) (NOLOAD) : {
        __ledger_fixed_start = .;
        . += LAYOUT_MEM_LEDGER_META_SIZE;
        __ledger_fixed_end = .;
        __bss_end = .;
    }
    . = ALIGN(LAYOUT_MEM_PAGE_SIZE);

    /* SQLite image, heap and stack belong to its runtime owner. */
    __kernel_resident_end = .;
    ASSERT(__ledger_fixed_start % LAYOUT_MEM_PAGE_SIZE == 0 &&
           __ledger_fixed_end - __ledger_fixed_start == LAYOUT_MEM_LEDGER_META_SIZE &&
           __bss_start <= __ledger_fixed_start && __ledger_fixed_end == __bss_end,
           "ledger backing must be two pages inside core BSS")
    /* ==================================================================== */
    /*  カーネル本体の予算 (2026-09-17 決裁 D2、票                            */
    /*  docs/archive/kernel_v21/TASK_KSTACK_USER.md)                          */
    /*                                                                       */
    /*  KHEAP_BASE は __kernel_resident_end を 4KB に切り上げた位置で、そこから */
    /*  ヒープ → KAPI → SHM が芋づるで動く。SHM の後方ガードが固定 PD/PT 枠の  */
    /*  guard を越えると、**静かに**他人のページを壊す。                      */
    /*  それを **ここで、リンク時に** 止める (MEM_KERNEL_IMAGE_MAX と同値)。  */
    /* ==================================================================== */
    ASSERT(LAYOUT_MEM_SHM_RESV_START <= LAYOUT_MEM_FIXED_PAGING_GUARD_BASE,
           "resident chain exceeds SLACK ceiling")
    ASSERT(LAYOUT_MEM_FIXED_PAGING_GUARD_BASE + LAYOUT_MEM_GUARD_SIZE == MEM_FIXED_PAGING_BASE &&
           MEM_FIXED_PAGING_FRAME_END - MEM_FIXED_PAGING_BASE == 26 * LAYOUT_MEM_PAGE_SIZE &&
           MEM_FIXED_PAGING_FRAME_END - MEM_FIXED_PAGING_USED_END == 15 * LAYOUT_MEM_PAGE_SIZE &&
           MEM_FIXED_PAGING_FRAME_END == MEM_STACK_GUARD &&
           MEM_STACK_GUARD + LAYOUT_MEM_GUARD_SIZE == LAYOUT_MEM_KSTACK_BASE &&
           LAYOUT_MEM_KSTACK_BASE + LAYOUT_MEM_KSTACK_SIZE == MEM_SHELL_LOAD_ADDR,
           "fixed guard/PT/guard/stack must be 1/26/1/4 pages")
    ASSERT(MEM_FIXED_PD_BASE == MEM_FIXED_PAGING_BASE &&
           MEM_FIXED_BOOT_PT_BASE == MEM_FIXED_PD_BASE + LAYOUT_MEM_PAGE_SIZE &&
           MEM_FIXED_BOOT_PT_COUNT == 8 &&
           MEM_FIXED_APERTURE_PT_BASE == MEM_FIXED_BOOT_PT_BASE + 8 * LAYOUT_MEM_PAGE_SIZE &&
           MEM_FIXED_MMIO_TOP_PT_BASE == MEM_FIXED_APERTURE_PT_BASE + LAYOUT_MEM_PAGE_SIZE &&
           MEM_FIXED_PAGING_USED_END == MEM_FIXED_MMIO_TOP_PT_BASE + LAYOUT_MEM_PAGE_SIZE &&
           MEM_FIXED_PAGING_USED_END - MEM_FIXED_PAGING_BASE == 11 * LAYOUT_MEM_PAGE_SIZE,
           "fixed paging used extent must be 1/8/1/1 pages")
    ASSERT(MEM_SHELL_BAND_END + 1 == MEM_POOL_BASE &&
           MEM_SHELL_HEAP_END == LAYOUT_MEM_SHELL_GUARD &&
           MEM_SHELL_HEAP_SIZE == LAYOUT_MEM_SHELL_EXEC_HEAP_SIZE &&
           LAYOUT_MEM_SHELL_STACK_SIZE == 0xA000 &&
           MEM_SHELL_HEAP_BASE - LAYOUT_MEM_SHELL_IMAGE_HEADROOM ==
               MEM_SHELL_LOAD_ADDR + LAYOUT_MEM_SHELL_MAX_SIZE,
           "shell image/heap/guard/stack budget")
    ASSERT(MEM_DMA_POOL_BASE == 0x7F0000 && MEM_DMA_POOL_END + 1 == 0x800000 &&
           MEM_DMA_POOL_BASE % 0x10000 == 0 && LAYOUT_MEM_DMA_POOL_SIZE == 0x10000 &&
           MEM_DMA_POOL_BASE >= (LAYOUT_MEM_BOOT_BUNDLE_BASE + LAYOUT_MEM_BOOT_BUNDLE_SIZE) &&
           MEM_DMA_POOL_END < LAYOUT_MEM_SYSTEM_SPACE_BASE,
           "DMA window must be outside boot bundle and system space")
    ASSERT(((__bss_start | __bss_end) & 3) == 0 &&
           __bss_start <= __bss_end,
           "BSS zero ranges must be ordered and DWORD aligned")

    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /* GENERATED GENERATION REF BEGIN */
__os32_abi_1_mem_4_fmt_4 = 0;
/* GENERATED GENERATION REF END */

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.eh_frame)
        *(.comment)
    }
}
