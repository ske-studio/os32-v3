ENTRY(kentry)

/* ======================================================================== */
/*  include/memmap.h からの写し (2026-09-17 決裁 D2)                         */
/*                                                                          */
/*  リンカスクリプトは C のヘッダを読めないので、ここだけ値を二重に持つ。    */
/*  **一致は `python3 tools/gen_memmap.py --check` が機械的に照合する**      */
/*  (make check の中)。片方だけ直すと赤になる ([C4])。                       */
/*                                                                          */
/*  MEM_KSTACK_TOP  kernel/kentry.asm が `extern` で参照する ESP の初期値。  */
/*                  ASM からは C のマクロを引けないので絶対シンボルで渡す。  */
/*  MEM_KERNEL_IMAGE_MAX  カーネル本体 (.text+.data+.bss) に使える最大。     */
/*                  カーネル帯域 1MB から、ヒープ 176KB・KAPI 4KB・          */
/*                  ガード 2 枚・SHM 224KB を引いた残り。                    */
/*  MEM_SQLITE_STACK_SIZE / MEM_DMA_POOL_BASE                                */
/*                  SQLite の伸び代を止めるための 2 つ (票 TASK_HAL_WIRING   */
/*                  §1-3)。DMA プールは予約域の中の**固定番地**なので、      */
/*                  SQLite が育ってプールを飲むとページングを張る前に        */
/*                  設計が壊れる。**リンク時に**止める。                     */
/* ======================================================================== */
KERNEL_LOAD_ADDR     = 0x100000;
MEM_KSTACK_TOP       = 0x2FFFFC;
MEM_KERNEL_IMAGE_MAX = 0x99000;

/*  名前が長いので列は揃えない。**上の 3 行の綴りは変えないこと** —
 *  tools/tests/test_memmap_gen.py が「写しがずれたら気づくか」を
 *  `MEM_KSTACK_TOP       = 0x2FFFFC;` の文字列置換で試す。 */
MEM_SQLITE_STACK_SIZE = 0x20000;
MEM_DMA_POOL_BASE = 0x2E8000;

/* T2a′: 絶対シンボル。画像に BSS/NOLOAD を追加しない。 */
MEM_FIXED_PAGING_BASE = 0x3F1000;
MEM_FIXED_PD_BASE = 0x3F1000;
MEM_FIXED_BOOT_PT_BASE = 0x3F2000;
MEM_FIXED_BOOT_PT_COUNT = 0x8;
MEM_FIXED_APERTURE_PT_BASE = 0x3FA000;
MEM_FIXED_PAGING_END = 0x3FB000;
MEM_SHELL_HEAP_BASE = 0x380000;
MEM_SHELL_HEAP_END = 0x3F1000;
MEM_SHELL_HEAP_SIZE = 0x71000;
MEM_SHELL_BAND_END = 0x3FFFFF;
MEM_POOL_BASE = 0x400000;
MEM_SHELL_STACK_TOP = 0x380000;
MEM_SHELL_LOAD_ADDR = 0x300000;
MEM_KERNEL_BAND_END = 0x1FFFFF;
MEM_LEDGER_META_END = 0x2FB000;
MEM_STACK_GUARD = 0x2FB000;
MEM_DMA_POOL_END = 0x2F7FFF;

SECTIONS
{
    /* カーネルは 0x100000 (1MB) にロードされる */
    . = 0x100000;

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

    .bss : {
        __bss_start = .;
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .bss)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) .bss.*)
        *(EXCLUDE_FILE(lib/sqlite3/sqlite3.o lib/sqlite3/os32_sqlite_vfs.o lib/sqlite3/os32_sqlite_test.o) COMMON)
        __bss_end = .;
    }

    /* ================================================================ */
    /*  SQLite 拡張域 (0x200000 - 0x2FFFFF, 1MB)                       */
    /* ================================================================ */
    . = 0x200000;

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
        __sqlite_data_end = .;
    }

    .sqlite_bss (NOLOAD) : {
        lib/sqlite3/sqlite3.o(.bss .bss.* COMMON)
        lib/sqlite3/os32_sqlite_vfs.o(.bss .bss.* COMMON)
        lib/sqlite3/os32_sqlite_test.o(.bss .bss.* COMMON)
        __sqlite_end = .;
    }

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
             MEM_FIXED_APERTURE_PT_BASE | MEM_FIXED_PAGING_END | MEM_SHELL_HEAP_BASE |
             MEM_SHELL_HEAP_END | MEM_SHELL_STACK_TOP | (MEM_SHELL_BAND_END + 1)) & 0xFFF) == 0,
        "fixed paging/shell boundaries unaligned")
    ASSERT(MEM_FIXED_BOOT_PT_COUNT == 8 && MEM_FIXED_PD_BASE == MEM_FIXED_PAGING_BASE &&
           MEM_FIXED_BOOT_PT_BASE == MEM_FIXED_PD_BASE + 0x1000 &&
           MEM_FIXED_APERTURE_PT_BASE == MEM_FIXED_BOOT_PT_BASE + MEM_FIXED_BOOT_PT_COUNT * 0x1000 &&
           MEM_FIXED_PAGING_END == MEM_FIXED_APERTURE_PT_BASE + 0x1000 &&
           MEM_FIXED_PAGING_END - MEM_FIXED_PAGING_BASE == 10 * 0x1000,
        "fixed paging must be contiguous 1/8/1 pages")
    ASSERT(MEM_SHELL_STACK_TOP <= MEM_SHELL_HEAP_BASE &&
           MEM_SHELL_HEAP_BASE < MEM_SHELL_HEAP_END &&
           MEM_SHELL_HEAP_BASE + MEM_SHELL_HEAP_SIZE == MEM_SHELL_HEAP_END &&
           MEM_SHELL_HEAP_END == MEM_FIXED_PAGING_BASE,
        "shell heap overlaps fixed paging")
    ASSERT(MEM_FIXED_PAGING_END <= MEM_SHELL_BAND_END + 1 &&
           MEM_SHELL_BAND_END + 1 == MEM_POOL_BASE &&
           MEM_POOL_BASE - MEM_FIXED_PAGING_END == 5 * 0x1000,
        "fixed paging tail must reserve five pages")
    ASSERT(MEM_KERNEL_BAND_END < MEM_SHELL_LOAD_ADDR &&
           __sqlite_end + MEM_SQLITE_STACK_SIZE + 0x1000 <= MEM_DMA_POOL_BASE &&
           MEM_DMA_POOL_END < MEM_SHELL_LOAD_ADDR &&
           MEM_LEDGER_META_END <= MEM_STACK_GUARD && MEM_KSTACK_TOP < MEM_SHELL_LOAD_ADDR &&
           MEM_SHELL_LOAD_ADDR <= MEM_SHELL_HEAP_BASE,
        "fixed paging overlaps kernel/SQLite/DMA/metadata/stack")

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
