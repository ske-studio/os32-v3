#include "platform_memory.h"
#include "paging.h"
#include "pc98.h"
#include "v86_bios.h"

/* Probe pattern is derived from the address, so any aliasing (24-bit wrap or
 * a short RAM array repeating) shows up as a mismatch on the second pass. */
#define MEMORY_BOOT_PROBE_XOR 0xAA55AA55UL
/* 24bit (16MiB) アドレスラップの落ち先を作るマスク。 */
#define MEMORY_BOOT_WRAP_MASK 0x00FFFFFFUL

/* ======================================================================== */
/*  platform_memory_probe_high — 16MB 超の物理 RAM を数える                          */
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
u32 platform_memory_probe_high(void)
{
    u32 reported, accepted, k, addr, pattern, wrap, saved;
    volatile u32 *cell;
    volatile u32 *low;

    const u8 *bios = v86_bios_real_lowmem();
    reported = bios[BIOS_WORK_MEM_HIGH_MB] |
               ((u32)bios[BIOS_WORK_MEM_HIGH_MB + 1] << 8);
    /* 最上位の ROM / PCI MMIO 帯は RAM にならないので、そこまでで頭打ち。
     * これは人為的な上限ではなく、デバイスが居る番地の除外である。 */
    if (reported > (platform_memory_geometry.ram_ceiling - platform_memory_geometry.high_first) / MEM_1MB)
        reported = (platform_memory_geometry.ram_ceiling - platform_memory_geometry.high_first) / MEM_1MB;
    accepted = 0;
    for (k = 0; k < reported; k++) {
        addr = platform_memory_geometry.high_first + k * MEM_1MB;
        pattern = addr ^ MEMORY_BOOT_PROBE_XOR;
        wrap = addr & MEMORY_BOOT_WRAP_MASK;
        cell = (volatile u32 *)P2V_BOOT(addr);
        low = (volatile u32 *)P2V_BOOT(wrap);
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
        addr = platform_memory_geometry.high_first + k * MEM_1MB;
        if (*(volatile u32 *)P2V_BOOT(addr) != (addr ^ MEMORY_BOOT_PROBE_XOR)) {
            accepted = k;
            break;
        }
    }
    return accepted ? platform_memory_geometry.high_first / PAGE_SIZE +
                      accepted * (MEM_1MB / PAGE_SIZE) : 0;
}

