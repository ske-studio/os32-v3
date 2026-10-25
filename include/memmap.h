/* ======================================================================== */
/*  MEMMAP.H — OS32 システムメモリマップ定数                                 */
/*                                                                          */
/*  カーネル・ページング・プログラムローダーが参照する物理/仮想アドレスを     */
/*  一元管理する。変更時はpaging.c, kernel.c, exec.hとの整合性を確認。       */
/*                                                                          */
/*  **地図はここに書かない。** 同じ表を 2 か所に書いた時点で片方が必ず古くなる  */
/*  — 2026-09-17 まで、ここも docs/02_memory.md も CLAUDE.md も「カーネル本体   */
/*  ~220KB」と書いていた (実測 432KB)。相対表記が共有メモリとカーネルスタックの */
/*  重なりを隠していた (票 docs/archive/kernel_v21/TASK_KSTACK_USER.md)。             */
/*                                                                          */
/*  実値の地図 (絶対番地) は build/out/MEMMAP.md (コミットしない)。             */
/*  説明は docs/02_memory.md §2-1。tools/gen_memmap.py が定義と kernel.map の    */
/*  __bss_end から生成する。番地を動かしたら:                                 */
/*                                                                          */
/*      make kernel                          (kernel.map を作り直す)         */
/*      python3 tools/gen_memmap.py --write  (地図を書き直す)                */
/*      python3 tools/gen_memmap.py --check  (重なり・逆転を検査する)         */
/*                                                                          */
/*  下の #define が番地の正典であることは変わらない。                         */
/* ======================================================================== */

#ifndef MEMMAP_H
#define MEMMAP_H

#include "types.h"

/* ====================================================================== */
/*  カーネル配置                                                            */
/* ====================================================================== */
#define KIB 1024UL
#define MEM_PDE_SHIFT 22
#define MEM_PAGE_SIZE         0x1000UL    /* ページサイズ。paging.h の PAGE_SIZE と同値 */
#define MEM_1MB               0x100000UL  /* 1MB */
#define MEM_GUARD_SIZE        0x1000UL    /* ガードページ 1 枚 (= PAGE_SIZE) */
#define KERNEL_LOAD_ADDR      0x100000UL  /* カーネルロードアドレス (1MB) */

/* ====================================================================== */
/*  カーネルヒープ (kmalloc)                                                */
/*  __kernel_resident_end (ページ整列した core BSS の末尾) から動的算出 (4KB)    */
/* ====================================================================== */
extern u32 __kernel_resident_end;
extern u32 __ledger_fixed_start, __ledger_fixed_end;
#define KHEAP_BASE ((((u32)&__kernel_resident_end) + 0xFFF) & ~0xFFFUL)
#define KHEAP_SIZE 0x030000UL /* 192KiB */

/* ====================================================================== */
/*  ページング保護範囲                                                      */
/* ====================================================================== */

/* IVT/BIOSデータ領域: Read-Only (ブート後に再利用されるまで) */
/* ページ0 (0x0-0xFFF) はブート後に NOT PRESENT (NULLポインタ検出) */
#define MEM_IVT_PROT_START    0x01000UL
#define MEM_IVT_PROT_END      0x05FFFUL

/* loader.bin (使用済み): Read-Only → ブート後に再利用 
 * (注: ブートローダーが 0x804E に作成した GDT は
 *  カーネル初期化時 (gdt_init) に安全な上位メモリへ退避されます) */
#define MEM_LOADER_START      0x08000UL
#define MEM_LOADER_END        0x08FFFUL

/* ====================================================================== */
/*  カーネルスタック: 固定 PT 枠の後、guard 1頁 + stack 16KiB。             */
/*  低位は V86 のために維持する。ISR ネストの高水位は別途実測する。          */
/* ====================================================================== */
#define MEM_STACK_GUARD 0x3FB000UL
#define MEM_STACK_GUARD_END 0x3FBFFFUL
#define MEM_KSTACK_BASE 0x3FC000UL
#define MEM_KSTACK_TOP 0x3FFFFCUL

/* kernel/kentry.asm は ESP をここへ張り替える。ASM・ld は tools/gen_memmap.py
 * が生成する $(BUILD_OUT)/memmap_layout.h の LAYOUT_MEM_KSTACK_TOP を CPP で
 * 読む。直書きに戻していないことは `gen_memmap.py --check` が見る ([C4])。 */

