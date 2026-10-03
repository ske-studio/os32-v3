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

#include "memmap.h"
#include "gfx_internal.h"   /* gfx.h (PAL_*_PORT), pc98.h, memmap.h, _out/_in */
#include "gfx_hal.h"
#include "pegc.h"
#include "paging.h"
#include "pgalloc.h"
#include "sys.h"
#include "kstring.h"
#include "kprintf.h"
#include "palette.h"   /* palette_get_all / palette_shadow_set */
#include "os32_kapi_shared.h"
#include "cpu_calibrate.h"   /* cpu_delay_us (GDC の FIFO 待ち) */
#include "console.h"         /* console_hw_cursor_enable (CUI へ戻った後のカーソル) */
#include "con_sink.h"        /* con_sink_is_enabled (GUI 中はカーソルを出さない) */

/* アプリ帯 (kernel/paging.c) がリニア窓を踏まないための照合 ([C4] 三層定数)。
 * memmap.h 側は pegc.h を読まない (9821 の文字を core に持ち込まない) ので、
 * 両方を読むここで値の一致を検査する。ここが落ちたら
 * MEM_SYSTEM_SPACE_BASE を PEGC_LINEAR_BASE に合わせ直すこと。 */
STATIC_ASSERT(MEM_SYSTEM_SPACE_BASE == PEGC_LINEAR_BASE,
              app_band_device_floor_is_pegc_window);

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
static int s_probe_ok  = 0;   /* probe の結果 (キャッシュ) */
static int s_active    = 0;   /* init 済み = 拡張グラフィックモード中か */
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
/*  E0000h も 0000:0400h 台も低位 1MB のアイデンティティマップなので、       */
/*  volatile ポインタで直接読み書きできる (ページングの追加作業は不要)。      */
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

/* BIOS ワークエリアは master PD にしか写像が無い (ページ 0 は R/O、アプリ
 * PD では not present)。CPL=3 のアプリ文脈から読むと #PF でアプリが死ぬ。
 * probe は起動時のカーネル文脈で 1 回だけ走らせて結果をキャッシュする設計
 * (kernel.c の gfx_probe_backends) なので、ここへ来るのは想定外。多重防御
 * として、master AS 以外では読まずに 0 を返す。 */
static int in_master_addrspace(void)
{
    return paging_current_cr3() == paging_kernel_pd_phys();
}

static u8 bios_flag(u32 addr)
{
    /* アドレスを volatile 経由にして定数畳み込みを止める。直に
     * *(volatile u8 *)0x045C と書くと GCC が「ヌルポインタ近傍の配列外」と
     * 誤診断する (-Warray-bounds)。ここは BIOS ワークエリアの実アドレス。 */
    volatile u32 a = addr;
    if (!in_master_addrspace()) return 0;
    return *(volatile u8 *)P2V_IO(a);
}

