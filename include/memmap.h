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
/*  実値の地図 (絶対番地) の正典は **docs/02_memory.md §2-1** で、             */
/*  tools/gen_memmap.py がこのファイルの #define と build/out/kernel.map の    */
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
#define MEM_1MB               0x100000UL  /* 1MB */
#define MEM_GUARD_SIZE        0x1000UL    /* ガードページ 1 枚 (= PAGE_SIZE) */
#define KERNEL_LOAD_ADDR      0x100000UL  /* カーネルロードアドレス (1MB) */

/* ====================================================================== */
/*  カーネルヒープ (kmalloc)                                                */
/*  __bss_end から動的算出 (4KBアライン)                                    */
/* ====================================================================== */
extern u32 __bss_end;
#define KHEAP_BASE            ((((u32)&__bss_end) + 0xFFF) & ~0xFFFUL)
#define KHEAP_SIZE            0x030000UL  /* カーネルヒープサイズ (192KB。2026-09-23 に 320KB から。実測の使用は
                                                * FD 起動で 3.6KB、kmalloc_peak_bytes で根拠を取る。浮いた 128KB は
                                                * MEM_KERNEL_IMAGE_MAX へ。docs/tasks/memory/ の予算の節) */

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
/*  カーネルスタック (SQLite 帯域の末尾, 下向き成長)                         */
/*                                                                          */
/*  2026-09-17 (決裁 D1、票 docs/archive/kernel_v21/TASK_KSTACK_USER.md §4 の 4):  */
/*  **カーネル帯域の末尾 0x1FC000 から SQLite 帯域の末尾 0x2FC000 へ移した。**  */
/*  理由は「浮いた番地と固定番地を同じ帯で隣り合わせにしない」こと。         */
/*  カーネル帯域は KHEAP_BASE 以降が __bss_end 由来で浮くので、カーネルが    */
/*  育つと SHM が固定番地のスタックへ食い込む。実際 2026-09-17 には SHM の   */
/*  後方ガードがスタックの真ん中に居座り、**あと 224 バイト育つと            */
/*  実際に使っているスタックページが not-present になる**ところまで来ていた。 */
/*  移設先はカーネル予約域 (0x2DD000-0x2FFFFF, 140KB, もともと NP) の末尾で、 */
/*  ここは固定番地しか無いので浮動番地とぶつかりようがない。                 */
/*                                                                          */
/*  **低位に置いてはいけない。** V86 のゲストが見るリニアアドレスは CPU が   */
/*  (seg<<4)+off で作るので 0x00000-0x10FFEF に固定されている。カーネル      */
/*  スタックが低位のリニアを占有していると、その分だけゲストに渡せる         */
/*  コンベンショナルメモリが減る。しかも PC-98 は 128KB 単位でしか申告       */
/*  できないので、中途半端に空けても切り捨てで 512KB のままになる。          */
/*  0x00000-0x9FFFF を丸ごと空けて初めて 640KB を渡せる。                    */
/*                                                                          */
/*  サイズは実測にもとづく。ブート直後からシェル起動までの最深到達点は       */
/*  3,068 バイト (0x9F400) で、その後は誰もこのスタックを使わない            */
/*  (GDT はリング 0 のみ = 割り込みで特権遷移が起きない / exec_run() は      */
/*   子ごとに ESP を張り替える / V86 の #GP フレームは v86_enter が          */
/*   TSS.ESP0 を呼び出し元の ESP にするので v86 プログラムのスタックに載る)。 */
/*  16KB は実測の 5 倍強。**リング 3 のユーザプログラムを導入したら          */
/*  ここが全プログラムの ISR ネストを受けることになるので、その時は          */
/*  measure し直すこと。**                                                  */
/*  → docs/archive/v21/v86v2/09_memmap.md                                   */
/* ====================================================================== */
#define MEM_STACK_GUARD       0x2FB000UL
#define MEM_STACK_GUARD_END   0x2FBFFFUL
#define MEM_KSTACK_BASE       0x2FC000UL
#define MEM_KSTACK_TOP        0x2FFFFCUL

/* kernel/kentry.asm は ESP をここへ張り替える。ASM から C のマクロは引けない
 * ので、値は build/os32.ld が同じ名前の絶対シンボルとして持ち、kentry.asm は
 * それを extern で参照する。**二重定義の一致は
 * `python3 tools/gen_memmap.py --check` が機械的に照合する** ([C4])。 */