/* ブート時スタック (ローダー専用)。
 *
 * **ここは低位のままでなければならない。** ローダーは PM に入った後も
 * ディスク BIOS (INT 1Bh) を呼ぶためにリアルモードへ戻るので、SS:SP で
 * 届く 1MB 未満にスタックが要る。3 つのローダーの `mov esp, 0009FFFCh` が
 * この値。カーネルは kentry.asm で MEM_KSTACK_TOP に張り替えるため、
 * この領域はカーネルが動き出した時点で用済みになる
 * (V86 セッションはブートの遥か後なので競合しない)。 */
#define MEM_BOOT_STACK_TOP    0x9FFFCUL

/* ローダ専用の低位番地。NASM の写しへの適用は N12。地図として数値を保つ。 */
#define MEM_BOOT_RM_STACK_TOP  0x07C00UL
#define MEM_BOOT_FAT_BUF       0x06000UL
#define MEM_BOOT_COMPRESSED_BASE 0x10000UL
#define MEM_BOOT_BIOS_PARAM_BASE 0x07F00UL
#define MEM_HDD_IPL_BASE       0x1FC00UL
#define MEM_BOOT_PROBE_END     0x1000000UL /* 現行 RAM probe の上限 (排他) */

/* 例外診断の旧来の stack trace 上限。メモリ帯を広げる変更は別件。 */
#define MEM_TRACE_STACK_LIMIT  0xF00000UL

/* BIOS ROM: Read-Only */
#define MEM_BIOS_ROM_START    0xF0000UL
#define MEM_BIOS_ROM_END      0xFFFFFUL

/* ====================================================================== */
/*  V86_LOW: [0, MEM_CONV_END) は単一 FIXED/kernel/WB、pool供給なし。       */
/*  page0は通常NP、残りはsupervisor/RW。セッション中だけ直接ゲストへ写像。 */
/* ====================================================================== */
#define MEM_CONV_RECLAIM_START  0x01000UL
#define MEM_CONV_RECLAIM_END    0x9FFFFUL

/* コンベンショナルメモリの終端 (VRAM の直前)。
 * V86 ゲストに渡せるリニアの上限でもある。 */
#define MEM_CONV_END            0xA0000UL

/* NULLポインタ検出ガード */
#define MEM_NULL_GUARD_END      0x00FFFUL

/* ブート情報域 (ローダ → kernel_main、256B)。形式は include/bootinfo.h。
 * FD / HDD のローダが実モードで INT 1Bh AH=84h の結果をここへ書き、
 * kernel_main が**最初に** (フォント・ヒープより前に) 写す。
 * V86_LOW の内側なので、写した後はV86が上書きしてよい。
 * 置き場の根拠 (票 TASK_HDD_INSTALL §1-v3 N1): ローダの実モードスタックは
 * 0x7C00 から下へ、ローダ本体は 0x8000〜、loader_hdd の受け渡しは
 * 0x7F00〜0x7F11、FAT のバッファは 0x6000〜0x77FF、圧縮イメージは 0x10000〜。
 * **NASM 側の写しは boot/bootinfo.inc** (一致は gen_memmap.py --check)。 */
#define MEM_BOOTINFO_BASE       0x07E00UL
#define MEM_BOOTINFO_SIZE       0x00100UL
#define MEM_BOOTINFO_END        0x07EFFUL

/* planar BB: boot時にpoolから連続32頁を確保し、既存SURFACEで管理する。 */
#define MEM_GFX_BB_SIZE         0x20000UL  /* 128KiB (32000B × 4プレーン) */

/* 自動プレイ mailbox: game/app の実装と game/tools/autoplay/driver.py の対。
 * 配置や大きさを変えるときは game 側も同じコミットで直す (CLAUDE.md)。 */
#define MEM_AUTOPLAY_MAILBOX_BASE 0x90000UL
#define MEM_AUTOPLAY_MAILBOX_SIZE 868UL

