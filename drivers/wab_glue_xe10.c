/* ======================================================================== */
/*  WAB_GLUE_XE10.C — PC-9821Xe10 内蔵アクセラレータのボードグルー (票 H3)   */
/*                                                                          */
/*  役割は 3 つだけ (DESIGN §6):                                             */
/*    1. VGA 論理レジスタ (3C0h 系) → PC-98 ポートの翻訳表                   */
/*    2. 2 段 I/O の制御レジスタ (0FAAh index / 0FABh data) — ID / VRAM 窓 / */
/*       映像リレー / MMIO 有効                                              */
/*    3. CPU から見える VRAM 窓の場所と大きさを申告する                      */
/*                                                                          */
/*  チップ (CL-GD5430) のレジスタ体系は一切知らない。逆にチップドライバは     */
/*  このファイルの番地を知らない (層を越えない: DESIGN §7-3)。               */
/*                                                                          */
/*  番地と出典はすべて include/wab_xe10.h。ここには生の数値を書かない。      */
/*                                                                          */
/*  ⚠ **触ってはいけないポート**: 40E1h / 42E1h / 46E8h / 51E1h / 5BE1h。    */
/*  メルコ WAB-S / WSN / I-O DATA GA-98NB という C バス製ボードのポートで、  */
/*  Xe10 のものではない (DESIGN §3.2、2026-09-04 訂正)。                     */
/* ======================================================================== */

#include "types.h"
#include "io.h"
#include "wab_glue.h"
#include "wab_glue_xe10.h"
#include "wab_xe10.h"

/* ------------------------------------------------------------------------ */
/*  2 段 I/O (0FAAh = 内部レジスタ番号 / 0FABh = データ)                     */
/*  出典 [W] I/O 0FAAh・0FABh。                                              */
/* ------------------------------------------------------------------------ */
static void ctrl_out(u8 index, u8 val)
{
    outp(WAB_XE10_INDEX_PORT, index);
    outp(WAB_XE10_DATA_PORT, val);
    wab_glue_xe10.io_count += 2;
}

static u8 ctrl_in(u8 index)
{
    u8 v;
    outp(WAB_XE10_INDEX_PORT, index);
    v = (u8)inp(WAB_XE10_DATA_PORT);
    wab_glue_xe10.io_count += 2;
    return v;
}

/* ------------------------------------------------------------------------ */
/*  probe — Xe10 内蔵 (ID 5Bh) が居るか                                      */
/*                                                                          */
/*  9801 や WAB 非搭載の 9821 では 0FAAh/0FABh は open bus で FFh が返る。    */
/*  「FFh でない」だけでは弱いので 2 段で確かめる:                           */
/*    (a) 0FAAh に書いた番号が 0FAAh から読み戻せる (インデックスラッチが     */
/*        実在する)。NP21/W も cirrusvga_ifaa で書いた値を返す。             */
/*    (b) レジスタ 00h が **5Bh ちょうど**。他機種 (58h Xe / 59h Cb /        */
/*        5Ah Cf / 5Ch Cb2 / 5Dh Cx2) も同じ CL-GD5430 だが窓の既定を        */
/*        検証していないので v1 では受け付けない (票 H3 §1)。                */
/*  副作用は「インデックスラッチが書き換わる」だけ = 表示にもモードにも      */
/*  影響しない (probe は副作用を残さない、の範囲)。                          */
/* ------------------------------------------------------------------------ */
static int xe10_probe(void)
{
    u8 idx_readback;
    u8 id;

    outp(WAB_XE10_INDEX_PORT, WAB_XE10_REG_ID);
    idx_readback = (u8)inp(WAB_XE10_INDEX_PORT);
    wab_glue_xe10.io_count += 2;
    if (idx_readback != WAB_XE10_REG_ID) return 0;

    id = (u8)inp(WAB_XE10_DATA_PORT);
    wab_glue_xe10.io_count++;
    if (id != WAB_XE10_ID_XE10) return 0;

    return 1;
}

