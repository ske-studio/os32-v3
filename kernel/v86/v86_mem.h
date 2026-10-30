/* ======================================================================== */
/*  V86_MEM.H — V86 ゲスト用アドレス空間                                    */
/*                                                                          */
/*  低位640KiBは単一 FIXED/kernel/WB (V86_LOW)、一般poolへ供給しない。      */
/*  セッション中は実物理を直接使い、page0原本とmailboxを終了時に戻す。      */
/*  通常のpage0はNP、残りはsupervisor。BBはpool上の別SURFACE。              */
/*                                                                          */
/*  VRAM (0xA0000-0xEFFFF) はリマップせず実物理をそのまま見せる。           */
/*  実測で Ys は GVRAM へ CPU で直接書き込んで描画しており、GDC/GRCG/EGC の  */
/*  コマンドポートを使っていなかった。VRAM を直マップすれば描画は           */
/*  メモリアクセスのままなので #GP が一切発生しない — V86 方式が            */
/*  386DX20 で成立する最大の理由 (docs/archive/v21/v86v2/03_ys_profile.md §3.3)。  */
/* ======================================================================== */

#ifndef __V86_MEM_H
#define __V86_MEM_H

#include "pc98.h"
#include "types.h"
#include "ksetjmp.h"
#include "../../mm/paging.h"
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
 * → docs/archive/v21/v86v2/09_memmap.md */
#define V86_REMAP_START     0x00000UL
#define V86_REMAP_END       MEM_CONV_END             /* 0xA0000 (含まず) */

/* VRAM / ROM 帯 (リマップせず実物理を見せる) */
#define V86_VRAM_START      TVRAM_CHAR_BASE
#define V86_VRAM_END        GVRAM_BRG_END   /* TVRAM/CG/GVRAM (含まず) */
#define V86_EXTROM_START    0xC0000UL
#define V86_EXTROM_END      0xE0000UL   /* 拡張 ROM (含まず) */
#define V86_GVRAM_E_START   GVRAM_PLANE_I
#define V86_GVRAM_E_END     GVRAM_I_END   /* GVRAM プレーン E (含まず) */
#define V86_SOUNDROM_START  0xE8000UL
#define V86_SOUNDROM_END    MEM_BIOS_ROM_START   /* サウンド BIOS ROM (含まず) */

/* ------------------------------------------------------------------------ */
/*  v86_guest_range_ok — ゲストのリニアアドレス範囲が直接低位 RAM に      */
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
/*  v86_guest_range_ok() (直接低位 RAM 内) で検証する。                    */
/* ------------------------------------------------------------------------ */
#define V86_GUEST_MAP_END   MEM_1MB

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

/* One owner for mapping, real page zero and runtime unwind state. */
struct v86_session_state {
    int open;
    int closing;
    volatile int active;
    int running;
    int aborting;
    int release_pending;
    int real_saved; /* Boot capture latch: never clear on session reset. */
    u32 jmpbuf[KSETJMP_BUF_LEN];
    u32 saved_esp0, saved_eflags;
    u32 low_pte[V86_GUEST_MAP_END / PAGE_SIZE];
    u32 master_pde, active_pde, active_cr3;
    u32 restore_pte_bad, restore_pde_bad; /* Last restore diagnostic counts. */
    u8 real_lowmem[PAGE_SIZE]; /* Immutable core boot page0; T7a-1c. */
};
extern struct v86_session_state v86_session;
extern u32 v86_restore_mismatch;
int v86_session_begin(u32 owner);
void v86_session_end(void);
void v86_runtime_end(void);
int paging_v86_session_open(void);
int paging_v86_snapshot(struct v86_session_state *s);
int paging_v86_restore(struct v86_session_state *s);

/* ======== API ======== */

/* 低位を直接ゲストへ写像。ownerは既存callerとの入口互換のため保持し、
 * 予約ownerはkernelのまま。0で成功、負でエラー。 */
int  v86_mem_setup(u32 owner);

/* 保存地図へ復元。例外中の解放は trusted 着地の再呼出しへ送る。 */
void v86_mem_teardown(void);

#endif /* __V86_MEM_H */