/* ====================================================================== */
/*  8bpp パックドバックバッファ (PEGC 256 色, GUI v1.1 H2)                  */
/*                                                                          */
/*  640×480×1B = 307,200B = ちょうど 75 ページ。planarの32頁と別に確保。    */
/*                                                                          */
/*  **置き場所は CPL=0 子のアリーナ (低位 RAM) の上端** — 起動時の ⑥       */
/*  (gfx_boot_reserve) が池からアリーナ内の上端を TOP_DOWN で連続確保し      */
/*  (owner = boot → gshell、台帳の SURFACE)、アリーナの上端を BB の下に凍結  */
/*  する (sys_usable_mem_end、TASK_T1_LEDGER §3-6)。CPL=0 子プロセスの      */
/*  コード/ヒープ/スタックは sys_usable_mem_end() まで使い切るので、上限の   */
/*  側を下げる (旧 sys_reserve_top の役、T1e で撤去)。確保は PEGC が候補に   */
/*  残ったときだけで、9801 では 0 バイト = 従来と同じレイアウト。           */
/* ====================================================================== */
#define MEM_GFX_BB8_WIDTH       640UL
#define MEM_GFX_BB8_HEIGHT      480UL
#define MEM_GFX_BB8_PITCH       MEM_GFX_BB8_WIDTH        /* 1B/px パックド */
#define MEM_GFX_BB8_SIZE        (MEM_GFX_BB8_PITCH * MEM_GFX_BB8_HEIGHT)
                                                         /* 307200B = 75 ページ */

/* KernelAPIテーブルアドレスは os32_kapi_shared.h で定義 (KAPI_ADDR) */

/* ====================================================================== */
/*  カーネル常駐鎖の動的配置 (固定 paging guard より下)                     */
/*  KAPI + SHM はヒープの直後に動的配置される                               */
/* ====================================================================== */

/* KAPIテーブル: KHEAP末尾直後 */
#define MEM_KAPI_OFFSET       (KHEAP_SIZE)  /* ヒープ末尾からのオフセット */
#define MEM_KAPI_SIZE         0x1000UL      /* 4KB */

/* 共有メモリ: KAPIテーブルの直後。
 * 2026-09-17 (決裁 D1): 256KB (16 ブロック) → **224KB (14 ブロック)**。
 * カーネル帯域 1MB が 17KB 超過していたので、いちばん大きい固定サイズの帯を
 * 削った。予算の余りは MEM_KERNEL_IMAGE_MAX を参照。 */
#define MEM_SHM_GUARD_LO      (KHEAP_BASE + KHEAP_SIZE + MEM_KAPI_SIZE)
#define MEM_SHM_BASE          (MEM_SHM_GUARD_LO + MEM_GUARD_SIZE)
#define MEM_SHM_SIZE          0x038000UL  /* 224KB = 16KB × 14 ブロック */
#define MEM_SHM_END           (MEM_SHM_BASE + MEM_SHM_SIZE - 1)
#define MEM_SHM_GUARD_HI      (MEM_SHM_BASE + MEM_SHM_SIZE)

/* DB 結果・エラー文用 SHM: 先頭 1 ブロックを固定予約する。
 * shm_alloc には配らず、末尾の GUI 予約を除いた 9 ブロックを IPC に使う。
 * SDK の DB_SHM_BLOCK_SIZE との一致は kernel/shm.c で表明する。 */
#define MEM_SHM_DB_OFFSET     0UL
#define MEM_SHM_DB_SIZE       0x4000UL                     /* 16KB = 1 ブロック */
#define MEM_SHM_DB_BASE       (MEM_SHM_BASE + MEM_SHM_DB_OFFSET)

/* GUI 予約 SHM (契約 T2 / docs/decisions/gui/API_CONTRACTS.md)。
 * SHM 帯の **末尾 4 ブロック** を GUI 用に固定予約する。kernel/shm.c の
 * 初期化でこの 4 ブロックを SHM_RESERVED にし、shm_alloc が配らないようにする。
 * 1 スロット (16KB) = アプリ 1 本、最大 4 アプリ (v1 はスロット 0 のみ)。
 * PTE は SHM 帯として既に RW+USER (v2 C2)。
 *
 * 2026-09-17 (決裁 D1): 「先頭 +192KB」の決め打ちをやめ、**末尾から数える**
 * 形にした。決め打ちだと SHM を縮めた瞬間に予約が帯の外へ出る (14 ブロックに
 * するとブロック 12〜15 のうち 14・15 が存在しなくなる)。
 * MEM_SHM_GUI_OFFSET は KHEAP_BASE を含まない **純粋な定数式** — だから
 * kernel/shm.c の STATIC_ASSERT がこれを検査できる (それ以前は
 * (u32)&__bss_end を含んでいて表明が黙って死んでいた)。
 * SDK 側の写しは sdk/include/os32/os32_gui_shared.h の GUI_SHM_OFFSET と
 * Rust の os32api::gui::proto::GUI_SHM_OFFSET。make check-gui-proto が照合する。 */
