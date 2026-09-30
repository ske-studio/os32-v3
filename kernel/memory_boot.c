#include "memory_boot.h"
#include "pgalloc.h"
#include "memmap.h"
#include "pc98.h"

/* The loader samples one dword per 512KiB, including F00000/F80000.
 * Writable device memory can pass that test: it does NOT establish RAM in
 * [15,16)MiB. Restrict this legacy provider's admitted extent, not a blanket
 * MMIO hole policy. The unproven tail stays UNKNOWN for the device broker;
 * RAM at/above MEM_HIGH_RAM_BASE comes from memory_boot_detect instead.
 * Keep the raw sys_mem_kb report unchanged; sys freezes the safe tail. */
#define MEMORY_BOOT_LEGACY_END (15UL * MEM_1MB)
/* Spare workspace page kept for later device windows, on top of the identity
 * PTs that high RAM needs. Never a cap on how much RAM may be admitted. */
#define MEMORY_BOOT_WORKSPACE_PAGES 1UL
/* Probe pattern is derived from the address, so any aliasing (24-bit wrap or
 * a short RAM array repeating) shows up as a mismatch on the second pass. */
#define MEMORY_BOOT_PROBE_XOR 0xAA55AA55UL
/* 24bit (16MiB) アドレスラップの落ち先を作るマスク。 */
#define MEMORY_BOOT_WRAP_MASK 0x00FFFFFFUL

/* 絶対番地のポインタを GCC の「ほぼ NULL の参照」解析から隠す。
 * BIOS ワークエリア (0594h) も 24bit ラップの落ち先 (最小 0) も実在する
 * 物理番地で、paging_init より前に素の物理アドレスとして触る。
 * これを挟まないと -Wall の -Warray-bounds が誤検出する。 */
static void *boot_ptr(u32 addr)
{
    void *p = (void *)addr;
    __asm__ volatile ("" : "+r"(p));
    return p;
}

/* Small boot-owned model: never publish a caller's stack object. */
static struct physmem boot_memory;
/* End PFN (exclusive) of RAM that memory_boot_detect actually CONFIRMED above
 * MEM_HIGH_RAM_BASE; 0 until then. The loader's mem_kb is a hint and on its
 * own never promotes a single high page — only a verified attestation does. */
static u32 boot_high_end;
/* 実 RAM の合計 (KB)。memory_boot_init が最後に確定させる。0 = 未初期化。
 * 上端 (sys_mem_kb) との差が「RAM にならない番地」= 15-16MB システム空間。 */
static u32 boot_ram_kb;