/* ブート時スタック (ローダー専用)。
 *
 * **ここは低位のままでなければならない。** ローダーは PM に入った後も
 * ディスク BIOS (INT 1Bh) を呼ぶためにリアルモードへ戻るので、SS:SP で
 * 届く 1MB 未満にスタックが要る。3 つのローダーの `mov esp, 0009FFFCh` が
 * この値。カーネルは kentry.asm で MEM_KSTACK_TOP に張り替えるため、
 * この領域はカーネルが動き出した時点で用済みになる
 * (V86 セッションはブートの遥か後なので競合しない)。 */
#define MEM_BOOT_STACK_TOP    0x9FFFCUL

/* BIOS ROM: Read-Only */
#define MEM_BIOS_ROM_START    0xF0000UL
#define MEM_BIOS_ROM_END      0xFFFFFUL

/* ====================================================================== */
/*  コンベンショナルメモリ再利用域 (ブート後に配置)                          */
/*  Step 04 で最終アドレスに更新される。暫定的にカーネル帯域後方にも配置。   */
/* ====================================================================== */
#define MEM_CONV_RECLAIM_START  0x01000UL
#define MEM_CONV_RECLAIM_END    0x9FFFFUL

/* コンベンショナルメモリの終端 (VRAM の直前)。
 * V86 ゲストに渡せるリニアの上限でもある。 */
#define MEM_CONV_END            0xA0000UL

/* NULLポインタ検出ガード */
#define MEM_NULL_GUARD_END      0x00FFFUL

/* フォントキャッシュ (コンベンショナル, ~292KB) */
#define MEM_FONT_CACHE_BASE     0x01000UL

/* ブート情報域 (ローダ → kernel_main、256B)。形式は include/bootinfo.h。
 * FD / HDD のローダが実モードで INT 1Bh AH=84h の結果をここへ書き、
 * kernel_main が**最初に** (フォント・ヒープより前に) 写す。
 * フォントキャッシュの内側なので、写した後は上書きされてよい。
 * 置き場の根拠 (票 TASK_HDD_INSTALL §1-v3 N1): ローダの実モードスタックは
 * 0x7C00 から下へ、ローダ本体は 0x8000〜、loader_hdd の受け渡しは
 * 0x7F00〜0x7F11、FAT のバッファは 0x6000〜0x77FF、圧縮イメージは 0x10000〜。
 * **NASM 側の写しは boot/bootinfo.inc** (一致は gen_memmap.py --check)。 */
#define MEM_BOOTINFO_BASE       0x07E00UL
#define MEM_BOOTINFO_SIZE       0x00100UL
#define MEM_BOOTINFO_END        0x07EFFUL

/* Unicode-JIS変換テーブル (コンベンショナル, 128KB) */
#define MEM_UNICODE_TABLE_BASE  0x4A000UL
#define MEM_UNICODE_TABLE_SIZE  0x20000UL  /* 128KB */

/* GFXバックバッファ (コンベンショナル, 128KB = 32000B × 4プレーン) */
#define MEM_GFX_BB_BASE         0x6A000UL
#define MEM_GFX_BB_SIZE         0x20000UL  /* 128KB (32000B×4プレーン, 0x6A000-0x89FFF) */

/* ====================================================================== */
/*  8bpp パックドバックバッファ (PEGC 256 色, GUI v1.1 H2)                  */
/*                                                                          */
/*  640×480×1B = 307,200B = ちょうど 75 ページ。9801 の 4 プレーン用         */
/*  128KB (MEM_GFX_BB_BASE) には収まらず、コンベンショナルの空き            */
/*  (0x8A000-0x9FFFF, 88KB) にも入らない。                                  */
/*                                                                          */
/*  **置き場所は物理メモリの末尾** — ホットデプロイ窓の直下を削って取る。    */
/*  0x400000-0x7FFFFF (アプリ帯 PDE 1) は PD ごとに差し替わるので共有面を    */
/*  置けず、それ以外の 0x500000〜mem_end は CPL=0 子プロセスの              */
/*  コード/ヒープ/スタックが sys_usable_mem_end() まで使い切る。よって      */
/*  「使ってよい上限」そのものを下げる sys_reserve_top() が唯一の安全策。    */
/*  予約は PEGC バックエンドが選ばれたときだけ行い、9801 では 0 バイト =     */
/*  従来と完全に同じレイアウトになる (H2 の 9801 回帰ゼロ条件)。            */
/* ====================================================================== */
#define MEM_GFX_BB8_WIDTH       640UL
#define MEM_GFX_BB8_HEIGHT      480UL
#define MEM_GFX_BB8_PITCH       MEM_GFX_BB8_WIDTH        /* 1B/px パックド */
#define MEM_GFX_BB8_SIZE        (MEM_GFX_BB8_PITCH * MEM_GFX_BB8_HEIGHT)
                                                         /* 307200B = 75 ページ */