#define MEM_SHM_GUI_SIZE      0x10000UL                    /* 64KB = 4 ブロック */
#define MEM_SHM_GUI_OFFSET    (MEM_SHM_SIZE - MEM_SHM_GUI_SIZE)  /* 定数式 */
#define MEM_SHM_GUI_BASE      (MEM_SHM_BASE + MEM_SHM_GUI_OFFSET)
#define GUI_SLOT_SIZE         0x4000UL                     /* 16KB = 1 スロット */
#define GUI_SLOT_MAX          4                            /* スロット 0〜3 */

/* カーネル帯域終端 */
#define MEM_KERNEL_BAND_END 0x3DFFFFUL

/* SHM 後方の SLACK: 鎖の終端から固定 paging guard までの全頁を一度だけ
 * 一般池へ供給する。空 (START == END + 1) は許すが、逆転は拒否する。 */
#define MEM_SHM_RESV_START    (MEM_SHM_GUARD_HI + MEM_GUARD_SIZE)
#define MEM_SHM_RESV_END      MEM_KERNEL_BAND_END

/* ====================================================================== */
/*  カーネル本体の予算と「最悪の配置」 (決裁 D2、2026-09-17)                */
/*                                                                          */
/*  KHEAP_BASE は __kernel_resident_end 由来で浮かせたまま。固定にしても     */
/*  使われないバイト数は同じで、変わるのは余りがどこに溜まるかと尽きたとき   */
/*  の挙動だけだから。代わりに **上限をリンク時に保証する**:                */
/*                                                                          */
/*      build/os32.ld の                                                     */
/*        ASSERT(MEM_SHM_RESV_START <= MEM_FIXED_PAGING_GUARD_BASE, ...)     */
/*      (__kernel_resident_end <= KERNEL_LOAD_ADDR + MEM_KERNEL_IMAGE_MAX    */
/*       と同値)                                                             */
/*                                                                          */
/*  上限が保証されれば「予算いっぱいまで育った場合の配置」が **定数式** で   */
/*  書ける。重なりの検査はその最悪配置に対して行う — 実値 (KHEAP_BASE) は    */
/*  定数式ではないので STATIC_ASSERT では扱えない (条件が真でも              */
/*  "variably modified at file scope" で落ちる)。                            */
/*                                                                          */
/*  予算を超えたら: (1) カーネルを削る、(2) MEM_SHM_SIZE を 16KB 単位で      */
/*  減らす (kernel/shm.h の SHM_BLOCK_COUNT も同時)、(3) KHEAP_SIZE を       */
/*  減らす。どれも票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4 の 4 の続き。  */
/* ====================================================================== */
#define MEM_KERNEL_IMAGE_MAX  (MEM_KERNEL_BAND_END + 1 - KERNEL_LOAD_ADDR - \
                               KHEAP_SIZE - MEM_KAPI_SIZE - \
                               2UL * MEM_GUARD_SIZE - MEM_SHM_SIZE)

/* 予算いっぱいまで育った場合の配置。全部が定数式であることが肝心で、
 * ここに KHEAP_BASE (= (u32)&__kernel_resident_end 由来) を混ぜてはいけない。 */
#define MEM_KHEAP_BASE_MAX     (KERNEL_LOAD_ADDR + MEM_KERNEL_IMAGE_MAX)
#define MEM_SHM_GUARD_LO_MAX   (MEM_KHEAP_BASE_MAX + KHEAP_SIZE + MEM_KAPI_SIZE)
#define MEM_SHM_BASE_MAX       (MEM_SHM_GUARD_LO_MAX + MEM_GUARD_SIZE)
#define MEM_SHM_END_MAX        (MEM_SHM_BASE_MAX + MEM_SHM_SIZE - 1)
#define MEM_SHM_GUARD_HI_MAX   (MEM_SHM_BASE_MAX + MEM_SHM_SIZE)
#define MEM_SHM_RESV_START_MAX (MEM_SHM_GUARD_HI_MAX + MEM_GUARD_SIZE)