/* モード F/F2 の読み戻し ([B] 表3-3): 番号を 09A0h に書いて同じ番号から読む。 */
static int pegc_stat(u8 sel)
{
    _out(PEGC_STAT_PORT, sel);
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

/* ------------------------------------------------------------------------ */
/*  GDC の表示制御 (票 H2c)                                                  */
/*                                                                          */
/*  [HW1] が禁じているのは GDC の **描画** コマンド (VECTW/VECTE/TEXTE/      */
/*  WDAT 等) であって、表示制御 (SYNC / SCROLL / START / STOP / PITCH) は    */
/*  9801 側の backend_pc98.c / gfx_core.c も従来から使っている。画素は        */
/*  引き続き CPU からリニア窓へ直接書くだけ。                                */
/*                                                                          */
/*  コマンドは 62h (マスタ = テキスト) / A2h (スレーブ = グラフィック)、      */
/*  パラメータは 60h / A0h。                                                 */
/* ------------------------------------------------------------------------ */
/* uPD7220 の 1 面ぶんのポート。ステータスの READ はパラメータの WRITE と同じ
 * 番地 (pc98.h: 60h / A0h)。 */
typedef struct {
    u16 cmd;
    u16 prm;
    u16 stat;
} PegcGdcPorts;

static const PegcGdcPorts s_gdc_text = { GDC_TEXT_CMD, GDC_TEXT_PARAM, GDC_TEXT_STAT };
static const PegcGdcPorts s_gdc_gfx  = { GDC_GFX_CMD,  GDC_GFX_PARAM,  GDC_GFX_STAT  };

/* ステータスの (st & mask) == want になるまで待つ。上限は pegc.h §12。
 * 間に合わなければ数えて 0 を返す (呼び手はそのまま書く)。 */
static int gdc_wait_status(u16 stat_port, u8 mask, u8 want)
{
    int i;
    for (i = 0; i < PEGC_GDC_FIFO_POLLS; i++) {
        u8 st = (u8)_in(stat_port);
        gfx_counters.io_accesses++;
        if ((u8)(st & mask) == want) return 1;
        cpu_delay_us(PEGC_GDC_FIFO_POLL_US);
    }
    pegc_gdc_fifo_timeouts++;
    return 0;
}

/* コマンド 1 バイトとパラメータ n バイトを送る (票 TASK_PEGC480_REALHW §4)。
 * コマンドの前は FIFO EMPTY (前のコマンドを食べ終わった)、パラメータの前は
 * FIFO FULL でないことを待つ。以前は待たずに流していた (H3)。 */
static void gdc_send(const PegcGdcPorts *g, u8 cmd, const u8 *para, int n)
{
    int i;
    (void)gdc_wait_status(g->stat, GDC_STAT_FEMP, GDC_STAT_FEMP);
    _out(g->cmd, cmd);
    for (i = 0; i < n; i++) {
        (void)gdc_wait_status(g->stat, GDC_STAT_FFUL, 0);
        _out(g->prm, para[i]);
    }
    gfx_counters.io_accesses += (u32)(n + 1);
}

/* テキスト GDC のステータスの VSYNC (bit5) が want になるまで待つ (pegc.h §13)。
 * 上限で諦めたら数えて先へ。 */
static void pegc_wait_vsync_edge(u8 want)
{
    int i;
    for (i = 0; i < PEGC_VSYNC_POLLS; i++) {
        u8 st = (u8)_in(GDC_TEXT_STAT);
        gfx_counters.io_accesses++;
        if ((u8)(st & GDC_STAT_VSYNC) == want) return;
        cpu_delay_us(PEGC_VSYNC_POLL_US);
    }
    pegc_vsync_timeouts++;
}

/* VSYNC が来て、明けるまで (= 次の表示期間の頭)。実機の ROM の群の間の待ち。 */
static void pegc_wait_vsync(void)
{
    pegc_wait_vsync_edge(GDC_STAT_VSYNC);
    pegc_wait_vsync_edge(0);
}

/* 6Eh (拡張アトリビュート) の 1 書き。[U] io_disp.md I/O 006Eh 0010000nb
 * 「OUT 時には OUT 5Fh,AL によるウェイトが必要」なので毎回 1 つ挟む
 * (io_wait = 5Fh への書き込み。v86 -g の記録は 5Fh を捕まえないので、実機の
 * ROM が挟んでいたかは列からは見えない)。 */
static void xattr_write(u8 val)
{
    _out(PEGC_XATTR_PORT, val);
    io_wait();
    gfx_counters.io_accesses += 2;
}

static void gdc_cmd(const PegcGdcPorts *g, u8 cmd)
{
    gdc_send(g, cmd, (const u8 *)0, 0);
}

static void gdc_cmd1(const PegcGdcPorts *g, u8 cmd, u8 para)
{
    gdc_send(g, cmd, &para, 1);
}

/* 表示モード一式 (値は pegc.h §10 — 実機の記録で差し替える 1 か所)。 */
typedef struct {
    int       pegc;        /* 1 = PEGC の probe が通った機種: 6Ah・6Eh を書く */
    u8        ext;         /* 6Ah: PEGC_FF2_EXT_GFX (21h) / PEGC_FF2_STD_GFX (20h) */
    u8        vram;        /* 6Ah: PEGC_FF2_VRAM_800L (69h) / PEGC_FF2_VRAM_400L (68h) */
    u8        xattr;       /* 6Eh: PEGC_XATTR_31KHZ (21h) / PEGC_XATTR_24KHZ (20h) */
    u8        hsync;       /* 09A8h (今の読みと違うときだけ書く) */
    int       set_clock;   /* 0 = クロックと PITCH に触らない (起動時の値が分からない) */
    u8        clk1;        /* 6Ah: PEGC_FF2_GDC_CLK1_* */
    u8        clk2;        /* 6Ah: PEGC_FF2_GDC_CLK2_* */
    const u8 *msync;       /* テキスト (マスタ) GDC の SYNC 8 バイト */
    const u8 *ssync;       /* グラフィック (スレーブ) GDC の SYNC 8 バイト */
    u8        pitch;       /* グラフィック GDC の PITCH (set_clock のときだけ) */
    const u8 *scroll;      /* グラフィック GDC の SCROLL 4 バイト */
} PegcTiming;

/* 表示モードを入れる。**書く順序はここ 1 か所** で、実機 PC-9821Ra266 の ROM の
 * INT 18h AH=30h の OUT 列 (`v86 -g`、票 TASK_PEGC480_REALHW §3-3 の比較表) を
 * そのままなぞる。s480 と back は値が違うだけで同じ形だった。
 *   1. 6Ah 07h/(21h|20h)/06h、6Ah 41h (LCD モード)
 *   2. 68h 0Eh (画面表示を止める)
 *   3. [OS32] 09A8h — 今の読み (D0) と違うときだけ。実機 Ra266 の ROM は起動時
 *      から 31kHz なので書いていない (09A8h は 3 回読んだだけ)。24kHz で起動する
 *      機種 (NP21/W) では書かないと 31kHz にならない。
 *   4. 6Ah GDC CLOCK-1、CLOCK-2 ([B] 3-2 表3-2「周波数を変更したら SYNC の再設定」)
 *   5. VSYNC 待ち、グラフィック GDC RESET1・SLAVE、VSYNC 待ち
 *   6. テキスト GDC RESET1・MASTER・SYNC・CSRFORM
 *   7. グラフィック GDC SYNC・CSRFORM
 *   8. 両 GDC RESET1 (グラフィック → テキスト)、VSYNC 待ち ×2
 *   9. 両 GDC STOP2
 *  10. グラフィック GDC PITCH・ZOOM・SCROLL、テキスト GDC PITCH・ZOOM・SCROLL
 *  11. VSYNC 待ち、START ×2、VSYNC 待ち、STOP2 ×2、START ×2、STOP2 ×2
 *  12. 68h 08h、6Ah 07h/(69h|68h)/06h、6Eh 03h/(21h|20h)/02h、CRTC 70h〜7Ah
 *  13. VSYNC 待ち、68h 0Fh (画面表示)、6Eh 03h・68h 07h・68h 00h・6Eh 02h
 * 実機の ROM は両 GDC を**止めたまま**返す (表示の開始は呼び手の AH=40h / 0Ch
 * の仕事)。OS32 は呼び手を兼ねるので、START は呼び手 (pegc_enter_480_ports /
 * pegc_text_sync_400) が出す。
 * 実機の列から**意図的に外したもの** (理由は票 §3-3):
 *   - 6Ah 07h・A0h DFh・A2h 28h・6Ah 06h: **未解明の差分**。A2h 28h は WRITE
 *     ([B] 2-6 表2-26 の描画制御コマンド = [HW1])、DFh はコマンドより先の
 *     パラメータで意味が資料に無い。6Ah 07h/06h が A0h/A2h の意味を変えるとも
 *     書かれていない。RESET1 で打ち消されるとも言えない (票 §3-3)。
 *   - A2h 20h・78h 00h 00h: WRITE と TEXTW (描画パターン) — [HW1]。
 *   - 09A0h 03h (+ 読み): ROM が表示状態を読む選択。OS32 は最後に必ず表示可。
 *   - emu 行 (063Ch・A46Eh・A660h): 記録では実機へ通していない = ROM が見た
 *     読み値は偽 (FFh)。063Ch は D8000h の ROM バンク ([U] io_mem.md)、A46Eh・
 *     A660h は PC-98GS の SGP/同期 ([U] io_gs.md) で、Ra266 での意味は不明。
 *   - back の 68h 09h: ROM は BIOS ワークエリアの「グラフィック 200 ライン」を
 *     戻す。OS32 の 400 ライン構成 (gfx_core.c の 68h 08h) に合わせて 08h。
 * PEGC の probe が通っていない機種 (v86 -g の FALLBACK が 9801 で来た場合) は
 * 6Ah・6Eh を書かない (6Eh は古い 9801 では周波数切換 — pegc.h §13)。 */
static void pegc_apply_timing(const PegcTiming *t)
{
    int i;

    if (t->pegc) {
        ff2_locked_write(t->ext);
        _out(MODE_FF2_PORT, PEGC_FF2_LCD_MODE);
        gfx_counters.io_accesses++;
    }
    _out(MODE_FF1_PORT, MFF1_DISP_OFF);
    gfx_counters.io_accesses++;

    if (((u8)_in(PEGC_HSYNC_PORT) & PEGC_HSYNC_READ_HF) !=
        (u8)(t->hsync & PEGC_HSYNC_READ_HF)) {
        _out(PEGC_HSYNC_PORT, t->hsync & PEGC_HSYNC_MASK);
    }
    gfx_counters.io_accesses += 2;
    if (t->set_clock) {
        _out(MODE_FF2_PORT, t->clk1);
        _out(MODE_FF2_PORT, t->clk2);
        gfx_counters.io_accesses += 2;
    }

    pegc_wait_vsync();
    gdc_cmd(&s_gdc_gfx, GDC_CMD_RESET);
    gdc_cmd(&s_gdc_gfx, GDC_CMD_SLAVE);
    pegc_wait_vsync();

    gdc_cmd(&s_gdc_text, GDC_CMD_RESET);
    gdc_cmd(&s_gdc_text, GDC_CMD_MASTER);
    gdc_send(&s_gdc_text, GDC_CMD_SYNC, t->msync, PEGC_GDC_SYNC_LEN);
    gdc_send(&s_gdc_text, GDC_CMD_CSRFORM, s_tcsrform, PEGC_GDC_CSRFORM_LEN);
    gdc_send(&s_gdc_gfx,  GDC_CMD_SYNC, t->ssync, PEGC_GDC_SYNC_LEN);
    gdc_send(&s_gdc_gfx,  GDC_CMD_CSRFORM, s_gcsrform, PEGC_GDC_CSRFORM_LEN);

    gdc_cmd(&s_gdc_gfx,  GDC_CMD_RESET);
    gdc_cmd(&s_gdc_text, GDC_CMD_RESET);
    pegc_wait_vsync();
    pegc_wait_vsync();

    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_STOP2);
    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_STOP2);
    if (t->set_clock) gdc_cmd1(&s_gdc_gfx, GDC_CMD_PITCH, t->pitch);
    gdc_cmd1(&s_gdc_gfx, GDC_CMD_ZOOM, PEGC_GDC_ZOOM);
    gdc_send(&s_gdc_gfx, GDC_CMD_SCROLL, t->scroll, PEGC_GDC_SCROLL_LEN);
    gdc_cmd1(&s_gdc_text, GDC_CMD_PITCH, PEGC_GDC_TPITCH);
    gdc_cmd1(&s_gdc_text, GDC_CMD_ZOOM, PEGC_GDC_ZOOM);
    gdc_send(&s_gdc_text, GDC_CMD_SCROLL, s_tscroll, PEGC_GDC_SCROLL_LEN);
    pegc_wait_vsync();

    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_START2);
    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);
    pegc_wait_vsync();
    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_STOP2);
    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_STOP2);
    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_START2);
    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);
    gdc_cmd(&s_gdc_gfx,  PEGC_GDC_CMD_STOP2);
    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_STOP2);

    _out(MODE_FF1_PORT, MFF1_HIRES);
    gfx_counters.io_accesses++;
    if (t->pegc) {
        ff2_locked_write(t->vram);
        xattr_write(PEGC_XATTR_UNLOCK);
        xattr_write(t->xattr);
        xattr_write(PEGC_XATTR_LOCK);
    }
    for (i = 0; i < PEGC_CRTC_COUNT; i++) {
        _out(s_crtc_port[i], s_crtc_val[i]);
    }
    gfx_counters.io_accesses += (u32)PEGC_CRTC_COUNT;
    pegc_wait_vsync();

    _out(MODE_FF1_PORT, MFF1_DISP_ON);
    gfx_counters.io_accesses++;
    if (t->pegc) xattr_write(PEGC_XATTR_UNLOCK);
    _out(MODE_FF1_PORT, MFF1_ANK_7x13);
    _out(MODE_FF1_PORT, MFF1_ATR_VLINE);
    gfx_counters.io_accesses += 2;
    if (t->pegc) xattr_write(PEGC_XATTR_LOCK);
}

