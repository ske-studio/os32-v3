ENTRY(kentry)

/* Values come from the selected memmap_layout.h via CPP. */
KERNEL_LOAD_ADDR = LAYOUT_KERNEL_LOAD_ADDR;
MEM_KSTACK_TOP = LAYOUT_MEM_KSTACK_TOP;
MEM_KERNEL_IMAGE_MAX = LAYOUT_MEM_KERNEL_IMAGE_MAX;
MEM_SQLITE_STACK_SIZE = LAYOUT_MEM_SQLITE_STACK_SIZE;
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
MEM_LAYOUT_V5 = LAYOUT_MEM_LAYOUT_V5;
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
        /* SQLite関連ファイルを除外して残りを配置 */
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .text)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .text.*)
    }

    .data : {
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .data)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .data.*)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .rodata)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .rodata.*)
    }

    .bss ALIGN(4) : {
        __bss_start = .;
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .bss)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .bss.*)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) COMMON)
        . = ALIGN(4);
#if !LAYOUT_MEM_LAYOUT_V5
        __bss_end = .;
#endif
    }

    /* ================================================================ */
    /*  SQLite 拡張域 (0x200000 - 0x2FFFFF, 1MB)                       */
    /* ================================================================ */
#if LAYOUT_MEM_LAYOUT_V5
    .ledger_fixed ALIGN(LAYOUT_MEM_PAGE_SIZE) (NOLOAD) : {
        __ledger_fixed_start = .;
        . += LAYOUT_MEM_LEDGER_META_SIZE;
        __ledger_fixed_end = .;
        __bss_end = .;
    }
    . = ALIGN(LAYOUT_MEM_PAGE_SIZE);
#else
    __ledger_fixed_start = LAYOUT_MEM_LEDGER_META_BASE;
    __ledger_fixed_end = LAYOUT_MEM_LEDGER_META_END;
    . = 0x200000;
#endif

    .sqlite_text : {
        __sqlite_start = .;
        KEEP(lib/sqlite3/sqlite3.o(.text .text.*))
        KEEP(lib/sqlite3/os32_sqlite_vfs.o(.text .text.*))
        KEEP(lib/sqlite3/os32_sqlite_test.o(.text .text.*))
    }

    .sqlite_rodata : {
        KEEP(lib/sqlite3/sqlite3.o(.rodata .rodata.*))
        KEEP(lib/sqlite3/os32_sqlite_vfs.o(.rodata .rodata.*))
        KEEP(lib/sqlite3/os32_sqlite_test.o(.rodata .rodata.*))
    }

    .sqlite_data : {
        KEEP(lib/sqlite3/sqlite3.o(.data .data.*))
        KEEP(lib/sqlite3/os32_sqlite_vfs.o(.data .data.*))
        KEEP(lib/sqlite3/os32_sqlite_test.o(.data .data.*))
        . = ALIGN(4);
        __sqlite_data_end = .;
    }

    .sqlite_bss (NOLOAD) : {
        lib/sqlite3/sqlite3.o(.bss .bss.* COMMON)
        lib/sqlite3/os32_sqlite_vfs.o(.bss .bss.* COMMON)
        lib/sqlite3/os32_sqlite_test.o(.bss .bss.* COMMON)
        . = ALIGN(4);
        __sqlite_end = .;
    }


    MEM_SQLITE_STACK_BASE = LAYOUT_MEM_SQLITE_STACK_BASE;
    MEM_SQLITE_STACK_TOP = LAYOUT_MEM_SQLITE_STACK_TOP;
    MEM_SQLITE_STACK_END = MEM_SQLITE_STACK_BASE + MEM_SQLITE_STACK_SIZE;