/* ====================================================================== */
/*  固定 paging / stack 枠と core 内 ledger backing                         */
/* ====================================================================== */
#define MEM_FIXED_PAGING_GUARD_BASE 0x3E0000UL
#define MEM_FIXED_PAGING_FRAME_END 0x3FB000UL
#define MEM_FIXED_PAGING_FRAME_PAGES 26UL
#define MEM_FIXED_PAGING_SPARE_PAGES 15UL
#define MEM_SHELL_IMAGE_HEADROOM 0x010000UL
#define MEM_SHELL_EXEC_HEAP_SIZE 0x040000UL
#define MEM_SHELL_HEAP_ALIGN 4UL
#define MEM_LAYOUT_LOAD_END MEM_FIXED_PAGING_GUARD_BASE
#define MEM_FIXED_PAGING_USED_END MEM_FIXED_PAGING_END
#define MEM_FIXED_PAGING_USED_PAGES 11UL
#define MEM_LEDGER_META_SIZE 0x2000UL
#define MEM_KSTACK_SIZE 0x4000UL


/* ---------------------------------------------------------------------- */
/*  DMA プール (票 docs/tasks/v3/TASK_HAL_WIRING.md §1-3、決裁 2026-09-23)  */
/*                                                                          */
/*  0x7F0000〜0x7FFFFF の 64KiB 整列池。RAM 全域の存在を確認して先行予約、 */
/*  不足時は DMA 装置だけ省略する。全 PD で supervisor/RW/WB を共有する。    */
/*  各割当は装置の limit と 64KiB 境界を検査する ([HW2])。                   */
/* ---------------------------------------------------------------------- */
#define MEM_DMA_POOL_BASE 0x7F0000UL
#define MEM_DMA_POOL_SIZE      0x010000UL   /* 64KB */
#define MEM_DMA_POOL_END       (MEM_DMA_POOL_BASE + MEM_DMA_POOL_SIZE - 1)

/* ---------------------------------------------------------------------- */
/*  台帳の backing (FIXED 型、票 docs/archive/v3/TASK_T1_LEDGER.md §3-3)      */
/*                                                                          */
/*  低位 RAM の末尾に metadata + workspace を置くと下端がアプリ帯の最大上端 */
/*  (MEM_APP_BAND_MAX_TOP) を割る構成 (8MB・9MB・12MB。高位 RAM は無い) は、 */
/*  ここ [BASE, END) に置く: metadata 1 ページ + workspace 1 ページ。       */
/*  core BSS 内の 2 頁を全構成で恒久保持し、present / supervisor / R/W。    */
/*  ARENA_TOP でも池に戻さない。END は排他 (半開区間)。                     */
/* ---------------------------------------------------------------------- */
#define MEM_LEDGER_META_BASE ((u32)&__ledger_fixed_start)
#define MEM_LEDGER_META_END ((u32)&__ledger_fixed_end)

