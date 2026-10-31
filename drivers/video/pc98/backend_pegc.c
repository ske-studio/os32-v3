/* ======================================================================== */
/*  BACKEND_PEGC.C — PC-9821 PEGC 256 色バックエンド (GfxBackend, 票 H2)     */
/*                                                                          */
/*  640x480 / 8bpp パックドピクセル。描画は主記憶のバックバッファに対して    */
/*  行い、present_rect で F00000h のリニア窓へ矩形転送する。バンク窓         */
/*  (A8000h / B0000h) は使わない — 51 ライン (32768/640) ごとに MMIO で      */
/*  バンクを切り替える必要があり、present する矩形が途中で割れる             */
/*  (DESIGN §8)。                                                            */
/*                                                                          */
/*  [HW1] EGC / GRCG / GDC 描画コマンドは一切使わない。画素は CPU から       */
/*  リニア窓へ直接書くだけ。                                                 */
/*                                                                          */
/*  ポート・MMIO・BIOS ワークエリアの番地と出典は **すべて include/pegc.h**。 */
/*  このファイルには生の番号を書かない ([C4] / 票 H2 の鉄則)。               */
/*                                                                          */
/*  ページテーブルも台帳も触らない。リニア窓の予約・写像とバックバッファの  */
/*  確保は起動時の ⑥ (gfx_core.c gfx_boot_reserve、TASK_T1_LEDGER §3-8) が  */
/*  probe より前に済ませ、ここは台帳の SURFACE (PEGC の CLIENT) を受け取る。 */
/*                                                                          */
/*  票 H2c (NP21/W 実機で出た 3 点を修正。根拠は NP21/W のソース):            */
/*    1. パレットがほぼ黒 → KAPI の輝度は 0〜15、PEGC の 256 色パレット      */
/*       レジスタは 0〜255。set_palette で ×17 して伸ばす。                   */
/*       (io/gdc.c gdc_oaa/oac/oae, vram/palettes.c pal_make9821)            */
/*    2. 480 ラインにならない → 09A8h と 6Ah だけでは足りず、GDC の SYNC     */
/*       パラメータが表示ライン数を決める。両 GDC へ 480 ライン用の SYNC を   */
/*       流す。(vram/dispsync.c dispsync_renewalvertical,                    */
/*        bios/bios18.c gdcmastersync/gdcslavesync)                          */
/*    3. grph_disp=0 → グラフィック GDC へ START (0Dh) を出していないと      */
/*       gdcs.grphdisp の GDCSCRN_ENABLE が立たず、そもそも合成されない。     */
/*       (io/gdc.c gdc_work, vram/scrndraw.c scrndraw_draw)                  */
/* ======================================================================== */

#include "serial_diag.h"
#include "idt.h"
#include "memmap.h"
#include "../../../gfx/gfx_internal.h"   /* gfx.h (PAL_*_PORT), pc98.h, memmap.h, _out/_in */
#include "gfx_hal.h"
#include "pegc.h"
#include "np2sysp.h"
#include "paging.h"
#include "v86_bios.h"
#include "pgalloc.h"
#include "sys.h"
#include "kstring.h"
#include "kprintf.h"
#include "../../../gfx/palette.h"   /* palette_get_all / palette_shadow_set */
#include "os32_kapi_shared.h"
#include "cpu_calibrate.h"   /* cpu_delay_us (GDC の FIFO 待ち) */
#include "console.h"         /* console_hw_cursor_enable (CUI へ戻った後のカーソル) */
#include "con_sink.h"        /* con_sink_is_enabled (GUI 中はカーソルを出さない) */

/* アプリ帯 (mm/paging.c) がリニア窓を踏まないための照合 ([C4] 三層定数)。
 * memmap.h 側は pegc.h を読まない (9821 の文字を core に持ち込まない) ので、
 * 両方を読むここで値の一致を検査する。ここが落ちたら
 * MEM_SYSTEM_SPACE_BASE を PEGC_LINEAR_BASE に合わせ直すこと。 */
STATIC_ASSERT(MEM_SYSTEM_SPACE_BASE == PEGC_LINEAR_BASE,
              app_band_device_floor_is_pegc_window);