#if LAYOUT_MEM_LAYOUT_V5
    MEM_SQLITE_STACK_GUARD = LAYOUT_MEM_SQLITE_STACK_GUARD;
    __kernel_resident_end = MEM_SQLITE_STACK_END;
    ASSERT(__ledger_fixed_start % LAYOUT_MEM_PAGE_SIZE == 0 &&
           __ledger_fixed_end - __ledger_fixed_start == LAYOUT_MEM_LEDGER_META_SIZE &&
           __bss_start <= __ledger_fixed_start && __ledger_fixed_end == __bss_end,
           "ledger backing must be two pages inside core BSS")
    ASSERT(__bss_end <= __sqlite_start && __sqlite_end <= MEM_SQLITE_STACK_GUARD &&
           MEM_SQLITE_STACK_BASE == MEM_SQLITE_STACK_GUARD + LAYOUT_MEM_GUARD_SIZE,
           "SQLite chain overlaps BSS/guard")
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
           MEM_SHELL_HEAP_SIZE == 0x40000 &&
           LAYOUT_MEM_SHELL_STACK_SIZE == 0xA000 &&
           MEM_SHELL_HEAP_BASE - LAYOUT_MEM_SHELL_IMAGE_HEADROOM ==
               MEM_SHELL_LOAD_ADDR + LAYOUT_MEM_SHELL_MAX_SIZE,
           "shell image/heap/guard/stack budget")
    ASSERT(MEM_DMA_POOL_BASE == 0x7F0000 && MEM_DMA_POOL_END + 1 == 0x800000 &&
           MEM_DMA_POOL_BASE % 0x10000 == 0 && LAYOUT_MEM_DMA_POOL_SIZE == 0x10000 &&
           MEM_DMA_POOL_BASE >= (LAYOUT_MEM_BOOT_BUNDLE_BASE + LAYOUT_MEM_BOOT_BUNDLE_SIZE) &&
           MEM_DMA_POOL_END < LAYOUT_MEM_SYSTEM_SPACE_BASE,
           "DMA window must be outside boot bundle and system space")
#else
    __kernel_resident_end = ALIGN(__bss_end, LAYOUT_MEM_PAGE_SIZE);
#endif
    ASSERT(((__bss_start | __bss_end | __sqlite_data_end | __sqlite_end) & 3) == 0 &&
           __bss_start <= __bss_end && __sqlite_data_end <= __sqlite_end,
           "BSS zero ranges must be ordered and DWORD aligned")
    /* ==================================================================== */
    /*  カーネル本体の予算 (2026-09-17 決裁 D2、票                            */
    /*  docs/archive/kernel_v21/TASK_KSTACK_USER.md)                               */
    /*                                                                       */
    /*  KHEAP_BASE は __bss_end を 4KB に切り上げた位置で、そこから           */
    /*  ヒープ → KAPI → SHM が芋づるで動く。カーネルが予算を超えると SHM の   */
    /*  後方ガードがカーネル帯域を突き抜け、**静かに**他人のページを          */
    /*  not-present にする。2026-09-17 にはカーネルスタックの真ん中に         */
    /*  居座り、あと 224 バイトで三重フォルトするところまで来ていた。         */
    /*  それを **ここで、リンク時に** 止める。                                */
    /* ==================================================================== */