/* ====================================================================== */
/*  シェル常駐帯域 (0x400000-0x4FFFFF, 1MiB)                                */
/*  image + sbrk は 0x4B5000 まで。画像末尾から 64KiB 以上を確保する。      */
/*  exec_heap: 0x4B5000-0x4F4FFF、guard: 0x4F5000、stack: 0x4F6000-。       */
/*  FONTPERF F3b (2026-10-20): sbrk の余裕 IMAGE_HEADROOM を 128→64KiB に   */
/*  下げ、像の上限 MAX_SIZE を 596→660KiB に広げた。sbrk の実測 peak は      */
/*  gshell 0 B (Rust は mem_alloc)・CUI shell 1,032 B。実行時の境界         */
/*  (HEAP_BASE = sbrk 上限・exec_heap) は変えないので配置の世代は据え置き。 */
/*                                                                          */
/*  sbrk ヒープと exec_heap は **必ず別領域** にすること。かつて両方が BSS   */
/*  終端から始まっていたため、newlib の stdio バッファと mem_alloc のブロック */
/*  ヘッダが互いを上書きし、`ls > file` の化け・`pipe: out of memory`・     */
/*  double free 警告として現れていた (2026-09-03 実測)。                     */
/* ====================================================================== */
#define MEM_SHELL_LOAD_ADDR 0x400000UL
#define MEM_SHELL_MAX_SIZE 0x0A5000UL
#define MEM_SHELL_GUARD 0x4F5000UL
#define MEM_SHELL_STACK_TOP 0x500000UL
#define MEM_SHELL_STACK_SIZE  0x00A000UL  /* シェルスタックサイズ (40KB) */
#define MEM_SHELL_HEAP_BASE 0x4B5000UL
#define MEM_SHELL_HEAP_END MEM_SHELL_GUARD
#define MEM_SHELL_HEAP_SIZE   (MEM_SHELL_HEAP_END - MEM_SHELL_HEAP_BASE) /* 256KiB */
/* T2a′: 画像外の固定 PD 1 + boot PT 8 + device PT 1 + 最上位 MMIO PT 1 (恒久予約)。 */
#define MEM_FIXED_PAGING_BASE 0x3E1000UL
#define MEM_FIXED_BOOT_PT_COUNT 8
#define MEM_FIXED_PD_BASE     MEM_FIXED_PAGING_BASE
#define MEM_FIXED_BOOT_PT_BASE (MEM_FIXED_PD_BASE + MEM_GUARD_SIZE)
#define MEM_FIXED_APERTURE_PT_BASE (MEM_FIXED_BOOT_PT_BASE + MEM_FIXED_BOOT_PT_COUNT * MEM_GUARD_SIZE)
#define MEM_FIXED_MMIO_TOP_PT_BASE (MEM_FIXED_APERTURE_PT_BASE + MEM_GUARD_SIZE)
#define MEM_FIXED_PAGING_END  (MEM_FIXED_MMIO_TOP_PT_BASE + MEM_GUARD_SIZE)
#define MEM_SHELL_BAND_END 0x4FFFFFUL

/* T2c: private virtual bands; physical supply has separate policy below. */
#define MEM_APP_BAND_BASE     0x80000000UL
#define MEM_APP_BAND_PDE_SIZE 0x400000UL
#define MEM_APP_BAND_MAX_PDES 64UL
#define MEM_APP_BAND_TOP      (MEM_APP_BAND_BASE + MEM_APP_BAND_PDE_SIZE)
#define MEM_APP_BAND_MAX_TOP  (MEM_APP_BAND_BASE + MEM_APP_BAND_MAX_PDES * MEM_APP_BAND_PDE_SIZE)
#define MEM_EXEC_HEAP_BASE    0x88000000UL
#define MEM_APP_STACK_TOP     MEM_APP_BAND_MAX_TOP
#define MEM_APP_STACK_MIN     0x4000UL
/* 物理 pool の起点と workspace。USER の VA 予算とは独立。 */
#define MEM_PHYS_EXEC_FLOOR   0x500000UL
#define MEM_PHYS_WORKSPACE_FLOOR 0xC00000UL
#define MEM_LEGACY_APP_BASE   MEM_POOL_BASE