/* 480 ラインへ入るときの一式 (pegc.h §10 の値)。 */
static const PegcTiming s_timing_480 = {
    1, PEGC_FF2_EXT_GFX, PEGC_FF2_VRAM_800L, PEGC_XATTR_31KHZ,
    PEGC_HSYNC_31KHZ, 1, PEGC_GDC_CLK1_480, PEGC_GDC_CLK2_480,
    s_msync_480, s_ssync_480, PEGC_GDC_PITCH_480, s_scroll_480
};

/* テキスト VRAM の row0 行目から row1 行目の手前までをクリアする。
 * 480 ラインでは 30 行が見えるので、25 行モードのまま切り替えると
 * 26 行目以降に古い内容が残り、テキストがグラフィックを隠す
 * (NP21/W vram/sdrawex.mcr pex_2 はテキスト画素優先)。 */
static void pegc_tvram_clear(int row0, int row1)
{
    volatile u16 *tvram_char = (volatile u16 *)P2V_IO(TVRAM_CHAR_BASE);
    volatile u8  *tvram_attr = (volatile u8  *)P2V_IO(TVRAM_ATTR_BASE);
    int i;
    int from = TVRAM_COLS * row0;
    int to   = TVRAM_COLS * row1;

    for (i = from; i < to; i++) {
        tvram_char[i] = 0x0000;
        tvram_attr[i * 2] = 0x00;
    }
}