/* KernelAPIテーブルアドレスは os32_kapi_shared.h で定義 (KAPI_ADDR) */

/* ====================================================================== */
/*  カーネル帯域内の動的配置 (0x100000-0x1FFFFF)                             */
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

/* GUI 予約 SHM (契約 T2 / docs/tasks/gui/API_CONTRACTS.md)。
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
#define MEM_KERNEL_BAND_END   0x1FFFFFUL

/* SHM後方予約域 (NOT PRESENT): SHM 後方ガードの後 〜 カーネル帯域の終端。
 * カーネルスタックは 2026-09-17 に SQLite 帯域の末尾へ出ていったので、
 * ここはカーネル帯域の終わりまで丸ごと予約になる。
 *
 * **カーネルが予算いっぱいまで育つと、この帯は空になる** (START == END + 1)。
 * 空は正しい状態で、逆転 (START > END + 1) は設計ミス。区別は
 * kernel/paging.c の STATIC_ASSERT (最悪配置) が固定し、
 * paging_init は空のときに範囲指定を呼ばない。 */
#define MEM_SHM_RESV_START    (MEM_SHM_GUARD_HI + MEM_GUARD_SIZE)
#define MEM_SHM_RESV_END      MEM_KERNEL_BAND_END

/* ====================================================================== */
/*  カーネル本体の予算と「最悪の配置」 (決裁 D2、2026-09-17)                */
/*                                                                          */
/*  KHEAP_BASE は __bss_end 由来で浮かせたまま。固定にしても使われない       */
/*  バイト数は同じで、変わるのは余りがどこに溜まるかと尽きたときの挙動だけ   */
/*  だから。代わりに **上限をリンク時に保証する**:                          */
/*                                                                          */
/*      build/os32.ld の                                                     */
/*        ASSERT(__bss_end <= KERNEL_LOAD_ADDR + MEM_KERNEL_IMAGE_MAX, ...)  */
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
 * ここに KHEAP_BASE (= (u32)&__bss_end 由来) を混ぜてはいけない。 */
#define MEM_KHEAP_BASE_MAX     (KERNEL_LOAD_ADDR + MEM_KERNEL_IMAGE_MAX)
#define MEM_SHM_GUARD_LO_MAX   (MEM_KHEAP_BASE_MAX + KHEAP_SIZE + MEM_KAPI_SIZE)
#define MEM_SHM_BASE_MAX       (MEM_SHM_GUARD_LO_MAX + MEM_GUARD_SIZE)
#define MEM_SHM_END_MAX        (MEM_SHM_BASE_MAX + MEM_SHM_SIZE - 1)
#define MEM_SHM_GUARD_HI_MAX   (MEM_SHM_BASE_MAX + MEM_SHM_SIZE)
#define MEM_SHM_RESV_START_MAX (MEM_SHM_GUARD_HI_MAX + MEM_GUARD_SIZE)

/* ====================================================================== */
/*  SQLite帯域 (0x200000-0x2FFFFF, 1MB)                                     */
/*  SQLite code+BSS + 代替スタック (128KB)                                  */
/* ====================================================================== */
extern u32 __sqlite_end;
#define MEM_SQLITE_STACK_BASE  ((((u32)&__sqlite_end) + 0xFFF) & ~0xFFFUL)
#define MEM_SQLITE_STACK_SIZE  0x020000UL  /* 128KB */
#define MEM_SQLITE_STACK_TOP   (MEM_SQLITE_STACK_BASE + MEM_SQLITE_STACK_SIZE - 16)

/* SQLite帯域後の予約: NOT PRESENT */
#define MEM_KERNEL_RESV_START  ((MEM_SQLITE_STACK_BASE + MEM_SQLITE_STACK_SIZE + 0xFFF) & ~0xFFFUL)
#define MEM_KERNEL_RESV_END    (MEM_STACK_GUARD - 1)  /* カーネルスタックガードの直前まで */