/* ====================================================================== */
/*  物理 RAM の地図 (K6-RAM, 2026-09-11)                                    */
/*                                                                          */
/*  RAM 登録の上端は MEM_PHYS_RAM_CEILING=2GB (D11、v4 で見直す)。          */
/*  K6-RAM で旧 16MB 上限を撤廃。設計対象は物理 4GiB (PAE なし)。           */
/*  静的な恒等写像は 32MB まで、それより上は検出量だけ動的に張る。           */
/*                                                                          */
/*    [MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END)                         */
/*        PC-98 の 15〜16MB システム空間。9821 の PEGC リニア窓             */
/*        (include/pegc.h PEGC_LINEAR_BASE) がここに出るので、ローダの      */
/*        書き込みプローブが通っても RAM として配ってはならない。           */
/*        ただし 043Bh bit2=1 (切り離す) では通常 RAM、PEGC は別名          */
/*        FFF00000h のみ。OS32 はこの窓を使う (R7FIX2 段 3b)。             */
/*    [MEM_HIGH_RAM_BASE, ...)                                              */
/*        16MB 以上に載る実 RAM。検出量ぶんだけ pgalloc の池に入る。        */
/*    [MEM_DEVICE_APERTURE_BASE, MEM_DEVICE_APERTURE_END)                   */
/*        **OS が番地を決めるデバイス窓の帯** (v3 のメモリマップ、         */
/*        2026-09-29)。ボードの窓の番地をソフトウェアで選べるもの          */
/*        (NP21/W の Xe10 Cirrus のリニア窓 = 0FABh レジスタ 02h) を、      */
/*        RAM の量に関係なくここへ置く。RAM にはしない (物理地図で MMIO)。  */
/*        16MB 直上に置くと 16MB 超の RAM と奪い合い (実機 64MB・NP21/W   */
/*        17MB で衝突)、512MB 付近は PCI の BIOS が BAR を割り当てる帯     */
/*        (Ra266 の実測 0x20000000〜) なので、どちらも避けて 4GB の上位に   */
/*        置く。1 PDE = 4MB ごとに 1 枚の PT で覆い、paging_init が先頭の  */
/*        PT を静的に用意する (最初のアプリ AS より前に PDE が要るため)。   */
/*    [MEM_PHYS_MMIO_TOP, 4GB)                                              */
/*        32bit 空間の最上位。BIOS ROM ミラーと PCI 機の MMIO 窓が          */
/*        居る帯で、v3 では RAM を登録しない。最上位 4MB の PT を静的に持ち、 */
/*        PEGC の別名 FFF00000h〜FFF7FFFFh だけを必要時に恒等で張る。     */
/*                                                                          */
/*  RAM として登録してよい上端は MEM_PHYS_RAM_CEILING (2GB、D11)。          */
/*  それより上 [2GB, 4GB) は窓の帯も含めて RAM にしない。                   */
/* ====================================================================== */
#define MEM_SYSTEM_SPACE_BASE 0x00F00000UL /* 0x00F00000 (15MB) */
#define MEM_SYSTEM_SPACE_END  0x01000000UL              /* 16MB */
#define MEM_HIGH_RAM_BASE     MEM_SYSTEM_SPACE_END      /* 16MB */
#define MEM_PHYS_MMIO_TOP     0xFF000000UL              /* 4GB - 16MB */
#define MEM_MMIO_TOP_PT_BASE 0xFFC00000UL              /* 最上位 PT 1 枚 = 4MB */
#define MEM_DEVICE_APERTURE_BASE 0xFE000000UL           /* 4GB - 32MB */
#define MEM_DEVICE_APERTURE_END  MEM_PHYS_MMIO_TOP      /* exclusive */
#define MEM_DEVICE_APERTURE_PDE_SIZE 0x00400000UL       /* PT 1 枚 = 4MB */
/* RAM として登録してよい上端 (D11、MEMMAP_V3)。0594h の申告はここで
 * 頭打ちにし、[MEM_PHYS_RAM_CEILING, 4GB) は RAM にしない (PCI の BAR や
 * デバイス窓の帯が載る。v3 のアプリ帯 0x80000000〜 は仮想なので物理とは
 * 別物)。 */
#define MEM_PHYS_RAM_CEILING  0x80000000UL              /* 2GB */

/* ====================================================================== */
/*  物理ページの池 (pgalloc の管理範囲の下端)                               */
/*                                                                          */
/*  シェル帯の直後 0x500000。別供給の SLACK と重複させない。               */
/* ====================================================================== */
#define MEM_POOL_BASE 0x500000UL

/* ====================================================================== */
/*  ブート時の集積域・同梱域 (MEMMAP_V3 §4-5)                               */
/*                                                                          */
/*  集積域 = ローダが圧縮画像を読む場所 (展開が終われば池へ)。同梱域 =      */
/*  ブート必須モジュールの展開先 (台帳が owner=bundle で予約)。T1 の時点    */
/*  ではローダはどちらも使わない (定数だけ。ローダ側は T5b / T6b)。         */
/* ====================================================================== */
#define MEM_BOOT_STAGING_BASE 0x500000UL
#define MEM_BOOT_STAGING_SIZE 0x100000UL   /* 1MB */
#define MEM_BOOT_BUNDLE_BASE  0x600000UL
#define MEM_BOOT_BUNDLE_SIZE  0x100000UL   /* 1MB */

/* ====================================================================== */
/*  共有ライブラリ帯域 (仮想 0x80000000-, MEM_SHLIB_BASE, 1MB)              */
/*                                                                          */
/*  固定アドレス常駐の位置依存ライブラリ (再配置なし、ロードアドレスは 1 つ)。 */
/*  カーネル (kernel/shlib.c) が /sys/lib/libos32gui.shlib を起動時にここへ  */
/*  読み、レイアウトは先頭ページの OS32ShlibHeader が決める:                 */
/*                                                                          */
/*  T2c: 高位 VA に位置依存で置く。原本は池から非連続の実ページを確保。 */
/*  master の高位 APP PDE は空。各 AS に text RO と data 私有複製を張る。 */
/* ====================================================================== */
#define MEM_SHLIB_BASE        MEM_APP_BAND_BASE   /* 0x80000000 */
#define MEM_SHLIB_SIZE        0x100000UL          /* 1MB */
#define MEM_SHLIB_END         (MEM_SHLIB_BASE + MEM_SHLIB_SIZE)