STATIC_ASSERT(PEGC_LINEAR_ALT_BASE >= MEM_MMIO_TOP_PT_BASE &&
              PEGC_LINEAR_ALT_BASE - MEM_MMIO_TOP_PT_BASE <=
              MEM_DEVICE_APERTURE_PDE_SIZE - PEGC_LINEAR_SIZE,
              pegc_alt_window_in_mmio_top_pt);

#define PEGC_DAC_BITS 8
#define PEGC_MODE_480 0
static const GfxMode pegc_modes[] = {
    {PEGC_WIDTH, PEGC_HEIGHT_480, GFX_FMT_PACKED8, PEGC_DAC_BITS, PEGC_MODE_480, 0, PEGC_PITCH}
};

/* ------------------------------------------------------------------------ */
/*  GDC 表示タイミング表 (値と出典は include/pegc.h §9)                      */
/* ------------------------------------------------------------------------ */
static const u8 s_msync_480[PEGC_GDC_SYNC_LEN]   = PEGC_GDC_MSYNC_480;
static const u8 s_ssync_480[PEGC_GDC_SYNC_LEN]   = PEGC_GDC_SSYNC_480;
static const u8 s_msync_400[PEGC_GDC_SYNC_LEN]   = PEGC_GDC_MSYNC_400;
static const u8 s_ssync_400_2m5[PEGC_GDC_SYNC_LEN] = PEGC_GDC_SSYNC_400_2M5;
static const u8 s_ssync_400_5m[PEGC_GDC_SYNC_LEN]  = PEGC_GDC_SSYNC_400_5M;
static const u8 s_scroll_480[PEGC_GDC_SCROLL_LEN] = PEGC_GDC_SCROLL_480;
static const u8 s_scroll_400_2m5[PEGC_GDC_SCROLL_LEN] = PEGC_GDC_SCROLL_400_2M5;
static const u8 s_scroll_400_5m[PEGC_GDC_SCROLL_LEN]  = PEGC_GDC_SCROLL_400_5M;
static const u8 s_msync_400_31k[PEGC_GDC_SYNC_LEN] = PEGC_GDC_MSYNC_400_31K;
static const u8 s_ssync_400_31k_2m5[PEGC_GDC_SYNC_LEN] = PEGC_GDC_SSYNC_400_31K_2M5;
static const u8 s_ssync_400_31k_5m[PEGC_GDC_SYNC_LEN]  = PEGC_GDC_SSYNC_400_31K_5M;
static const u8 s_tscroll[PEGC_GDC_SCROLL_LEN]    = PEGC_GDC_TSCROLL;
static const u8 s_tcsrform[PEGC_GDC_CSRFORM_LEN] = PEGC_GDC_TCSRFORM;
static const u8 s_gcsrform[PEGC_GDC_CSRFORM_LEN] = PEGC_GDC_GCSRFORM;
static const u8 s_crtc_val[PEGC_CRTC_COUNT]      = PEGC_CRTC_VALUES;
static const u16 s_crtc_port[PEGC_CRTC_COUNT] = {
    CRTC_CHRLINE, CRTC_BODYLINE, CRTC_CHARLINE,
    CRTC_SCROLL, CRTC_SCROLL_TOP, CRTC_SCROLL_LINES
};

/* ------------------------------------------------------------------------ */
/*  内部状態                                                                */
/* ------------------------------------------------------------------------ */
static u32 s_bb_phys   = 0;   /* バックバッファ先頭 (物理 = 仮想, 4KB 境界) */
static int s_probed    = 0;   /* probe を 1 回でも走らせたか */
static int s_probe_ok  = 0;   /* 480 ラインモードの利用可否 (キャッシュ) */
static int s_hw_ok     = 0;   /* PEGC 検出結果。prepare の利用取り下げ後も保持 */
static int s_active    = 0;   /* init 済み = 拡張グラフィックモード中か */
int gfx_diag_stripes;
int gfx_diag_altwin;
static u32 s_window_base = PEGC_LINEAR_BASE;

u32 pegc_window_base(void) { return s_window_base; }