/* ---------------------------------------------------------------------- */
/*  DMA プール (票 docs/tasks/v3/TASK_HAL_WIRING.md §1-3、決裁 2026-09-23)  */
/*                                                                          */
/*  予約域の**中**に開ける 64KB の穴。上下は予約域のまま NP なので、        */
/*  はみ出しはそこで止まる。割り込みは「そのときの CR3 (アプリの PD)」で    */
/*  走るが、全 PD が共有するのは PDE 0 (0〜4MB) なのでここは全 PD で同じ    */
/*  写像になる (kernel/paging.h の契約 C2)。                               */
/*                                                                          */
/*  **純粋な定数式**にしてあるのが肝心 — 浮動番地 (__bss_end / __sqlite_end */
/*  由来) を混ぜると STATIC_ASSERT で固定できない。SQLite がここまで        */
/*  育たないことは build/os32.ld の ASSERT がリンク時に止める。             */
/*                                                                          */
/*  0x2E8000-0x2F7FFF (64KB)。**0x2F0000 の 64KB バンク境界をまたぐ**ので、 */
/*  dma_pool_alloc は候補ごとに dma_crosses_64k を見る ([HW2])。            */
/*  暫定 (ユーザー決裁 2026-09-23): v3 のメモリマップ見直しで再配置し得る。 */
/* ---------------------------------------------------------------------- */
#define MEM_DMA_POOL_BASE      0x2E8000UL
#define MEM_DMA_POOL_SIZE      0x010000UL   /* 64KB */
#define MEM_DMA_POOL_END       (MEM_DMA_POOL_BASE + MEM_DMA_POOL_SIZE - 1)

/* ====================================================================== */
/*  シェル常駐帯域 (0x300000-0x3FFFFF, 1MB)                                 */
/*  シェルはここに常駐し、子プロセスは一切触れない。PD切り替え不要。         */
/*                                                                          */
/*  レイアウト:                                                             */
/*    0x300000-           .text + .data + .bss (~466KB)                     */
/*    (〜0x372000)        BSS終端 → ここから newlib の sbrk ヒープ (malloc) */
/*    0x375000            ガードページ (Not-Present, 4KB) = sbrk 上限        */
/*    0x376000-0x37FFFF   スタック (40KB, ESP初期値=0x380000)               */
/*    0x380000-0x3FFFFF   exec_heap (KAPI mem_alloc/mem_free 用, 512KB)      */
/*                                                                          */
/*  sbrk ヒープと exec_heap は **必ず別領域** にすること。かつて両方が BSS   */
/*  終端から始まっていたため、newlib の stdio バッファと mem_alloc のブロック */
/*  ヘッダが互いを上書きし、`ls > file` の化け・`pipe: out of memory`・     */
/*  double free 警告として現れていた (2026-09-03 実測)。                     */
/* ====================================================================== */
#define MEM_SHELL_LOAD_ADDR   0x300000UL  /* シェルロードアドレス */
#define MEM_SHELL_MAX_SIZE    0x075000UL  /* シェルcode+bss最大 (468KB) */
#define MEM_SHELL_GUARD       0x375000UL  /* シェルスタックガード (= sbrk 上限) */
#define MEM_SHELL_STACK_TOP   0x380000UL  /* シェルスタック先頭 (下向き成長) */
#define MEM_SHELL_STACK_SIZE  0x00A000UL  /* シェルスタックサイズ (40KB) */
#define MEM_SHELL_HEAP_BASE   0x380000UL  /* シェル exec_heap 先頭 (mem_alloc) */
#define MEM_SHELL_HEAP_SIZE   0x080000UL  /* シェル exec_heap サイズ (512KB) */
#define MEM_SHELL_BAND_END    0x3FFFFFUL  /* シェル帯域終端 */