/* ------------------------------------------------------------------------ */
/*  init — ボードを使える状態にする                                          */
/*                                                                          */
/*  1. Video Subsystem Enable (ネイティブ 0102h = FF82h bit0)。VGA チップの   */
/*     I/O を有効にする、資料どおりの手順 ([W] I/O FF82h)。                   */
/*     ⚠ NP21/W ではこの書き込みが **映像リレーの内部状態も動かす**          */
/*     (cirrusvga_off82: relaystateint = videoenable ? 0x2 : 0)。以降        */
/*     リレーは 0FABh レジスタ 03h が上書きするので、init 直後に一瞬          */
/*     アクセラレータ側の画面が出るだけで実害は無い (init は VRAM を          */
/*     クリアしてからモードを立てる)。                                       */
/*     ネイティブ 0094h に当たる 0904h は NP21/W が**登録していない**         */
/*     (cirrus_vga.c の iocore_attachout(0x904,…) はコメントアウト) ので     */
/*     書かない。資料上も「102Access Control」で、必須ではない。             */
/*  2. VRAM 窓を既定 (F60000h) に固定する。ITF が設定済みの値と同じなので     */
/*     実機では実質 no-op、NP21/W でも初期値と同じ                           */
/*     (pc98_cirrus_vga_initVRAMWindowAddr → VRAMWindowAddr2 = 0xf60000)。   */
/*  3. リニア窓 (レジスタ 02h) を FE000000h に開く (v3 の窓の帯)。バンク窓と違い   */
/*     VRAM オフセット 0 から 2MB が連続して見えるので、300KB のクライアント  */
/*     面を CPU から一望できる。ページを張るのはバックエンド (K の            */
/*     paging_map_phys) の仕事で、ここはレジスタを出すだけ。番地の根拠は      */
/*     include/wab_xe10.h §4。                                               */
/*  4. リレーと MMIO は落としたままにする。画面を奪うのは enter() の仕事。    */
/*     MMIO 窓は Xe10 では NP21/W に存在しない (pciMMIO_Addr = 0 のまま) ので */
/*     bit0 は 0 のまま = BLT レジスタは I/O 経由で設定する。                 */
/* ------------------------------------------------------------------------ */
/* リニア窓は v3 のデバイス窓の帯の中、しかも paging_init が静的に持つ PT
 * 1 枚 (帯の先頭 4MB) の中に、NP21/W が窓として出す全体 (4MB) ごと収まること。
 * 外へ出ると、exec の後の gfx init では PT を足せず窓を張れない。 */
STATIC_ASSERT(WAB_XE10_LINEARWIN_BASE >= MEM_DEVICE_APERTURE_BASE,
              xe10_linear_in_aperture_band);
STATIC_ASSERT(WAB_XE10_LINEARWIN_BASE - MEM_DEVICE_APERTURE_BASE +
              WAB_XE10_LINEARWIN_DECODE <= MEM_DEVICE_APERTURE_PDE_SIZE,
              xe10_linear_in_static_aperture_pt);
STATIC_ASSERT(WAB_XE10_LINEARWIN_SIZE <= WAB_XE10_LINEARWIN_DECODE,
              xe10_linear_draw_within_decode);
/* dat = FFh は NP21/W が捨てる (窓が開かない)。00h は「閉じる」。 */
STATIC_ASSERT(WAB_XE10_LINEARWIN_SEL != 0x00 && WAB_XE10_LINEARWIN_SEL != 0xFF,
              xe10_linear_sel_accepted);
STATIC_ASSERT(((u32)WAB_XE10_LINEARWIN_SEL << WAB_XE10_LINEARWIN_SHIFT) ==
              MEM_DEVICE_APERTURE_BASE, xe10_linear_sel_exact);

static void xe10_linear_enable(int on);

static void xe10_init(void)
{
    outp(WAB_XE10_POS102_PORT, WAB_XE10_POS102_ENABLE);
    wab_glue_xe10.io_count++;

    ctrl_out(WAB_XE10_REG_VRAMWIN, WAB_XE10_WIN_SEL);
    xe10_linear_enable(1);
    ctrl_out(WAB_XE10_REG_RELAY, 0);
}

/* ------------------------------------------------------------------------ */
/*  リニア窓の開閉 ([W] 0FABh レジスタ 02h / include/wab_xe10.h §4)           */
/*                                                                          */
/*  on  : dat = FEh → 物理 FE000000h から (v3 のデバイス窓の帯)。             */
/*  off : dat = 00h。⚠ NP21/W はこの値を捨てる                               */
/*        ([N] cirrusvga_ofab case 0x02 の `if(dat!=0x00 && dat!=0xff)`)      */
/*        ので、エミュレータ上では窓は開いたまま残る。実際の後始末は          */
/*        バックエンドがページを Not-Present に戻すことで行い、ここは実機     */
/*        向けの作法として書き込みだけ出す。                                 */
/* ------------------------------------------------------------------------ */
static void xe10_linear_enable(int on)
{
    ctrl_out(WAB_XE10_REG_LINEARWIN,
             on ? WAB_XE10_LINEARWIN_SEL : WAB_XE10_LINEARWIN_OFF);
}