/* ------------------------------------------------------------------------ */
/*  起動時の同期状態の記録 (実機 Ra266 + 液晶の桁ズレ、TASK_FDC_REALHW §9-1)  */
/*                                                                          */
/*  読めるものだけ読む:                                                      */
/*    09A8h D0      水平走査周波数 ([U] io_disp.md「I/O 09A8h」READ/WRITE、  */
/*                  [B] §3-2 表3-1 は読み出しの D0=HF だけを定義し、D7〜D1   */
/*                  は不定)。**読み値でポートの有無を判定しない** — FFh も    */
/*                  D0=1 の正常な読みでありうる (レビュー往復 2)。           */
/*                                                                          */
/*  「PEGC probe が通った機種には 09A8h がある」の裏付けと限界:              */
/*    - [B] §3-2 表3-1 は「H98・MATE 共通の拡張 I/O ポート」として 09A8H の  */
/*      リード (D0=HF) を載せている。MATE = PC-9821 系、PEGC もこの節の      */
/*      「MATE の 256 色表示」。                                             */
/*    - [U] io_disp.md「I/O 09A8h」の対象は PC-98GS, PC-H98,                  */
/*      PC-9821[Ts を除く], PC-9801BA2･BS2･BX2, PC-9801NS/A。READ/WRITE は    */
/*      NS/A 以外 (NS/A は bit7 不明・他は未使用で意味が違う)。              */
/*    - probe の段 4 が見る 09A0h の対象 ([U] 同 1585 行) は PC-98GS, PC-H98, */
/*      PC-9821[Ts を除く], PC-9801BA2･BS2･BX2･BX3･BA3･BX4･NS/A。            */
/*      つまり **09A0h があって 09A8h の対象に無いのは BX3･BA3･BX4 だけ**、   */
/*      読みの意味が違うのが NS/A。これらは 9801 で、probe の段 1            */
/*      (045Ch bit6 と 0597h bit2) と段 5 (F00000h の PEGC リニア窓の        */
/*      書き読み) まで通る機種だという記述は資料に無い — **通らないはずだが、 */
/*      資料で直接「PEGC 機 ⇒ 09A8h あり」と書いた箇所は無い** (推論)。       */
/*    - PC-H98 は対象に入るが、H98 の 256 色は PEGC ではなく probe の段 5 を */
/*      通らない想定 (未確認)。                                               */
/*  この前提が崩れた機種では hsync= の値が当てにならない。行の MISMATCH と、 */
/*  480 ラインから戻った後の表示で気づける。                                 */
/*    054Ch bit5    BIOS の記録する水平走査周波数 ([US] memsys.md PRXCRT)。  */
/*                  9821 初代は 31kHz でも 0 なので補助にだけ使う。          */
/*    0459h bit0    BIOS の 480 ラインフラグ ([US] memsys.md CRT_EXT_STS)。   */
/*  テキスト GDC の SYNC は読めない (uPD7220 の SYNC は書き込み専用。I/O     */
/*  0062h の READ 系は READ/LPEN/CSRR だけ — [U] io_disp.md)。BIOS ワーク    */
/*  エリアにも写しは無い。だから「480 ラインへ入らない限り触らない」が本線。  */
/* ------------------------------------------------------------------------ */
/* 400 ラインへ戻るときの PITCH (pegc.h §10)。起動時のクロックが両方 5MHz
 * なら 80、それ以外は 40。未記録 (-1) は 2.5MHz 扱い。 */
static u8 pegc_restore_pitch(void)
{
    if (s_boot_clk1 == 1 && s_boot_clk2 == 1) return PEGC_GDC_PITCH_400_5M;
    return PEGC_GDC_PITCH_400_2M5;
}

static void pegc_boot_sync_record(void)
{
    u8 raw, prxcrt, crtext, clk, dupd;
    int hs_bios;

    if (s_boot_recorded) return;
    s_boot_recorded = 1;

    raw    = (u8)_in(PEGC_HSYNC_PORT);
    prxcrt = bios_flag(PEGC_BIOS_PRXCRT);
    crtext = bios_flag(PEGC_BIOS_CRT_EXT_STS);
    gfx_counters.io_accesses++;

    /* ここへ来るのは probe が通った後だけ (pegc_prepare / pegc_init)。
     * ポートはある前提 (上の注記) で、定義のある D0 だけを採る。 */
    s_boot_hsync = (raw & PEGC_HSYNC_READ_HF) ? PEGC_HSYNC_31KHZ
                                              : PEGC_HSYNC_24KHZ;
    hs_bios = (prxcrt & PEGC_BIOS_PRXCRT_31KHZ) ? 1 : 0;

    /* 09A8h と 054Ch bit5 の食い違いは行に出す (採るのは 09A8h)。
     * 054Ch bit5=0 は 9821 初代では 31kHz でも起きる ([US] memsys.md)。
     * 09a8= は生の読み値 (D7〜D1 は不定なので値そのものに意味は無い)。 */
    kprintf(0x07, "[pegc] hsync=%s 09a8=%02x bios054c.b5=%d bios0459.b0=%d%s\n",
            s_boot_hsync == PEGC_HSYNC_31KHZ ? "31k" : "24k",
            (unsigned int)raw, hs_bios,
            (crtext & PEGC_BIOS_CRT_480LINE) ? 1 : 0,
            ((s_boot_hsync == PEGC_HSYNC_31KHZ) != hs_bios)
                ? " MISMATCH(09a8 vs 054c)" : "");

    /* GDC クロック (pegc.h §2)。09A0h は probe が通った機種にある (段 4 で
     * 読めている)。054Dh bit2 は BIOS の記録する現在の GDC クロック
     * ([US] memsys.md PRXDUPD) — 比べるだけで採るのは 09A0h。 */
    _out(PEGC_STAT_PORT, PEGC_STAT_SEL_GDCCLK1);
    clk = (u8)_in(PEGC_STAT_PORT);
    gfx_counters.io_accesses += 2;
    s_boot_clk1 = (clk & PEGC_STAT_BIT) ? 1 : 0;
    s_boot_clk2 = (clk & PEGC_STAT_RD_GDCCLK2) ? 1 : 0;
    dupd = bios_flag(PEGC_BIOS_PRXDUPD);
    kprintf(0x07, "[pegc] gdcclk=%s clk1=%d clk2=%d bios054d.b2=%d pitch400=%d\n",
            (s_boot_clk1 && s_boot_clk2) ? "5M" : "2.5M",
            s_boot_clk1, s_boot_clk2,
            (dupd & PEGC_BIOS_PRX_GDC5M) ? 1 : 0,
            (int)pegc_restore_pitch());
}