/* Boot-only: no AS/lease exists yet. Keep the old base on either failure. */
int __attribute__((cold)) pegc_select_window(u32 base)
{
    if (base != s_window_base) {
        struct ledger_surface *sf = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_DISPLAY);
        if (!sf) return 0;
        struct ledger_surface next = *sf;
        next.first = base / PAGE_SIZE;
        if (!ledger_surface_release((u32)(sf - ledger_surfaces)) ||
            !ledger_surface_create(&next, 0)) return 0;
    }
    s_window_base = base;
    return 1;
}
static int s_sys16m_ram = 0;  /* 043Bh bit2 の読み値 (1 = 通常 RAM 扱い) */

/* 起動時 (BIOS が作ったまま) の水平走査周波数。pegc_boot_sync_record() が
 * 1 回だけ埋め、480 ラインから戻る pegc_shutdown() がこの値へ戻す。
 * -1 = まだ記録していない (probe が通る前)。 */
static int s_boot_recorded = 0;
static int s_boot_hsync    = -1;  /* PEGC_HSYNC_24KHZ / PEGC_HSYNC_31KHZ / -1 */
static int s_boot_clk1     = -1;  /* 起動時の GDC CLOCK-1 (1 = 5MHz) / -1 = 未記録 */
static int s_boot_clk2     = -1;  /* 起動時の GDC CLOCK-2 (1 = 5MHz) / -1 = 未記録 */

/* GDC の FIFO 待ちが上限で打ち切られた回数 (pegc.h §12)。**kernel.map 越しに
 * 読めるよう意図的にグローバル** — 実機で H3 (FIFO の取りこぼし) が起きて
 * いたかを後から確かめる手段。0 のままなら待ちは全部間に合っている。 */
u32 pegc_gdc_fifo_timeouts = 0;
/* VSYNC 待ち (pegc.h §13) が上限で打ち切られた辺の数。同じくグローバル。 */
u32 pegc_vsync_timeouts = 0;

/* ------------------------------------------------------------------------ */
/*  MMIO / BIOS ワークエリアアクセス                                         */
/*  E0000h の MMIO は恒等写像から volatile ポインタで直接読み書きする。    */
/*  BIOS 値は起動時の原本 real_lowmem の写しから読む。page0 は通常 NP。      */
/* ------------------------------------------------------------------------ */
static void mmio_w8(u32 addr, u8 val)
{
    *(volatile u8 *)P2V_IO(addr) = val;
    gfx_counters.io_accesses++;
}

static void mmio_w16(u32 addr, u16 val)
{
    *(volatile u16 *)P2V_IO(addr) = val;
    gfx_counters.io_accesses++;
}

/* BIOS flags come from the immutable boot page, in every address space. */
static u8 bios_flag(u32 addr)
{
    return v86_bios_real_lowmem()[addr];
}

/* モード F/F2 の読み戻し ([B] 表3-3): 番号を 09A0h に書いて同じ番号から読む。 */
static int pegc_stat(u8 sel)
{
    _out(PEGC_STAT_PORT, sel);
    gfx_counters.io_accesses += 2;
    return (_in(PEGC_STAT_PORT) & PEGC_STAT_BIT) ? 1 : 0;
}

/* 6Ah の解錠 → 値 → 施錠。20h/21h/68h/69h は解錠中しか効かない。 */
static void ff2_locked_write(u8 val)
{
    _out(MODE_FF2_PORT, PEGC_FF2_UNLOCK);
    _out(MODE_FF2_PORT, val);
    _out(MODE_FF2_PORT, PEGC_FF2_LOCK);
    gfx_counters.io_accesses += 3;
}

#include "backend_pegc_timing.inc"