/* ======================================================================== */
/*  memory_boot_detect — 16MB 超の物理 RAM を数える                          */
/*                                                                          */
/*  低位 (1MB〜15MB) はローダの 512KB 書き込みプローブが数えた mem_kb を     */
/*  そのまま使い、ここでは 16MB 以降だけを足す。                             */
/*                                                                          */
/*  **プローブを 16MB 超へ伸ばさない理由** (docs/hw + NP21/W のソース):      */
/*  F00000h〜FFFFFFh は PC-98 の「16MB システム空間」で、PEGC の 512KB       */
/*  リニア窓 (F00000-F7FFFF)、オープンバス (F80000-F9FFFF)、そして           */
/*  **A0000h〜FFFFFh のミラー** (テキスト/グラフィック VRAM と BIOS ROM,     */
/*  FA0000-FFFFFF) が並ぶ。書き込みプローブをここへ通すと VRAM を壊す。      */
/*  ローダは 512KB 刻みで 16MB 手前まで見るので、この帯を避けて数えるには    */
/*  結局ハードウェアの知識が要る。そして PC-98 にはその知識を持った正典が    */
/*  最初から BIOS ワークエリアとして置いてある:                              */
/*                                                                          */
/*    0401h (BYTE) = 100000h〜FFFFFFh の容量、128KB 単位 (最大 70h = 14MB)   */
/*    0594h (WORD) = 1000000h 以降の容量、MB 単位                            */
/*                                                                          */
/*  よって **0594h を採る**。ローダ (boot/loader_*.asm) には一切触らない。    */
/*  NP21/W は bios_reinitbyswitch() が毎リセットでこの 2 つを書き、ITF ROM   */
/*  は読むだけなので、エミュレータ上でも正典として使える。                    */
/*                                                                          */
/*  ただし申告値は鵜呑みにしない。1MB ごとに 1 ダブルワードを書いて          */
/*    (1) 24bit アドレスラップで低位を壊していないか                         */
/*    (2) 読み戻しが一致するか (= 実装されているか)                          */
/*  を見て、さらに 2 巡目で別名 (エイリアス) を弾く。合格した連続分だけを    */
/*  返すので、切り詰めは「無かった RAM」だけに起きる。                        */
/*                                                                          */
/*  paging_init より前、IF=0 で 1 回だけ呼ぶこと。                           */
/* ======================================================================== */
u32 memory_boot_detect(u32 mem_kb)
{
    u32 reported, accepted, k, addr, pattern, wrap, saved;
    volatile u32 *cell;
    volatile u32 *low;

    reported = *(volatile u16 *)boot_ptr(BIOS_WORK_MEM_HIGH_MB);
    /* 最上位の ROM / PCI MMIO 帯は RAM にならないので、そこまでで頭打ち。
     * これは人為的な上限ではなく、デバイスが居る番地の除外である。 */
    if (reported > (MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / MEM_1MB)
        reported = (MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / MEM_1MB;
    accepted = 0;
    for (k = 0; k < reported; k++) {
        addr = MEM_HIGH_RAM_BASE + k * MEM_1MB;
        pattern = addr ^ MEMORY_BOOT_PROBE_XOR;
        wrap = addr & MEMORY_BOOT_WRAP_MASK;
        cell = (volatile u32 *)boot_ptr(addr);
        low = (volatile u32 *)boot_ptr(wrap);
        saved = *low;
        *cell = pattern;
        if (*low != saved) {
            /* 24bit ラップ = この機体に 16MB 超は無い。壊した 1 語を戻す。 */
            *low = saved;
            break;
        }
        if (*cell != pattern) break;
        accepted++;
    }
    /* 2 巡目: 後の書き込みで前の番地が壊れていれば別名 (実装量の水増し)。 */
    for (k = 0; k < accepted; k++) {
        addr = MEM_HIGH_RAM_BASE + k * MEM_1MB;
        if (*(volatile u32 *)boot_ptr(addr) != (addr ^ MEMORY_BOOT_PROBE_XOR)) {
            accepted = k;
            break;
        }
    }
    if (!accepted) return mem_kb;
    boot_high_end = MEM_HIGH_RAM_BASE / PAGE_SIZE +
                    accepted * (MEM_1MB / PAGE_SIZE);
    return MEM_HIGH_RAM_BASE / 1024UL + accepted * (MEM_1MB / 1024UL);
}

/* workspace が要るページ数 = 恒等 PT (ブート窓 PAGING_BOOT_MAP_SIZE より上の
 * PDE 1 枚につき 1 枚) + デバイス窓ぶんの予備 1 枚。検出量に比例する。 */
static u32 memory_boot_workspace_pages(u32 limit)
{
    u32 pages;
    pages = MEMORY_BOOT_WORKSPACE_PAGES;
    if (limit > PAGING_BOOT_MAP_SIZE / PAGE_SIZE)
        pages += (limit + PTE_COUNT - 1) / PTE_COUNT - PAGING_BOOT_PT_COUNT;
    return pages;
}

/* 表 (pgalloc の bitmap 2 面 + workspace) が要るページ数の合計。 */
static u32 memory_boot_table_pages(u32 limit)
{
    u32 words, pages;
    words = (limit + 31) / 32;
    pages = (words * 2 * (u32)sizeof(u32) + PAGE_SIZE - 1) / PAGE_SIZE;
    return pages + memory_boot_workspace_pages(limit);
}

/* 表を置けるのは「アプリ帯の最大上端より上、15MB システム空間より下」の
 * 低位 RAM だけ (workspace >= MEM_APP_BAND_MAX_TOP は、2 枚 PDE のアプリが
 * master のページテーブルを USER で恒等マップして任意物理を書けてしまうのを
 * 防ぐ不変条件)。この帯の広さが、表で覆える RAM の量を決める。人為的な定数
 * ではなく置き場所から出てくる量なので、ここで高位 RAM の上端を丸める。
 * 現行のレイアウト (3MB の帯) で約 2.8GB まで覆える。 */
static u32 memory_boot_high_fit(u32 high_end, u32 top)
{
    u32 room, base;
    base = MEM_HIGH_RAM_BASE / PAGE_SIZE;
    if (top <= MEM_APP_BAND_MAX_TOP / PAGE_SIZE) return 0;
    room = top - MEM_APP_BAND_MAX_TOP / PAGE_SIZE;
    while (high_end > base && memory_boot_table_pages(high_end) > room)
        high_end -= PTE_COUNT;
    return high_end > base ? high_end : 0;
}

/* 検出した 16MB 超の RAM をモデルへ登録する。RAM にならない番地
 * (15-16MB のシステム空間、OS が割り当てるデバイス窓の帯、最上位の ROM /
 * PCI MMIO) は予約として明示的に抜く。窓の帯と最上位の帯は隣り合うので
 * [MEM_PHYS_RAM_CEILING, 4GB) を 1 回で MMIO にする。
 * 0 = モデル不変の失敗 (呼び出し側は fail-stop)。 */
static int memory_boot_add_high(u32 high_end)
{
    return physmem_exclude(&boot_memory, MEM_SYSTEM_SPACE_BASE / PAGE_SIZE,
                           MEM_HIGH_RAM_BASE / PAGE_SIZE, PHYSMEM_RESERVED) &&
           physmem_exclude(&boot_memory, MEM_PHYS_RAM_CEILING / PAGE_SIZE,
                           PHYSMEM_MAX_PFN, PHYSMEM_MMIO) &&
           physmem_add_trusted(&boot_memory, MEM_HIGH_RAM_BASE / PAGE_SIZE,
                               high_end, PHYSMEM_SOURCE_MACHINE);
}

/* 登録した span の合計 (KB)。低位は [0, admitted_kb)、高位は
 * [MEM_HIGH_RAM_BASE, high_end)。15-16MB のシステム空間はどちらの span にも
 * 入らないので、ここで自然に落ちる (引き算の特例は書かない)。
 * high_end が高位帯に届いていなければ高位は 0 ページ。 */
static u32 memory_boot_sum_kb(u32 admitted_kb, u32 high_end)
{
    u32 base;
    base = MEM_HIGH_RAM_BASE / PAGE_SIZE;
    if (high_end <= base) return admitted_kb;
    return admitted_kb + (high_end - base) * (PAGE_SIZE / 1024UL);
}

u32 memory_boot_ram_kb(void)
{
    return boot_ram_kb;
}

int memory_boot_init(u32 mem_kb)
{
    struct pgalloc_layout layout;
    u32 admitted_kb, top, pages, ws_pages, high_end;
    admitted_kb = mem_kb;
    if (admitted_kb > MEMORY_BOOT_LEGACY_END / 1024UL)
        admitted_kb = MEMORY_BOOT_LEGACY_END / 1024UL;
    physmem_bootstrap_legacy(&boot_memory, admitted_kb);
    top = physmem_legacy_end(&boot_memory);
    /* 16MB 超は別の供給源 (memory_boot_detect が検証した量) から来る。
     * legacy アリーナ (exec の連続帯) はこれに影響されない。 */
    high_end = boot_high_end > MEM_HIGH_RAM_BASE / PAGE_SIZE ?
               memory_boot_high_fit(boot_high_end, top) : 0;
    if (high_end && !memory_boot_add_high(high_end)) return 0;
    boot_ram_kb = memory_boot_sum_kb(admitted_kb, high_end);
    layout.capacity = pgalloc_metadata_bytes(&boot_memory);
    pages = layout.capacity / PAGE_SIZE;
    ws_pages = memory_boot_workspace_pages(high_end);
    /* PREINIT choice only. In particular 8MiB has no shared workspace
     * above APP_BAND_MAX_TOP; preserve its legacy allocator and exec limits.
     * The floor is the **maximum** app band top, not the default one: the
     * band now grows in 4MiB steps (docs/tasks/memory/APP_BAND_PDE.md), so a
     * workspace between 0x800000 and 0xBFFFFF could be identity-mapped USER
     * by a two-PDE app and let it rewrite shared page tables. */
    if (!pages || top < pages + ws_pages ||
        top - pages - ws_pages < MEM_APP_BAND_MAX_TOP / PAGE_SIZE) {
        /* legacy の池は PHYSMEM_LEGACY_MAX_PFN までしか見ないので、ここへ
         * 落ちた構成では高位 RAM は 1 ページも登録されない。報告も戻す。 */
        boot_ram_kb = memory_boot_sum_kb(admitted_kb, 0);
        pgalloc_init(mem_kb);
        return 1;
    }
    layout.metadata_first = top - pages;
    layout.metadata = (void *)(layout.metadata_first * PAGE_SIZE);
    layout.workspace_end = layout.metadata_first;
    layout.workspace_first = layout.workspace_end - ws_pages;
    /* These calls verify real PTEs before touching metadata/workspace/hot.
     * Once attempted, failure is fatal: never reinitialize as legacy. */
    if (!sys_memory_bootstrap_model(&boot_memory, &layout, paging_verify_identity))
        return 0;
    return sys_memory_stage_online();
}