/* ====================================================================== */
/*  アプリ帯域 (先頭は APP_BAND_PDE = PDE 1, 0x400000-)                     */
/*                                                                          */
/*  ここだけが「PD ごと」の帯域 (kernel/paging.h の CONTRACTS C2)。          */
/*  先頭 1MB を共有ライブラリに、残りを外部プログラム本体・ヒープ・          */
/*  ユーザスタックに割り当てる。PDE 単位で切り替わるので、境界を PDE を      */
/*  またぐ位置へ動かしてはならない (paging.c の STATIC_ASSERT が検査する)。  */
/*                                                                          */
/*  2026-09-10 (票 docs/tasks/memory/APP_BAND_PDE.md): 1 枚 (4MB) 固定を     */
/*  やめ、**要求量に応じて 4MB 単位で増やせる**ようにした。                  */
/*                                                                          */
/*    MEM_APP_BAND_TOP      既定 (1 枚) の上端。ここまでは従来と同一で、     */
/*                          heap_size を明示しないプログラムは必ずこの形。   */
/*    MEM_APP_BAND_MAX_TOP  最大枚数まで伸ばしたときの上端 (exclusive)。     */
/*                                                                          */
/*  最大枚数の根拠 (票 §4-1): デバイス窓とぶつからない範囲。PEGC のリニア窓  */
/*  が MEM_APP_BAND_DEVICE_FLOOR (= include/pegc.h の PEGC_LINEAR_BASE) に   */
/*  あり、そこは PDE 3 (0xC00000-0xFFFFFF) の中なので、アプリ帯を伸ばせる    */
/*  のは PDE 1〜2 (0x400000-0xBFFFFF) まで。                                 */
/* ====================================================================== */
#define MEM_APP_BAND_BASE     0x400000UL  /* PDE 1 の先頭 */
#define MEM_APP_BAND_PDE_SIZE 0x400000UL  /* PDE 1 枚 = 4MB */
#define MEM_APP_BAND_MAX_PDES 2UL         /* 最大枚数 (PDE 1〜2) */
#define MEM_APP_BAND_TOP      (MEM_APP_BAND_BASE + MEM_APP_BAND_PDE_SIZE)
                                          /* 0x800000: 既定 (1 枚) の上端 */
#define MEM_APP_BAND_MAX_TOP  (MEM_APP_BAND_BASE + \
                               MEM_APP_BAND_MAX_PDES * MEM_APP_BAND_PDE_SIZE)
                                          /* 0xC00000: 最大まで伸ばした上端 */

/* アプリ帯を伸ばしてよい絶対の天井。9821 の PEGC リニア窓 (16MB システム
 * 空間の先頭) がここに出るので、帯がこれ以上へ伸びると窓を USER で踏む。
 * 値の正典は include/pegc.h の PEGC_LINEAR_BASE で、一致は
 * gfx/backend_pegc.c の STATIC_ASSERT が検査する ([C4] 三層定数)。 */
#define MEM_APP_BAND_DEVICE_FLOOR 0x00F00000UL

/* ====================================================================== */
/*  物理 RAM の地図 (K6-RAM, 2026-09-11)                                    */
/*                                                                          */
/*  OS 側に人為的な RAM 上限は持たない。上限は 32bit x86 のアーキテクチャ    */
/*  (PAE なし = 物理 4GB = PHYSMEM_MAX_PFN) だけで、そこから「RAM に         */
/*  ならない領域」を予約として引いた残りが実効の上限になる。                 */
/*                                                                          */
/*    [MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END)                         */
/*        PC-98 の 15〜16MB システム空間。9821 の PEGC リニア窓             */
/*        (include/pegc.h PEGC_LINEAR_BASE) がここに出るので、ローダの      */
/*        書き込みプローブが通っても RAM として配ってはならない。           */
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
/*        居る帯で、RAM にはならない (Win32 の実効 ≈3.2GB と同じ理由)。     */
/*                                                                          */
/*  RAM として登録してよい上端は MEM_PHYS_RAM_CEILING (= 窓の帯の先頭)。    */
/*  K6 の表の置き場で実効の上限は約 2.8GB なので、この帯で失う RAM は無い。 */
/* ====================================================================== */
#define MEM_SYSTEM_SPACE_BASE MEM_APP_BAND_DEVICE_FLOOR /* 0x00F00000 (15MB) */
#define MEM_SYSTEM_SPACE_END  0x01000000UL              /* 16MB */
#define MEM_HIGH_RAM_BASE     MEM_SYSTEM_SPACE_END      /* 16MB */
#define MEM_PHYS_MMIO_TOP     0xFF000000UL              /* 4GB - 16MB */
#define MEM_DEVICE_APERTURE_BASE 0xFE000000UL           /* 4GB - 32MB */
#define MEM_DEVICE_APERTURE_END  MEM_PHYS_MMIO_TOP      /* exclusive */
#define MEM_DEVICE_APERTURE_PDE_SIZE 0x00400000UL       /* PT 1 枚 = 4MB */
#define MEM_PHYS_RAM_CEILING  MEM_DEVICE_APERTURE_BASE