/* ------------------------------------------------------------------------ */
/*  probe — 9821 の PEGC が使えるか (9801 では必ず 0)                        */
/*                                                                          */
/*  段取り (どれか 1 つでも落ちたら 0 を返し、H1 の 9801 実装に落ちる):      */
/*    1. 副作用のない識別 (pegc_identify、起動時の ⑥ も同じものを使う)。   */
/*       9801 はここで確実に落ちるので、以降のポート叩きは 9801 では一切    */
/*       走らない = 回帰ゼロ。                                              */
/*    2. ⑥ がリニア窓を予約・写像し、BB を池から確保して SURFACE (PEGC の   */
/*       CLIENT) に登録していること (pegc_reserve_backbuffer が受け取る)。   */
/*    3. 043Bh bit2 を読む (診断用)。PC-9801-61 型 SIMM 機ではこのポートは   */
/*       SIMM ソケットステータスなので、これだけでは決めない。書き込みもしない。*/
/*    4. 09A0h の解錠フラグが 6Ah の施錠/解錠に追随するか。open bus で常に   */
/*       FFh を返す機種 (As2 の設定 / Ts はポート自体が無い) を弾くために、   */
/*       「1 が返る」ではなく「0→1 が切り替わる」ことを見る。                */
/*    5. 拡張モードで両窓の双方向別名試験を行い、DISPLAY を選び直す。 */
/*       終わったら **必ず標準グラフィックモードへ戻す** (probe は副作用を    */
/*       残さない)。本番のモード設定は init() が行う。                       */
/* ------------------------------------------------------------------------ */
static int __attribute__((cold, noinline)) pegc_linear_selftest(void);
static int __attribute__((cold, noinline)) pegc_alias_test(u32 base, u8 v[4]);
static int pegc_reserve_backbuffer(void);

/* 窓は起動時の静的な恒等写像 (PAGING_BOOT_MAP_SIZE = 32MB) の中に入ること。 */
STATIC_ASSERT(PEGC_LINEAR_BASE + PEGC_LINEAR_SIZE <= PAGING_MAP_SIZE,
              pegc_window_in_boot_map);

/* 副作用のない識別 (TASK_T1_LEDGER §3-8)。BIOS ワークエリアの 2 バイトと
 * 物理地図を読むだけで、ポートも VRAM も触らない。起動時の ⑥
 * (gfx_core.c gfx_boot_reserve) が予約・写像・BB の前に呼び、probe も
 * 最初に呼ぶ。1 = PEGC の候補。
 *   - 機種判別 (BIOS ワークエリア, [US] memsys.md)。
 *   - 15-16MB のシステム空間に OS32 が RAM を登録しているなら、そこは通常
 *     RAM (043Bh bit2=1 の構成)。PEGC VRAM は F00000h には出ないし、張ったら
 *     自分の RAM を潰す。**RAM の上端 (sys_get_mem_kb) では決めない** —
 *     K6-RAM (2026-09-11) で上端の定義が「RAM の上端アドレス / 1024」になり、
 *     15MB 構成 (ExMemory 16) でも高位 RAM 16-17MB のぶん 17408 になる。
 *     見るべきは「窓に RAM が登録されているか」で、検出器は 15-16MB を RAM に
 *     しない (memory_boot.c の MEMORY_BOOT_LEGACY_END クランプ +
 *     PHYSMEM_RESERVED)。8MB 構成は窓まで RAM が届かないので通る。 */
int pegc_identify(void)
{
    if (!(bios_flag(PEGC_BIOS_ARCH_FLAG) & PEGC_BIOS_ARCH_EXTGFX)) return 0;
    if (!(bios_flag(PEGC_BIOS_MODE_FLAG) & PEGC_BIOS_MODE_EXTGFX)) return 0;
    return !pgalloc_range_has_ram(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE,
                                  MEM_HIGH_RAM_BASE / PAGE_SIZE);
}

static int __attribute__((cold)) pegc_probe(void);
static int pegc_probe(void)
{
    int unlocked, locked;

    if (s_probed) return s_probe_ok;
    s_probed = 1;
    s_probe_ok = 0;

    /* --- 1〜2. 副作用のない識別 (pegc_identify) と、⑥ が窓を予約・写像して
     * BB を確保済みであること (SURFACE)。probe は自分で予約も写像もしない。 */
    if (!pegc_identify() || !pegc_reserve_backbuffer()) return 0;

    /* --- 3. 16MB 空間の設定 (診断用。判定は 5 の実測で行う) --- */
    s_sys16m_ram = (_in(PEGC_SYS16M_PORT) & PEGC_SYS16M_NORMAL_RAM) ? 1 : 0;
    gfx_counters.io_accesses++;

    /* --- 4. 09A0h が 6Ah に追随するか (open bus 除け) --- */
    _out(MODE_FF2_PORT, PEGC_FF2_UNLOCK);
    unlocked = pegc_stat(PEGC_STAT_SEL_UNLOCK);
    _out(MODE_FF2_PORT, PEGC_FF2_LOCK);
    locked = pegc_stat(PEGC_STAT_SEL_UNLOCK);
    gfx_counters.io_accesses += 2;
    if (!(unlocked == 1 && locked == 0)) return 0;

    /* --- 5. リニア窓の書き込み読み戻し --- */
    s_hw_ok = s_probe_ok = pegc_linear_selftest();
    return s_probe_ok;
}