/* 外部アプリ image/sbrk は高位、heap と可変 stack は別予約。 */
#define MEM_EXEC_LOAD_ADDR    MEM_SHLIB_END       /* 0x80100000 */
#define MEM_EXEC_HEAP_MIN     0x10000UL          /* exec_heap の最小 64KB */
#define MEM_EXEC_STACK_SIZE   0x40000UL          /* スタックサイズ 256KB (-O0 SQLite対応) */
/* boot pool が残す最小域: libc の初期追加は 1 page。
 * memmap.h は paging.h に先行するため、同値の正典 MEM_PAGE_SIZE を使う。 */
#define MEM_EXEC_BOOT_MIN (MEM_PHYS_EXEC_FLOOR + MEM_EXEC_STACK_SIZE + \
                           MEM_PAGE_SIZE + MEM_EXEC_HEAP_MIN)

/* ====================================================================== */
/*  ホットデプロイ窓は撤去 (2026-09-09)                                    */
/*                                                                        */
/*  物理末尾の 256KB を予約してホストから 1 バイナリを流し込む仕組みは     */
/*  廃止した。窓 [末尾-256KB, 末尾) は CPL=3 スタック帯と同じ範囲で、      */
/*  8MB 構成ではアプリ帯と必ず衝突していた                                 */
/*  (docs/decisions/gui/DESIGN.md §9.3 の「9.6MB 構成以上が実質の下限」)。      */
/*  ユーザーランドの配送は HostDrv (Windows の C:\os32 → ゲストの /host)   */
/*  に一本化し、ゲスト側で /usr/bin へ写す。カーネルは NHD 配備のまま。    */
/*  経緯: docs/tasks/hotdeploy/DESIGN.md                                   */
/* ====================================================================== */

/* ====================================================================== */
/*  タイマー設定                                                            */
/* ====================================================================== */
#define PIT_HZ                100   /* タイマー割り込み周波数 (Hz) */
#define TICK_MS (1000u / PIT_HZ)

/* ====================================================================== */
/*  物理 ⇔ 仮想の変換 (TASK_T1_LEDGER §3-4、D20)                            */
/*                                                                        */
/*  v3 の恒等の supervisor 領域 (カーネル帯・シェル帯・池・低位・MMIO の    */
/*  恒等窓) だけに使う。アプリ帯・lease 窓の番地は渡さない (T2 以降は       */
/*  as_va_to_pa / SURFACE から引く)。物理アクセスは [C5] と */
/*  check_p2v.py で検査する。                   */
/*  P2V_CONST / P2V_IO_CONST は静的初期化子・マクロ定義など定数式が要る     */
/*  場所だけ (B6)。関数の中では使わない。                                   */
/* ====================================================================== */
static inline void *P2V(u32 pa) { return (void *)(uptr)pa; }
static inline volatile void *P2V_IO(u32 pa)
{
    volatile void *p = (volatile void *)(uptr)pa;
    /* 低位物理へのアクセスを GCC の NULL 近傍解析から隠す。 */
    __asm__ volatile ("" : "+r"(p));
    return p;
}
/* paging_init 前の恒等アクセスの印。v3 では P2V_IO と同じ。 */
static inline volatile void *P2V_BOOT(u32 pa) { return P2V_IO(pa); }
static inline u32 V2P(const volatile void *va) { return (u32)(uptr)va; }
#define P2V_CONST(pa)    ((void *)(uptr)(pa))
#define P2V_IO_CONST(pa) ((volatile void *)(uptr)(pa))

/* T2b: private lease VA, independent of physical/device boundaries. */
#define MEM_LEASE_BASE        0xF0000000UL
#define MEM_LEASE_END         0xFE000000UL
#define MEM_LEASE_MAX_PDES    ((MEM_LEASE_END - MEM_LEASE_BASE) >> MEM_PDE_SHIFT)
#define MEM_LEASE_MAX         8

#endif /* MEMMAP_H */
