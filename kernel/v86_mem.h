/* ======================================================================== */
/*  V86_MEM.H — V86 ゲスト用アドレス空間                                    */
/*                                                                          */
/*  ゲストには 16bit 実行環境らしい低位 1MB を見せる必要があるが、OS32 は    */
/*  そこを既に使っている (フォントキャッシュ 292KB / Unicode 表 128KB /      */
/*  GFX バックバッファ 128KB / カーネルスタック 64KB)。                     */
/*                                                                          */
/*  そこで pgalloc から連続物理メモリを別途確保し、セッション中だけ         */
/*  低位アドレスをそこへリマップする。OS32 のデータは物理的には無傷のまま   */
/*  で、teardown でアイデンティティマッピングに戻せば元通りになる。         */
/*                                                                          */
/*  VRAM (0xA0000-0xEFFFF) はリマップせず実物理をそのまま見せる。           */
/*  実測で Ys は GVRAM へ CPU で直接書き込んで描画しており、GDC/GRCG/EGC の  */
/*  コマンドポートを使っていなかった。VRAM を直マップすれば描画は           */
/*  メモリアクセスのままなので #GP が一切発生しない — V86 方式が            */
/*  386DX20 で成立する最大の理由 (docs/tasks/v86v2/03_ys_profile.md §3.3)。  */
/* ======================================================================== */

#ifndef __V86_MEM_H
#define __V86_MEM_H

#include "types.h"
#include "memmap.h"

/* リマップ範囲は 0x00000 から VRAM の直前 (0xA0000) まで = 640KB。
 *
 * **実行中のスタックを含む範囲は絶対にリマップしてはならない。**
 * V86 から #GP が入ると CPU は TSS.ESP0 が指すスタックにフレームを積む。
 * v86_entry.asm が ESP0 を「v86_enter を呼んだ時点の ESP」にしているので、
 * それはこのセッションを起動した v86 プログラムのスタック (0x400000 以降) で、
 * 低位には無い。カーネルスタックも 0x1FC000 に退避済み。
 *
 * かつてカーネルスタックが 0x90000-0x9FFFF に居た頃はここを 0x8F000 で
 * 止めるしかなく、ゲストに渡せる RAM は 572KB だった。PC-98 は 128KB 単位
 * でしか申告できないので、それは実質 512KB を意味していた。
 * → docs/tasks/v86v2/09_memmap.md */
#define V86_REMAP_START     0x00000UL
#define V86_REMAP_END       MEM_CONV_END             /* 0xA0000 (含まず) */

/* バッキング RAM のページ数。
 * ページ 0 (IVT+BDA) は**実物理のまま**ゲストに見せるのでバッキングを
 * 持たない (理由は v86_mem.c の setup 内コメント)。160 ページ確保して
 * 先頭 1 ページを使わず捨てていたのを 159 ページに詰めてある。 */
#define V86_BACKING_PAGES   ((V86_REMAP_END / PAGE_SIZE) - 1)   /* 159 ページ */

/* VRAM / ROM 帯 (リマップせず実物理を見せる) */
#define V86_VRAM_START      0xA0000UL
#define V86_VRAM_END        0xC0000UL   /* TVRAM/CG/GVRAM (含まず) */
#define V86_EXTROM_START    0xC0000UL
#define V86_EXTROM_END      0xE0000UL   /* 拡張 ROM (含まず) */
#define V86_GVRAM_E_START   0xE0000UL
#define V86_GVRAM_E_END     0xE8000UL   /* GVRAM プレーン E (含まず) */
#define V86_SOUNDROM_START  0xE8000UL
#define V86_SOUNDROM_END    0xF0000UL   /* サウンド BIOS ROM (含まず) */

/* ------------------------------------------------------------------------ */
/*  v86_guest_range_ok — ゲストのリニアアドレス範囲がバッキング RAM に      */
/*  収まっているか。                                                        */
/*                                                                          */
/*  seg:off は 16bit の掛け算なので理屈上 0x10FFEF (1MB+64KB 弱) まで       */
/*  指せるが、実際に張ってあるのは 0x00000-0x9FFFF だけ。範囲外を黙って     */
/*  読み書きするとカーネルを壊すので、ゲスト由来のポインタを解決する側は    */
/*  必ずこれを通すこと。                                                    */
/* ------------------------------------------------------------------------ */
static inline int v86_guest_range_ok(u32 linear, u32 len)
{
    if (len == 0) {
        return 0;
    }
    if (linear >= V86_REMAP_END) {
        return 0;
    }
    if (linear + len > V86_REMAP_END) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------------ */
/*  v86_guest_read_ok — 読み出し (命令フェッチ/診断) 用の範囲検証            */
/*                                                                          */
/*  ゲストは VRAM (0xA0000-) や BIOS ROM (0xC0000-0xFFFFF) のコードも       */
/*  実行する (実測: IPL は FD80:xxxx の ROM 内で I/O して #GP を出す)。      */
/*  したがって命令デコードの検証は 1MB 全域を許可する。                     */
/*  **書き込みの検証には使わないこと** — ゲストスタックへの書き込みは       */
/*  v86_guest_range_ok() (バッキング RAM 内) で検証する。                    */
/* ------------------------------------------------------------------------ */
#define V86_GUEST_MAP_END   0x100000UL

static inline int v86_guest_read_ok(u32 linear, u32 len)
{
    if (len == 0) {
        return 0;
    }
    if (linear >= V86_GUEST_MAP_END) {
        return 0;
    }
    if (linear + len > V86_GUEST_MAP_END) {
        return 0;
    }
    return 1;
}

/* ======== API ======== */

/* バッキング RAM を確保して低位アドレスをリマップする。
 * 0 で成功、負でエラー。 */
int  v86_mem_setup(void);

/* アイデンティティマッピングに戻し、バッキング RAM を解放する。 */
void v86_mem_teardown(void);

/* 確保済みバッキング RAM の物理先頭 (0 = 未確保)。デバッグ用。 */
u32  v86_mem_backing_phys(void);

#endif /* __V86_MEM_H */