/* 拡張モードへ一時的に入り、リニア窓で読み書きできるか確かめる。
 * 戻り値 1 = 使える。終了時はハードを呼び出し前の状態に戻す。窓の写像は ⑥ が
 * 張ったもの (supervisor + PCD) で、失敗しても剥がさない (予約・写像は永久
 * 保持、TASK_T1_LEDGER §3-8)。 */
static int __attribute__((cold, noinline)) pegc_linear_selftest(void)
{
    int ok = 0, which = s_sys16m_ram || gfx_diag_altwin;
    const char *result[2] = {"skip", "skip"};
    const char *win = "none", *disp = "skip";
    u8 v[4];

    ff2_locked_write(PEGC_FF2_EXT_GFX);
    if (!pegc_stat(PEGC_STAT_SEL_GFXMODE)) goto restore_mode;
    mmio_w8(PEGC_MMIO_PIXFMT, PEGC_PIXFMT_PACKED);
    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_ON);
    for (int i = 0; i < 2; i++, which ^= 1) {
        u32 base = which ? PEGC_LINEAR_ALT_BASE : PEGC_LINEAR_BASE;
        ok = pegc_alias_test(base, v);
        result[which] = ok ? "ok" : "ng";
        if (!ok) continue;
        win = which ? "fff00000" : "00f00000";
        ok = pegc_select_window(base);
        if (which) disp = ok ? "ok" : "fail";
        break;
    }
    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_OFF);
restore_mode:
    ff2_locked_write(PEGC_FF2_STD_GFX);
    kprintf(KLOG_ATTR, "[pegc] win=%s sys16m=%d f00=%s fff=%s force=%d disp=%s\n",
            win, s_sys16m_ram, result[0], result[1], gfx_diag_altwin, disp);
    return ok;
}

/* ------------------------------------------------------------------------ */
/*  パレット初期化                                                          */
/*                                                                          */
/*  0〜15  : システム色。9801 の 16 色パレットと同じ色相を 4bit → 8bit へ    */
/*           伸ばす (x * 17 で 0-15 → 0-255)。既存アプリの色番号がそのまま   */
/*           同じ見た目になる = gdi_test が無変更で同じ絵を出す条件。        */
/*  16〜231: 6×6×6 の色立方 (216 色)。                                       */
/*  232〜255: 24 段のグレースケール。                                        */
/*  16 以降が契約 G8 のリース範囲 (lease_first=16, lease_count=240)。         */
/*                                                                          */
/*  ⚠ **輝度のスケールに 2 つの世界がある** (票 H2c の障害):                  */
/*    - ハードウェア (AAh/ACh/AEh) は 256 色モードでは **0〜255**。           */
/*      NP21/W は書いた値をそのまま np2_pal32 の RGB 成分にする               */
/*      (io/gdc.c gdc_oaa/oac/oae → vram/palettes.c pal_make9821)。          */
/*    - KAPI (gfx_set_palette / gfx_lease_palette) は **0〜15**。             */
/*      契約 G6 の GUI_SYSTEM_PALETTE も 0〜15 で、gshell が起動時に          */
/*      gfx_set_palette で 16 色ぶん流し込む (userland/gshell wm.rs)。        */
/*  H2 では KAPI 値を素通ししていたため、gshell がシステム色を入れた瞬間に     */
/*  0〜15 が 8bit レジスタへ書かれ、白 (15,15,15) が輝度 15/255 ≒ ほぼ黒に    */
/*  なっていた (実測: 画面全体が暗く、文字がかろうじて見える)。               */
/*  → KAPI から来る値は必ず pegc_pal8() で 8bit へ伸ばす。                    */
/* ------------------------------------------------------------------------ */
static void pegc_write_palette(int idx, u8 r, u8 g, u8 b)
{
    _out(PAL_IDX_PORT, idx & PEGC_PALETTE_MAX);
    _out(PAL_G_PORT, g);
    _out(PAL_R_PORT, r);
    _out(PAL_B_PORT, b);
    gfx_counters.io_accesses += 4;
}