/* ====================================================================== */
/*  共有ライブラリ帯域 (0x400000-0x4FFFFF, 1MB)  — GUI v1.1 K3              */
/*                                                                          */
/*  固定アドレス常駐の位置依存ライブラリ (再配置なし、ロードアドレスは 1 つ)。 */
/*  カーネル (kernel/shlib.c) が /sys/lib/libos32gui.shlib を起動時にここへ  */
/*  読み、レイアウトは先頭ページの OS32ShlibHeader が決める:                 */
/*                                                                          */
/*    0x400000                          ジャンプ表 (OS32_SHLIB_HDR_SIZE=4KB) */
/*    +.. text_pages ページ             .text/.rodata — read-only + USER。   */
/*                                      master PD に張るので全 PD で共有。   */
/*    data_vaddr .. +data_pages ページ  .data/.bss — 同じ仮想番地に          */
/*                                      **アプリごとの物理ページ**を張る。    */
/*    帯域末尾 data_pages ページ         .data/.bss の原本 (複製元)。          */
/*                                      pgalloc の管理外に置くため帯域内。    */
/*                                                                          */
/*  帯域全体はロード成功時に pgalloc_mark_used() で予約する (子プロセスの     */
/*  claim は MEM_EXEC_LOAD_ADDR からなので、ここは別に押さえないと            */
/*  PD/PT や V86 バッキングに持っていかれる)。未ロードなら予約しない。        */
/* ====================================================================== */
#define MEM_SHLIB_BASE        MEM_APP_BAND_BASE   /* 0x400000 */
#define MEM_SHLIB_SIZE        0x100000UL          /* 1MB */
#define MEM_SHLIB_END         (MEM_SHLIB_BASE + MEM_SHLIB_SIZE)  /* 0x500000 */

/* ====================================================================== */
/*  外部プログラムロード関連 (子プロセス用: 0x500000〜)                      */
/*  シェル常駐帯域とは完全に分離。アイデンティティマッピング。               */
/*  スタック/ヒープは exec_run() にてシステムメモリ量から動的に計算される      */
/*                                                                          */
/*  2026-09-05 (K3): 共有ライブラリ帯域を下に挿し込んだので 0x400000 →       */
/*  0x500000 へ 1MB 上がった。sdk/link/app.ld と mkos32x の load_addr、      */
/*  および exec の旧バイナリ判定がこの値に追従する。                          */
/* ====================================================================== */
#define MEM_EXEC_LOAD_ADDR    MEM_SHLIB_END
/* 子プロセス帯のレイアウト (2026-09-04 に固定 1MB 上限を撤廃):
 *   [load .. code_end)            code + data + bss
 *   [code_end .. guard_a)         newlib sbrk (少なくとも MEM_EXEC_SBRK_MIN)
 *   [guard_a]                     ガードページ (非present)
 *   [exec_heap_base .. heap_top)  KAPI mem_alloc (exec_heap)。ヘッダ heap_size
 *                                 指定があればその大きさ、0 なら空きを折半
 *   heap_top = スタックガード直下 (CPL=3: RING3_HEAP_TOP / CPL=0: guard_b
 *              から動的確保リザーブを引いた位置)
 * 本体の上限は heap_top - MEM_EXEC_SBRK_MIN - ガード - MEM_EXEC_HEAP_MIN で決まる。 */
#define MEM_EXEC_SBRK_MIN     0x40000UL          /* sbrk に最低限残す 256KB */
#define MEM_EXEC_HEAP_MIN     0x10000UL          /* exec_heap の最小 64KB */
#define MEM_EXEC_STACK_SIZE   0x40000UL          /* スタックサイズ 256KB (-O0 SQLite対応) */

/* ====================================================================== */
/*  ホットデプロイ窓は撤去 (2026-09-09)                                    */
/*                                                                        */
/*  物理末尾の 256KB を予約してホストから 1 バイナリを流し込む仕組みは     */
/*  廃止した。窓 [末尾-256KB, 末尾) は CPL=3 スタック帯と同じ範囲で、      */
/*  8MB 構成ではアプリ帯と必ず衝突していた                                 */
/*  (docs/tasks/gui/DESIGN.md §9.3 の「9.6MB 構成以上が実質の下限」)。      */
/*  ユーザーランドの配送は HostDrv (Windows の C:\os32 → ゲストの /host)   */
/*  に一本化し、ゲスト側で /usr/bin へ写す。カーネルは NHD 配備のまま。    */
/*  経緯: docs/tasks/hotdeploy/DESIGN.md                                   */
/* ====================================================================== */

/* ====================================================================== */
/*  タイマー設定                                                            */
/* ====================================================================== */
#define PIT_HZ                100   /* タイマー割り込み周波数 (Hz) */

#endif /* MEMMAP_H */