/* ------------------------------------------------------------------------ */
/*  VGA 論理レジスタ ⇔ PC-98 ポートの翻訳 ([W] 各ポートの項)                  */
/*                                                                          */
/*  03C0h〜03CFh → 0CA0h + 下位ニブル                                        */
/*  03D0h〜03DFh → 0DA0h + 下位ニブル                                        */
/*  03B0h〜03BFh → 0BA0h + 下位ニブル                                        */
/*  それ以外は 0 を返す = 呼び出し側の誤りなのでアクセスしない (無関係な      */
/*  98 のポートを叩いて機械を壊さないため)。                                 */
/* ------------------------------------------------------------------------ */
static unsigned int xe10_map(u16 reg)
{
    unsigned int group = (unsigned int)reg & WAB_XE10_VGA_GROUP_MASK;
    unsigned int low   = (unsigned int)reg & WAB_XE10_VGA_LOW_MASK;

    if (group == 0x03C0) return WAB_XE10_VGA_3CX_BASE + low;
    if (group == 0x03D0) return WAB_XE10_VGA_3DX_BASE + low;
    if (group == 0x03B0) return WAB_XE10_VGA_3BX_BASE + low;
    return 0;
}

static void xe10_out(u16 reg, u8 val)
{
    unsigned int port = xe10_map(reg);
    if (port == 0) return;
    outp(port, val);
    wab_glue_xe10.io_count++;
}

static u8 xe10_in(u16 reg)
{
    unsigned int port = xe10_map(reg);
    if (port == 0) return 0xFF;
    wab_glue_xe10.io_count++;
    return (u8)inp(port);
}

/* ------------------------------------------------------------------------ */
/*  映像出力リレー ([W] 0FABh レジスタ 03h bit1)                             */
/*  1 = アクセラレータ出力 / 0 = 本体 (98 グラフィック) スルー。             */
/*  MMIO (bit0) は常に 0 — Xe10 には MMIO 窓が無い (上の init のコメント)。   */
/*                                                                          */
/*  ⚠ **leave() で必ず 0 に戻すこと**。戻さないと `gfxmode pc98` や CUI 復帰  */
/*  のあとも 98 側の画面がモニタに出ない。gfx_shutdown() → backend->leave()  */
/*  → ここ、が唯一の復帰経路 (gfx/gfx_core.c gfx_shutdown)。                 */
/* ------------------------------------------------------------------------ */
static void xe10_relay(int on)
{
    ctrl_out(WAB_XE10_REG_RELAY, on ? WAB_XE10_RELAY_ACCEL : 0);
}

static void xe10_mmio_enable(int on)
{
    /* リレーの状態を壊さないよう、bit1 は現在値を読んで保つ。 */
    u8 cur = ctrl_in(WAB_XE10_REG_RELAY) & WAB_XE10_RELAY_ACCEL;
    ctrl_out(WAB_XE10_REG_RELAY,
             (u8)(cur | (on ? WAB_XE10_RELAY_MMIO : 0)));
}

/* ------------------------------------------------------------------------ */
/*  グルーの表。                                                            */
/*  const ではない: io_count を実行時に積むため (契約 G7 のカウンタは        */
/*  バックエンドがこの差分を拾う)。                                          */
/* ------------------------------------------------------------------------ */
WabGlue wab_glue_xe10 = {
    "xe10",
    xe10_probe,
    xe10_init,
    xe10_out,
    xe10_in,
    xe10_relay,
    xe10_mmio_enable,
    (u32)WAB_XE10_WIN_BASE,
    (u32)WAB_XE10_WIN_SIZE,
    (u32)WAB_XE10_LINEARWIN_BASE,   /* リニア窓 FE000000h (v3 の窓の帯) */
    (u32)WAB_XE10_LINEARWIN_SIZE,   /* 2MB */
    xe10_linear_enable,
    (volatile u8 *)0,   /* MMIO 窓なし (NP21/W の Xe10 は pciMMIO_Addr = 0) */
    0                   /* io_count */
};