/* KAPI の 4bit 輝度 (0〜15) → PEGC の 8bit 輝度 (0〜255)。15*17 = 255。 */
static u8 pegc_pal8(u8 v)
{
    return (u8)((v & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE);
}

#define PEGC_GRAY_FIRST 8
#define PEGC_GRAY_STEP 10
#define COPY_WORD_SHIFT 2

static void pegc_palette_init(void)
{
    static const u8 cube[GFX_PALETTE_CUBE_LEVELS] = { 0, 51, 102, 153, 204, 255 };
    const PaletteEntry *sys = palette_get_all();
    int i, r, g, b;

    /* 0-15: 9801 の 16 色をそのまま (4bit 輝度 → 8bit) */
    for (i = 0; i < GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); i++) {
        pegc_write_palette(i, pegc_pal8(sys[i].r), pegc_pal8(sys[i].g),
                           pegc_pal8(sys[i].b));
    }

    /* 16-231: 6x6x6 色立方 */
    i = PEGC_LEASE_FIRST;
    for (r = 0; r < GFX_PALETTE_CUBE_LEVELS; r++) {
        for (g = 0; g < GFX_PALETTE_CUBE_LEVELS; g++) {
            for (b = 0; b < GFX_PALETTE_CUBE_LEVELS; b++) {
                pegc_write_palette(i++, cube[r], cube[g], cube[b]);
            }
        }
    }

    /* 232-255: グレースケール 24 段 */
    for (b = 0; i < GFX_PAL_ENTRIES(GFX_FMT_PACKED8_BPP); i++, b++) {
        u8 v = (u8)(PEGC_GRAY_FIRST + b * PEGC_GRAY_STEP);
        pegc_write_palette(i, v, v, v);
    }
}

/* ------------------------------------------------------------------------ */
/*  init — 640x480 / 256 色を立ち上げる                                      */
/*                                                                          */
/*  gfx_init() が「probe で選ばれた直後」に 1 回だけ呼ぶ (H1 レビュー ⑤)。   */
/*  9801 の GDC / プレーン初期化は走らない (あちらは init が NULL)。         */
/* ------------------------------------------------------------------------ */
/* バックバッファ (300KB) を台帳から受け取る。済んでいれば何もしない。
 * 確保は起動時の ⑥ (gfx_core.c gfx_boot_reserve) が池の CPL=0 子のアリーナの
 * 上端から owner = boot で行い、確保の直後に 0 で埋めて (この面は exec が
 * CPL=3 のアプリへ USER で写すので前の中身を見せない) SURFACE (PEGC の
 * CLIENT) に登録している。アリーナの上端はその下に凍結されるので、CPL=0 の
 * 子もここへは伸びてこない (旧 sys_reserve_top の役、TASK_T1_LEDGER §3-6)。
 * bb_base / bb_size も SURFACE から引く (gfx_bb_phys_range と同じ情報源)。
 * 戻り値 1 = 使える。無ければ probe を取り下げる (9801 へ落ちる)。 */
static int pegc_reserve_backbuffer(void)
{
    const struct ledger_surface *sf;
    if (s_bb_phys != 0) return 1;
    sf = ledger_surface_find(LEDGER_SF_PEGC, LEDGER_ROLE_CLIENT);
    if (!sf) return 0;
    s_bb_phys = sf->first * PAGE_SIZE;
    gfx_backend_pegc.bb_base = P2V(s_bb_phys);
    gfx_backend_pegc.bb_size = sf->npages * PAGE_SIZE;
    return 1;
}