#if !LAYOUT_MEM_LAYOUT_V5
    ASSERT(__bss_end <= KERNEL_LOAD_ADDR + MEM_KERNEL_IMAGE_MAX,
        "kernel image exceeds MEM_KERNEL_IMAGE_MAX (include/memmap.h): the floating KHEAP_BASE/KAPI/SHM chain would run past the kernel band and silently unmap someone else's pages. Fix: shrink the kernel, or reduce MEM_SHM_SIZE in 16KB steps (keep SHM_BLOCK_COUNT in kernel/shm.h in sync), or reduce KHEAP_SIZE. Run 'python3 tools/gen_memmap.py --check' for the exact numbers; background is docs/archive/kernel_v21/TASK_KSTACK_USER.md")

    /* ==================================================================== */
    /*  SQLite の伸び代 (票 docs/tasks/v3/TASK_HAL_WIRING.md §1-3)            */
    /*                                                                       */
    /*  __sqlite_end の後ろには代替スタック 128KB が続き、その後ろの予約域    */
    /*  (NOT PRESENT) の中に DMA プールを 0x2E8000 固定で開ける。+0x1000 は   */
    /*  代替スタックの 4KB 切り上げぶん (MEM_SQLITE_STACK_BASE の式)。        */
    /*  ここが赤くなったら、プールを動かすか SQLite を削るかの判断が要る —    */
    /*  **黙って重ねてはいけない** (プールは割り込み文脈から DMA に渡す       */
    /*  メモリで、重なれば SQLite の .bss が装置に書き潰される)。             */
    /* ==================================================================== */
    ASSERT(__sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE,
        "SQLite band overruns DMA pool")

    ASSERT(((MEM_FIXED_PAGING_BASE | MEM_FIXED_PD_BASE | MEM_FIXED_BOOT_PT_BASE |
             MEM_FIXED_APERTURE_PT_BASE | MEM_FIXED_MMIO_TOP_PT_BASE | MEM_FIXED_PAGING_END | MEM_SHELL_HEAP_BASE |
             MEM_SHELL_HEAP_END | MEM_SHELL_STACK_TOP | (MEM_SHELL_BAND_END + 1)) & 0xFFF) == 0,
        "fixed paging/shell boundaries unaligned")
    ASSERT(MEM_FIXED_BOOT_PT_COUNT == 8 && MEM_FIXED_PD_BASE == MEM_FIXED_PAGING_BASE &&
           MEM_FIXED_BOOT_PT_BASE == MEM_FIXED_PD_BASE + 0x1000 &&
           MEM_FIXED_APERTURE_PT_BASE == MEM_FIXED_BOOT_PT_BASE + MEM_FIXED_BOOT_PT_COUNT * 0x1000 &&
           MEM_FIXED_MMIO_TOP_PT_BASE == MEM_FIXED_APERTURE_PT_BASE + 0x1000 &&
           MEM_FIXED_PAGING_END == MEM_FIXED_MMIO_TOP_PT_BASE + 0x1000 &&
           MEM_FIXED_PAGING_END - MEM_FIXED_PAGING_BASE == 11 * 0x1000,
        "fixed paging must be contiguous 1/8/1/1 pages")
    ASSERT(MEM_SHELL_STACK_TOP <= MEM_SHELL_HEAP_BASE &&
           MEM_SHELL_HEAP_BASE < MEM_SHELL_HEAP_END &&
           MEM_SHELL_HEAP_BASE + MEM_SHELL_HEAP_SIZE == MEM_SHELL_HEAP_END &&
           MEM_SHELL_HEAP_END == MEM_FIXED_PAGING_BASE,
        "shell heap overlaps fixed paging")
    ASSERT(MEM_FIXED_PAGING_END <= MEM_SHELL_BAND_END + 1 &&
           MEM_SHELL_BAND_END + 1 == MEM_POOL_BASE &&
           MEM_POOL_BASE - MEM_FIXED_PAGING_END == 4 * 0x1000,
        "fixed paging tail must reserve four pages")
    ASSERT(MEM_KERNEL_BAND_END < MEM_SHELL_LOAD_ADDR &&
           __sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE &&
           MEM_DMA_POOL_END < MEM_SHELL_LOAD_ADDR &&
           MEM_LEDGER_META_END <= MEM_STACK_GUARD && MEM_KSTACK_TOP < MEM_SHELL_LOAD_ADDR &&
           MEM_SHELL_LOAD_ADDR <= MEM_SHELL_HEAP_BASE,
        "fixed paging overlaps kernel/SQLite/DMA/metadata/stack")

#endif

    .os32_generations 0 (INFO) : { KEEP(*(.os32_generations)) }
    .os32_generation_ref 0 (INFO) : { KEEP(*(.os32_generation_ref)) }
    /* GENERATED GENERATION REF BEGIN */
__os32_abi_1_mem_3_fmt_4 = 0;
/* GENERATED GENERATION REF END */

    /DISCARD/ : {
        *(.note.gnu.build-id)
        *(.eh_frame)
        *(.comment)
    }
}