/* 480 ラインから戻るときの周波数。起動時に記録した値。分からなかったとき
 * (-1 = まだ記録していない。init は必ず先に記録するので、ここへは来ない
 * はず) だけ従来の 24kHz (OS32 のテキストの前提、資料の標準 SYNC と対) に
 * 落とす — H2 以来の既定動作。 */
static u8 pegc_restore_hsync(void)
{
    if (s_boot_hsync == PEGC_HSYNC_31KHZ) return PEGC_HSYNC_31KHZ;
    return PEGC_HSYNC_24KHZ;
}

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
/*    5. 実際に拡張モードへ入り、⑥ が張ったリニア窓で書き込み読み戻し試験。 */
/*       終わったら **必ず標準グラフィックモードへ戻す** (probe は副作用を    */
/*       残さない)。本番のモード設定は init() が行う。                       */
/* ------------------------------------------------------------------------ */
static int pegc_linear_selftest(void);
static int pegc_reserve_backbuffer(void);

/* ページングは先頭 16MB しか固定の PT を持たない。窓の末尾まで入ること。 */
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

    /* --- 4. 09A0h が 6Ah に追随するか (open bus 除け) --- */
    _out(MODE_FF2_PORT, PEGC_FF2_UNLOCK);
    unlocked = pegc_stat(PEGC_STAT_SEL_UNLOCK);
    _out(MODE_FF2_PORT, PEGC_FF2_LOCK);
    locked = pegc_stat(PEGC_STAT_SEL_UNLOCK);
    gfx_counters.io_accesses += 2;
    if (!(unlocked == 1 && locked == 0)) return 0;

    /* --- 5. リニア窓の書き込み読み戻し --- */
    s_probe_ok = pegc_linear_selftest();
    return s_probe_ok;
}

/* 拡張モードへ一時的に入り、リニア窓で読み書きできるか確かめる。
 * 戻り値 1 = 使える。終了時はハードを呼び出し前の状態に戻す。窓の写像は ⑥ が
 * 張ったもの (supervisor + PCD) で、失敗しても剥がさない (予約・写像は永久
 * 保持、TASK_T1_LEDGER §3-8)。 */
static int pegc_linear_selftest(void)
{
    volatile u8 *fb = (volatile u8 *)P2V_IO(PEGC_LINEAR_BASE);
    int ok = 0;
    u8 s0, s1;

    /* 拡張グラフィックモードへ。E0000h の意味が変わるのはこの瞬間から。 */
    ff2_locked_write(PEGC_FF2_EXT_GFX);
    if (!pegc_stat(PEGC_STAT_SEL_GFXMODE)) goto restore_mode;

    mmio_w8(PEGC_MMIO_PIXFMT, PEGC_PIXFMT_PACKED);
    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_ON);

    /* 2 か所に別の値を書いて読み戻す。同じ値が両方から返る (エイリアス) /
     * 何も返らない (open bus) を弾く。試験に使うのは表示されない末尾側。 */
    s0 = fb[0];
    s1 = fb[PEGC_FB_SIZE_480 - 1];
    fb[0] = 0x5A;
    fb[PEGC_FB_SIZE_480 - 1] = 0xA5;
    if (fb[0] == 0x5A && fb[PEGC_FB_SIZE_480 - 1] == 0xA5) ok = 1;
    fb[0] = s0;
    fb[PEGC_FB_SIZE_480 - 1] = s1;

    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_OFF);
restore_mode:
    ff2_locked_write(PEGC_FF2_STD_GFX);
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
    return (u8)((v & PEGC_PAL_KAPI_MASK) * PEGC_PAL_KAPI_SCALE);
}