/* ------------------------------------------------------------------------ */
/*  prepare — 起動時と Cirrus 未 probe の GUI 境界での下ごしらえ       */
/*                                                                          */
/*  **表示のモードも同期も変えない**。やるのは                               */
/*    1. 起動時の同期状態の記録 (と診断の 1 行)                              */
/*    2. 24kHz の実機では probe を取り下げ、480 ラインへ進ませない           */
/*  だけ (窓の予約・写像と BB は起動時の ⑥ が probe より前に済ませている)。 */
/*  かつては init → shutdown で済ませていたため、CUI しか使わない      */
/*  起動でも 31kHz/480 ラインへ入って 24kHz/400 ラインの標準 SYNC で戻して   */
/*  いた。実機 (PC-9821Ra266 + 液晶 LCD172VXM) ではテキストが 1 行ごとに     */
/*  1 文字ずつ右へずれ、GFX=pc98 (PEGC を選ばない) で消えた                  */
/*  (TASK_FDC_REALHW §9-1)。NP21/W は同期を模擬しないので再現しない。         */
/*                                                                          */
/*  ⚠ **原因は仮説のまま** (この変更での実機確認はまだ):                     */
/*   - 見立て: 起動時の送り直し (09A8h + GDC SYNC) が BIOS の作った同期と    */
/*     合わず、液晶の自動調整が合わなくなった。                              */
/*   - ただし **24kHz で起動していた場合、旧経路でも最終状態は同じ**         */
/*     (24kHz + 資料の標準 SYNC = BIOS の値)。違いは途中で 31kHz/480 を      */
/*     一瞬通ることと、同じ値の SYNC を入れ直すことだけ。起動行の          */
/*     `[pegc] hsync=` が 24k なら、最終値の食い違いでは説明できない。       */
/*   - それでも直らなければ次に疑う候補 (GFX=pc98 では両方とも走らない):     */
/*       a. PEGC probe (pegc_linear_selftest) の 6Ah 21h → 20h の出入り      */
/*          (拡張グラフィックモードへ入って戻る)。prepare でも走る。          */
/*       b. gdc_send が GDC の FIFO (ステータスの FIFO FULL/EMPTY) を待たずに  */
/*          コマンドとパラメータを流すこと。実機の GDC で SYNC の 8 バイトが   */
/*          取りこぼされうる (今回は注記のみ、直していない)。              */
/* ------------------------------------------------------------------------ */
static void pegc_prepare(void)
{
    if (!pegc_probe()) return;
    pegc_boot_sync_record();
    if (s_boot_hsync == PEGC_HSYNC_24KHZ) {
        int np2 = np2_detect();
        if (!np2) s_probe_ok = 0;
        if (!serial_gate_active())
            serial_puts_polled(np2 ? "[pegc] np2=1 fallback=none\r\n" :
                                    "[pegc] np2=0 fallback=pc98\r\n");
    }
}

/* 480 ラインへ入るポート操作の全部 (MMIO の前まで)。ホスト試験
 * (tools/tests/test_pegc_mode.py) はここを実物のまま回して OUT 列を見る。 */
/* 実機の ROM が止めたまま返す両 GDC を、OS32 が呼び手 (AH=40h / 0Ch の役) と
 * して表示開始する — ROM の列に対する OS32 の追加 (票 §3-3)。 */
static void pegc_enter_480_ports(void)
{
    pegc_apply_timing(&s_timing_480);
    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_START2);
    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);
}

#include "backend_pegc_diag.inc"

static void pegc_init(void)
{
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_pegc);
    if (!pegc_probe()) return;

    /* prepare を経ずに来た場合 (今の経路には無いが) も起動時の状態を先に
     * 記録しておく — 31kHz を書いた後では読み戻しが意味を失う。 */
    pegc_boot_sync_record();

    /* --- 表示モード ---
     * 実機 Ra266 の ROM (INT 18h AH=30h) と同じ列 (pegc_apply_timing、票
     * TASK_PEGC480_REALHW §3-3): 拡張グラフィックモード → 表示停止 → GDC
     * クロック 5MHz → 両 GDC の RESET・SYNC・CSRFORM・PITCH・ZOOM・SCROLL →
     * 800 ライン VRAM 構成・6Eh・テキスト CRTC → 表示。値は pegc.h §10。
     *
     * 以前は「SYNC を 6Ah の 69h/21h より前に置くのが肝」(票 H2c) だったが、
     * 実機の ROM は 21h を最初に出す。NP21/W でも SYNC のパラメータが変われば
     * gdcs.*disp に GDCSCRN_EXT が立ち (io/gdc_cmd.tbl gdc_dirtyflag)、次の
     * フレームで表示サイズが計算し直されるので、順序に依らず 480 ラインになる
     * (NP21/W の回帰で確かめる)。 */
    pegc_enter_480_ports();

    /* --- VRAM の見せ方: パックトピクセル + F00000h にリニア窓 --- */
    mmio_w8(PEGC_MMIO_PIXFMT, PEGC_PIXFMT_PACKED);
    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_ON);
    pegc_diag_alias();

    /* リニア窓は ⑥ が master PD に supervisor + PCD で張ってある (USER は
     * 付けない — F00000h は表示面そのもの。BB は主記憶側で、exec が
     * gfx_bb_phys_range() 経由でアプリ PD に USER で写す。表示面を USER に
     * すると CPL=3 が commit を経ずに画面へ書けて契約 G4 が崩れる、レビュー
     * #5 ②)。 */

    /* --- 画面をクリア --- */
    kmemset((u8 *)P2V(s_bb_phys), 0, mode->pitch * mode->height);
    kmemset((u8 *)P2V_IO(s_window_base), 0, mode->pitch * mode->height);

    /* テキスト VRAM もクリアする (9801 の _gfx_common_init と同じ扱い)。
     * 480 ラインでは 30 行ぶんが見えるので 25 行では足りない (票 H2c)。
     * テキスト面が非 0 の画素はグラフィックより優先されるため、消し残しは
     * そのまま窓を隠す (NP21/W vram/sdrawex.mcr pex_2)。
     * 文字セル (CSRFORM L/R・テキスト CRTC) は pegc_apply_timing が実機の
     * ROM と同じ値 (16 ラスタ) で入れ直している。 */
    pegc_tvram_clear(0, PEGC_TEXT_ROWS_480);

    pegc_palette_init();
    pegc_diag_stripes();

    /* グラフィック GDC の CSRFORM (L/R = 0)・PITCH・GDC クロックも
     * pegc_apply_timing が実機の ROM の値で入れる (以前は起動時の BIOS 状態に
     * 頼っていた — 票 TASK_PEGC480_REALHW §2 H5、§3-3)。 */
    pegc_diag_take(&s_diag_enter);
    pegc_diag_serial(&s_diag_enter);

    /* HAL 共通の状態。PEGC ではハードウェアページ切替を使わない
     * (800 ライン構成では 00A4h が使えない — pegc.h の PEGC_FF2_VRAM_800L)。 */
    gfx_current_height = mode->height;
    gfx_backend_pegc.bb_pitch = mode->pitch;
    gfx_flip_enabled = 0;
    gfx_display_page = 0;

    s_active = 1;
}

#include "backend_pegc_present.inc"

#include "backend_pegc_restore.inc"

/* ------------------------------------------------------------------------ */
/*  バックエンド表。                                                        */
/*  const ではない: bb_base / bb_size を init() が実行時に埋める             */
/*  (バックバッファは物理メモリ末尾からの動的な切り出し)。                   */
/* ------------------------------------------------------------------------ */
GfxBackend gfx_backend_pegc = {
    "pegc-256",
    pegc_probe,
    pegc_init,
    pegc_query,
    pegc_shutdown,
    pegc_present_rect,
    pegc_set_palette,
    (void (*)(void))0,         /* enter: 表示リレーなし */
    (void (*)(void))0,         /* leave: 表示リレーなし */
    (int (*)(int, int, int, int, u8))0,        /* fill_rect: CPU 実装へ */
    (int (*)(int, int, int, int, int, int))0,  /* blit:      CPU 実装へ */
    (u8 *)0,                  /* bb_base:  init() が埋める */
    (u32)PEGC_PITCH,          /* bb_pitch: 640 バイト/ライン */
    GFX_BB_PACKED8,
    0,                        /* bb_size:  init() が埋める */
    pegc_prepare,             /* 起動時: 予約・写像・同期の記録だけ */
    pegc_modes,
    sizeof(pegc_modes) / sizeof(pegc_modes[0]),
    0,                        /* default_mode */
    (const GfxMode *)0         /* selected_mode */
};