static void pegc_palette_init(void)
{
    static const u8 cube[6] = { 0, 51, 102, 153, 204, 255 };
    const PaletteEntry *sys = palette_get_all();
    int i, r, g, b;

    /* 0-15: 9801 の 16 色をそのまま (4bit 輝度 → 8bit) */
    for (i = 0; i < PALETTE_COUNT; i++) {
        pegc_write_palette(i, pegc_pal8(sys[i].r), pegc_pal8(sys[i].g),
                           pegc_pal8(sys[i].b));
    }

    /* 16-231: 6x6x6 色立方 */
    i = PEGC_LEASE_FIRST;
    for (r = 0; r < 6; r++) {
        for (g = 0; g < 6; g++) {
            for (b = 0; b < 6; b++) {
                pegc_write_palette(i++, cube[r], cube[g], cube[b]);
            }
        }
    }

    /* 232-255: グレースケール 24 段 */
    for (b = 0; i < PEGC_PALETTE_COUNT; i++, b++) {
        u8 v = (u8)(8 + b * 10);
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
    if (!sf) {
        s_probe_ok = 0;
        return 0;
    }
    s_bb_phys = sf->first * PAGE_SIZE;
    gfx_backend_pegc.bb_base = P2V(s_bb_phys);
    gfx_backend_pegc.bb_size = sf->npages * PAGE_SIZE;
    return 1;
}

/* ------------------------------------------------------------------------ */
/*  prepare — 起動時の下ごしらえ (gfx_prepare_backend が 1 回だけ呼ぶ)       */
/*                                                                          */
/*  **表示のモードも同期も変えない**。やるのは                               */
/*    1. 起動時の同期状態の記録 (と診断の 1 行)                              */
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

/* 状態の記録 (シリアル越しでも読める 1 行にして出す — rshell は kprintf を写す)。
 * 09A8h の生の読みと、09A0h で読める 6Ah / 68h の状態 ([U] io_disp.md
 * I/O 09A0h: 02h = 奇数ラスタのマスク (68h 09h で 1)、03h = 表示 (68h 0Fh で 1)、
 * 05h = LCD モード (6Ah 41h で 1)、09h = GDC クロック (bit0 CLOCK-1 / bit1
 * CLOCK-2)、0Ah = 256 色、0Dh = 800 ライン構成)、FIFO と VSYNC の打ち切り数。
 * 実機で画面を見ずに確かめる材料 (`pegcchk`、票 TASK_PEGC480_REALHW §5)。
 * 480 ラインへ入ったときは**読むだけ**で、出すのは戻った後 (CUI で 480 ラインの
 * 間に出すとテキスト面がテスト画に重なる — テキストはグラフィックより優先)。 */
typedef struct {
    u8  hs;
    u8  clk;
    u8  msk, dsp, lcd, ext, pgc;
    u32 fifo_to, vs_to;
} PegcDiag;

static PegcDiag s_diag_enter;

static void pegc_diag_take(PegcDiag *d)
{
    d->hs  = (u8)_in(PEGC_HSYNC_PORT);
    d->msk = (u8)pegc_stat(PEGC_STAT_SEL_ODDMASK);
    d->dsp = (u8)pegc_stat(PEGC_STAT_SEL_DISP);
    d->lcd = (u8)pegc_stat(PEGC_STAT_SEL_LCD);
    _out(PEGC_STAT_PORT, PEGC_STAT_SEL_GDCCLK1);
    d->clk = (u8)(_in(PEGC_STAT_PORT) & (PEGC_STAT_BIT | PEGC_STAT_RD_GDCCLK2));
    d->ext = (u8)pegc_stat(PEGC_STAT_SEL_GFXMODE);
    d->pgc = (u8)pegc_stat(PEGC_STAT_SEL_PAGECONT);
    d->fifo_to = pegc_gdc_fifo_timeouts;
    d->vs_to = pegc_vsync_timeouts;
    gfx_counters.io_accesses += 14;
}

static void pegc_diag_print(const char *tag, const PegcDiag *d)
{
    kprintf(0x07, "[pegc] %s 09a8=%02x msk=%d dsp=%d lcd=%d clk=%x ext=%d "
            "800l=%d fifo_to=%u vs_to=%u\n", tag, (unsigned int)d->hs,
            (int)d->msk, (int)d->dsp, (int)d->lcd, (unsigned int)d->clk,
            (int)d->ext, (int)d->pgc, (unsigned int)d->fifo_to,
            (unsigned int)d->vs_to);
}

static void pegc_init(void)
{
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

    /* リニア窓は ⑥ が master PD に supervisor + PCD で張ってある (USER は
     * 付けない — F00000h は表示面そのもの。BB は主記憶側で、exec が
     * gfx_bb_phys_range() 経由でアプリ PD に USER で写す。表示面を USER に
     * すると CPL=3 が commit を経ずに画面へ書けて契約 G4 が崩れる、レビュー
     * #5 ②)。 */

    /* --- 画面をクリア --- */
    kmemset((u8 *)P2V(s_bb_phys), 0, (u32)MEM_GFX_BB8_SIZE);
    kmemset((u8 *)P2V(PEGC_LINEAR_BASE), 0, (u32)PEGC_FB_SIZE_480);

    /* テキスト VRAM もクリアする (9801 の _gfx_common_init と同じ扱い)。
     * 480 ラインでは 30 行ぶんが見えるので 25 行では足りない (票 H2c)。
     * テキスト面が非 0 の画素はグラフィックより優先されるため、消し残しは
     * そのまま窓を隠す (NP21/W vram/sdrawex.mcr pex_2)。
     * 文字セル (CSRFORM L/R・テキスト CRTC) は pegc_apply_timing が実機の
     * ROM と同じ値 (16 ラスタ) で入れ直している。 */
    pegc_tvram_clear(0, PEGC_TEXT_ROWS_480);

    pegc_palette_init();

    /* グラフィック GDC の CSRFORM (L/R = 0)・PITCH・GDC クロックも
     * pegc_apply_timing が実機の ROM の値で入れる (以前は起動時の BIOS 状態に
     * 頼っていた — 票 TASK_PEGC480_REALHW §2 H5、§3-3)。 */
    pegc_diag_take(&s_diag_enter);

    /* HAL 共通の状態。PEGC ではハードウェアページ切替を使わない
     * (800 ライン構成では 00A4h が使えない — pegc.h の PEGC_FF2_VRAM_800L)。 */
    gfx_current_height = PEGC_HEIGHT_480;
    gfx_flip_enabled = 0;
    gfx_display_page = 0;

    s_active = 1;
}

/* ------------------------------------------------------------------------ */
/*  query — 能力ビットと画面情報 (契約 G5)。副作用なし・冪等。               */
/* ------------------------------------------------------------------------ */
static int pegc_query(GFX_ScreenInfo *info)
{
    int i;
    if (!info) return OS32_ERR_INVAL;

    info->width  = (u16)PEGC_WIDTH;
    info->height = (u16)PEGC_HEIGHT_480;
    info->bpp    = PEGC_BPP;
    info->format = GFX_FMT_PACKED8;

    /* 能力ビット (票 7):
     *  TEXT_OVERLAY = 1。NP21/W で実測 (2026-09-06): 256 色モード中に TVRAM へ
     *    書いた文字 (gui_busy の kprintf) が 256 色の絵の上に合成されて見えた
     *    (DESIGN §5 に書き戻し済み)。R4 の TVRAM クロームは PEGC でも使える。
     *  PAGE_FLIP    = 0。800 ライン VRAM 構成では 00A4h (表示ページ選択) が
     *    使えないので、PEGC のページ切替は v1 では使わない。
     *  HW_FILL/HW_BLT = 0。PEGC は塗り/転送エンジンを持たない (CPU 直書き)。 */
    info->flags  = GFX_CAP_TEXT_OVERLAY;

    /* パレットのリース (契約 G8): 256 色機は連続範囲で申告する。
     * lease_mask は 16 色機用なので 0。 */
    info->lease_mask  = 0;
    info->lease_first = (u16)PEGC_LEASE_FIRST;
    info->lease_count = (u16)PEGC_LEASE_COUNT;
    for (i = 0; i < 5; i++) info->reserved[i] = 0;
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  present_rect — バックバッファの矩形をリニア窓へ転送                      */
/*                                                                          */
/*  1 ライン 640 バイトの単純な矩形コピー。プレーンに分解しないので 9801 の   */
/*  ダーティキュー (gfx_vram.c) は通さず、ここで直接転送する。               */
/*  ⚠ 資料 ([B] §2-7「VRAM アクセスの注意点」):「32 ビットマシンであっても    */
/*  VRAM は 16 ビットバス接続であるため、32 ビットアクセスをしても 16 ビット  */
/*  2 回分の時間がかかります」。rep movsd はバス時間を縮めないが、発行する    */
/*  命令数は半分になる (票 5)。                                              */
/*  カウンタは 1 バイト/画素で加算する = 全画面で 640*480 = 307200 (完了条件)。*/
/* ------------------------------------------------------------------------ */
static void pegc_present_rect(int x, int y, int w, int h)
{
    u8 *src;
    u8 *dst;
    int row;
    u32 bytes;

    if (!s_active || s_bb_phys == 0) return;

    if (!gfx_clip_screen(&x, &y, &w, &h, PEGC_WIDTH, PEGC_HEIGHT_480)) return;

    src = (u8 *)P2V(s_bb_phys) + (u32)y * PEGC_PITCH + (u32)x;
    dst = (u8 *)P2V(PEGC_LINEAR_BASE) + (u32)y * PEGC_PITCH + (u32)x;
    bytes = (u32)w;

    for (row = 0; row < h; row++) {
        /* 先頭の端数 → dword 単位 → 末尾の端数。x と w が 4 の倍数なら
         * 端数ループは 0 回で、丸ごと rep movsd になる。 */
        u32 head = ((4u - ((u32)dst & 3u)) & 3u);
        u32 mid;
        u32 tail;
        if (head > bytes) head = bytes;
        mid  = (bytes - head) >> 2;
        tail = (bytes - head) - (mid << 2);

        if (head) kmemcpy(dst, src, head);
        if (mid)  _memcpy_d(dst + head, src + head, mid);
        if (tail) kmemcpy(dst + head + (mid << 2), src + head + (mid << 2),
                          tail);

        src += PEGC_PITCH;
        dst += PEGC_PITCH;
    }

    gfx_counters.present_bytes += bytes * (u32)h;   /* 1 バイト/画素 */
    gfx_counters.commits++;
}

/* ------------------------------------------------------------------------ */
/*  set_palette — 連続する count 項目を差し替える (rgb は 3B/項目)           */
/*  ハードウェアの並びは G, R, B ([U] パレットレジスタ) だが、HAL の引数は    */
/*  9801 と同じ R, G, B 順。                                                 */
/*                                                                          */
/*  引数の輝度は **KAPI のスケール = 0〜15** (契約 G6/G8 の GuiRgb)。         */
/*  gfx_set_palette (9801 互換の 1 色差し替え) も gfx_lease_palette (G8) も   */
/*  この単位で来るので、ここで一括して 8bit へ伸ばす (票 H2c)。               */
/*  0〜15 のシステム色はシャドウ台帳にも書き戻し、gfx_get_palette            */
/*  (= palette_get) が実際の色を返せるようにしておく。                        */
/* ------------------------------------------------------------------------ */
static void pegc_set_palette(int first, int count, const u8 *rgb)
{
    int k;
    if (!rgb) return;
    for (k = 0; k < count; k++) {
        int idx = first + k;
        u8 r = rgb[k * 3 + 0];
        u8 g = rgb[k * 3 + 1];
        u8 b = rgb[k * 3 + 2];
        if (idx < 0 || idx >= PEGC_PALETTE_COUNT) continue;
        pegc_write_palette(idx, pegc_pal8(r), pegc_pal8(g), pegc_pal8(b));
        if (idx < PALETTE_COUNT) palette_shadow_set(idx, r, g, b);
    }
}

/* ------------------------------------------------------------------------ */
/*  enter / leave — 表示出力の切替。PEGC は本体のグラフィック出力そのもの    */
/*  なのでリレーが無く、両方とも空 (9801 と同じ)。表示リレーを持つのは       */
/*  H3 のアクセラレータだけ (DESIGN §8)。                                    */
/* ------------------------------------------------------------------------ */
static void pegc_enter(void) { }
static void pegc_leave(void) { }

/* ------------------------------------------------------------------------ */
/*  shutdown — 標準グラフィックモード / 起動時の周波数のテキストへ戻す        */
/*                                                                          */
/*  リニア窓を閉じる → (pegc_apply_timing が) 拡張モードを抜ける → 起動時の   */
/*  周波数・クロックの 400 ラインの表示モード → 400 ライン VRAM 構成、の順。  */
/*  E0000h の意味が「制御レジスタ」から「プレーン 3」へ戻るのは拡張モードを   */
/*  抜けた瞬間なので、MMIO への最後の書き込みはその前に済ませておく。        */
/*  ※ 周波数は **起動時に BIOS が作っていた値** へ戻す (pegc_boot_sync_record */
/*  の記録。24kHz 決め打ちで CUI がずれた件 — TASK_FDC_REALHW §9-1)。         */
/*  s_active が 0 のとき (480 ラインへ入っていない) は何もしない。           */
/*  戻り方は実機の ROM の back の列と同じ (pegc_apply_timing)。ROM は両 GDC を  */
/*  止めたまま返すので、OS32 はテキスト GDC だけを START してコンソールを残す  */
/*  (グラフィック GDC は止めたまま — 9801 経路の gfx_init() が改めて START を  */
/*  出す、従来の挙動)。                                                      */
/* ------------------------------------------------------------------------ */
/* 起動時の周波数 hs とクロックの 400 ラインへ。pegc_shutdown と
 * pegc_restore_text_sync が共有する後半。
 * クロックと PITCH は**起動時に記録したクロック**へ戻す (480 ラインへ入る側と
 * 対称。pegc_boot_sync_record)。記録が無い (PEGC の probe が通っていない =
 * 09A0h を読んでいない) ときは触らない — 従来どおり。
 * グラフィック GDC の SYNC と SCROLL (IM) もクロックとの組で選ぶ (pegc.h §10、
 * Codex レビュー P2): 起動時が 5MHz (両方) なら 5MHz 用、2.5MHz なら 2.5MHz 用。
 * 記録が無いときは従来の組 (5MHz 用 SYNC + IM=0) で、クロックと PITCH は触らない。
 * 実機の ROM の列はテキスト GDC の CSRFORM を「カーソル非表示」(CS = 0) で
 * 入れるので、CUI (GUI のシンクが無効) ならコンソールのカーソルを戻す。GUI
 * から抜けるときは console_text_gdc_start() が同じことをする。 */
static void pegc_text_sync_400(u8 hs)
{
    PegcTiming t;
    int known = (s_boot_clk1 >= 0 && s_boot_clk2 >= 0) ? 1 : 0;
    int is5m = (s_boot_clk1 == 1 && s_boot_clk2 == 1) ? 1 : 0;
    int ss5m = known ? is5m : 1;     /* 記録なし = 従来の 5MHz 用 SYNC */

    t.pegc = s_probe_ok;
    t.ext = PEGC_FF2_STD_GFX;
    t.vram = PEGC_FF2_VRAM_400L;
    t.xattr = (hs == PEGC_HSYNC_31KHZ) ? PEGC_XATTR_31KHZ : PEGC_XATTR_24KHZ;
    t.hsync = hs;
    t.set_clock = known;
    t.clk1 = (s_boot_clk1 == 1) ? PEGC_FF2_GDC_CLK1_5M : PEGC_FF2_GDC_CLK1_2M5;
    t.clk2 = (s_boot_clk2 == 1) ? PEGC_FF2_GDC_CLK2_5M : PEGC_FF2_GDC_CLK2_2M5;
    if (hs == PEGC_HSYNC_31KHZ) {
        t.msync = s_msync_400_31k;
        t.ssync = ss5m ? s_ssync_400_31k_5m : s_ssync_400_31k_2m5;
    } else {
        t.msync = s_msync_400;
        t.ssync = ss5m ? s_ssync_400_5m : s_ssync_400_2m5;
    }
    t.pitch = pegc_restore_pitch();
    t.scroll = (known && is5m) ? s_scroll_400_5m : s_scroll_400_2m5;
    pegc_apply_timing(&t);

    gdc_cmd(&s_gdc_text, PEGC_GDC_CMD_START2);
    if (!con_sink_is_enabled()) console_hw_cursor_enable();
}

static void pegc_shutdown(void)
{
    u8 hs;

    if (!s_active) return;

    mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_OFF);

    /* 480 ラインでだけ見えていた 26〜30 行目を消す。25 行までは
     * コンソールの内容なので触らない (init 側で一度消してある)。 */
    pegc_tvram_clear(TVRAM_ROWS, PEGC_TEXT_ROWS_480);

    /* 起動時に記録した周波数へ戻し、それに合う 400 ラインの表示モードを入れる
     * (24kHz 決め打ちをやめた。TASK_FDC_REALHW §9-1)。起動時の 09A8h が
     * 読めなかった機種は従来どおり 24kHz。 */
    hs = pegc_restore_hsync();
    pegc_text_sync_400(hs);

    gfx_current_height = GFX_HEIGHT;
    s_active = 0;
    {
        PegcDiag d;
        pegc_diag_take(&d);
        pegc_diag_print("enter", &s_diag_enter);
        pegc_diag_print("exit ", &d);
    }
}

/* ------------------------------------------------------------------------ */
/*  pegc_restore_text_sync — ROM で戻れなかったときの OS32 側の戻し          */
/*                                                                          */
/*  `v86 -g` (kernel/v86_gcap.c、票 TASK_PEGC480_REALHW §3 段 1) は実機の    */
/*  ROM の INT 18h AH=30h で 480 ラインへ入り、同じ AH=30h で元のモードへ     */
/*  戻す。その戻しが失敗した (AH≠05h・打ち切り・暴走) ときだけここへ来る。    */
/*  やることは pegc_shutdown の後半と同じ考え方 — 拡張モードを抜け、        */
/*  **採取の前に AH=31h で読んだ周波数** (hsync31) の 400 ラインの SYNC を   */
/*  入れ、グラフィック GDC を止める。24kHz 固定にはしない (CUI の桁ずれが     */
/*  再発する、TASK_FDC_REALHW §9-1)。                                        */
/*  リニア窓は拡張モードのときだけ閉じる (標準モードの E0000h はプレーン 3 で、*/
/*  書くと画素を 1 つ壊すだけだが意味が無い)。09A0h は PEGC の probe が通った */
/*  機種にしか無いので、読むのもそのときだけ。                              */
/* ------------------------------------------------------------------------ */
void pegc_restore_text_sync(int hsync31)
{
    /* 09A0h と 6Ah・6Eh は PEGC の probe が通った機種の値。9801 で ROM の戻しに
     * 失敗してここへ来ても書かない (代行レビュー P3-2。6Ah・6Eh は
     * pegc_apply_timing が t.pegc = s_probe_ok で見る)。 */
    if (s_probe_ok && pegc_stat(PEGC_STAT_SEL_GFXMODE)) {
        mmio_w16(PEGC_MMIO_LINEAR, PEGC_LINEAR_OFF);
    }
    pegc_text_sync_400(hsync31 ? PEGC_HSYNC_31KHZ : PEGC_HSYNC_24KHZ);

    gfx_current_height = GFX_HEIGHT;
    s_active = 0;
}

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
    pegc_enter,
    pegc_leave,
    (int (*)(int, int, int, int, u8))0,        /* fill_rect: CPU 実装へ */
    (int (*)(int, int, int, int, int, int))0,  /* blit:      CPU 実装へ */
    (u8 *)0,                  /* bb_base:  init() が埋める */
    (u32)PEGC_PITCH,          /* bb_pitch: 640 バイト/ライン */
    GFX_BB_PACKED8,
    0,                        /* bb_size:  init() が埋める */
    pegc_prepare              /* 起動時: 予約・写像・同期の記録だけ */
};
