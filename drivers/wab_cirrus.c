/* ======================================================================== */
/*  WAB_CIRRUS.C — Cirrus Logic CL-GD54xx チップドライバ (票 H3 作業 2)      */
/*                                                                          */
/*  **PC-98 のポート番号を 1 つも含まない**。レジスタは VGA の論理番号        */
/*  (3C4h/3C5h = SR, 3CEh/3CFh = GR, 3D4h/3D5h = CR, 3C8h/3C9h = DAC) で     */
/*  指定し、glue->out() / glue->in() が機種ごとのポートへ翻訳する            */
/*  (DESIGN §6, §7-3)。                                                      */
/*                                                                          */
/*  [HW1] (EGC/GRCG/GDC 描画禁止) は 98 標準グラフィックにだけかかる規則で、  */
/*  アクセラレータの 2D エンジンは使ってよい (DESIGN B3 / §7-5)。            */
/*                                                                          */
/*  出典:                                                                    */
/*    [W]  docs/hw/undocumented/io_wab.md — SR / GR / CR のインデックス表     */
/*         (0CA5h の Sequencer Index 表、0CAFh の Graphics Controller Index  */
/*         表 = BLT レジスタ 20h〜39h、0DA5h の CRTC Index 表)               */
/*    [N]  NP21/W ai-debug fork ~/np21w-src/src/wab/cirrus_vga.c             */
/*         cirrus_bitblt_start() (GR20〜GR32 のラッチ)、cirrus_write_bitblt()*/
/*         (GR31 の起動条件)、cirrus_get_bpp()/cirrus_get_resolution()       */
/*         (SR7 と CR01/CR07/CR12 で解像度が決まる)、cirrusvga_drawGraphic() */
/*         (ピッチ = CR13|CR1B bit4、表示先頭 = CR0C/0D/1B/1D ×4)、          */
/*         cirrus_linear_memwnd_addr_convert() (GR09/GR0B のバンク)、         */
/*         cirrus_vga_rop2.h cirrus_colorexpand_pattern_* (8 バイトの         */
/*         モノクロパターンで塗る)                                           */
/*                                                                          */
/*  ⚠ NP21/W が実機と違う点 (どちらに合わせたかは各所に明記):                */
/*    - **塗りつぶし専用機能 (GR33 bit2 SOLIDFILL) は使えない**。            */
/*      cirrus_bitblt_start() は device_id != CL-GD5446 のとき                */
/*      cirrus_blt_modeext を 0 に固定するため。Xe10 は CL-GD5430 なので     */
/*      該当する。→ 全ビット 1 の 8x8 モノクロパターンの色展開で塗る         */
/*      (CL-GD5426 以来の定石で実機でも動く道)。                             */
/*    - **GR31 bit2 (RESET) を 1→0 にすると BLT が走る**                      */
/*      (cirrus_write_bitblt の非 5446 分岐)。RESET は一切立てない。          */
/*    - MMIO 窓は Xe10 では存在しない (glue->mmio == NULL) ので、BLT          */
/*      レジスタは I/O で設定する。DESIGN §8 は MMIO を勧めているが、        */
/*      NP21/W の Xe10 モードには窓が無い (PM 申し送り)。                    */
/* ======================================================================== */

#include "types.h"
#include "wab_glue.h"
#include "wab_cirrus.h"

/* ------------------------------------------------------------------------ */
/*  レジスタ番号 (Cirrus / VGA の資料上の番号。PC-98 のポートではない)        */
/* ------------------------------------------------------------------------ */
/* Sequencer ([W] 0CA5h の表) */
#define SR_RESET        0x00
#define SR_CLOCKING     0x01
#define SR_PLANEMASK    0x02
#define SR_CHARMAP      0x03
#define SR_MEMMODE      0x04
#define SR_UNLOCK       0x06  /* Unlock ALL Extensions */
#define SR_EXTMODE      0x07  /* Extended Sequencer Mode (画素形式) */
#define SR_DRAMCTL      0x0F

#define SR_UNLOCK_KEY   0x12  /* 12h を書くと解錠、読むと 12h */
#define SR_UNLOCK_LOCK  0x00  /* 12h 以外なら施錠 (読むと 0Fh) */
#define SR_UNLOCK_LOCKED_RB 0x0F

#define SR_MEMMODE_CHAIN4   0x08  /* bit3: パックドピクセル (1 バイト 1 画素) */
#define SR_MEMMODE_EXTMEM   0x02  /* bit1: 拡張メモリ有効 */
#define SR_MEMMODE_ODDEVEN  0x04  /* bit2: odd/even 無効化 */

#define SR_EXTMODE_SVGA     0x01  /* bit0: 1 = Cirrus SVGA / 0 = 標準 VGA */
#define SR_EXTMODE_BPPMASK  0x0E  /* bit3-1: 色数 (000b = 8bpp) */
#define SR_EXTMODE_BPP8     0x00

/* Graphics Controller ([W] 0CAFh の表) */
#define GR_SETRESET     0x00  /* 兼 BLT 背景色 (8bpp は下位 1 バイト) */
#define GR_SETRESET_EN  0x01  /* 兼 BLT 前景色 */
#define GR_MODE         0x05
#define GR_MISC         0x06
#define GR_OFFSET0      0x09  /* バンク 0 (CPU 窓) */
#define GR_OFFSET1      0x0A  /* バンク 1 (デュアルバンク時) */
#define GR_MODEEXT      0x0B  /* bit0 = デュアルバンク / bit5 = 16KB 粒度 */

#define GR_BLT_W_LO     0x20
#define GR_BLT_W_HI     0x21
#define GR_BLT_H_LO     0x22
#define GR_BLT_H_HI     0x23
#define GR_BLT_DPITCH_LO 0x24
#define GR_BLT_DPITCH_HI 0x25
#define GR_BLT_SPITCH_LO 0x26
#define GR_BLT_SPITCH_HI 0x27
#define GR_BLT_DST_LO   0x28
#define GR_BLT_DST_MID  0x29
#define GR_BLT_DST_HI   0x2A
#define GR_BLT_SRC_LO   0x2C
#define GR_BLT_SRC_MID  0x2D
#define GR_BLT_SRC_HI   0x2E
#define GR_BLT_WRMASK   0x2F  /* 左端スキップ画素数 (bit2-0)。必ず 0 にする */
#define GR_BLT_MODE     0x30
#define GR_BLT_STATUS   0x31
#define GR_BLT_ROP      0x32

/* GR30 (BLT mode) のビット ([N] cirrus_vga.c CIRRUS_BLTMODE_*) */
#define BLTMODE_BACKWARDS   0x01
#define BLTMODE_MEMSYSDEST  0x02
#define BLTMODE_MEMSYSSRC   0x04
#define BLTMODE_TRANSP      0x08
#define BLTMODE_PIXW8       0x00
#define BLTMODE_PATTERNCOPY 0x40
#define BLTMODE_COLOREXPAND 0x80

/* GR31 (BLT status/start) のビット ([N] CIRRUS_BLT_*) */
#define BLT_BUSY        0x01
#define BLT_START       0x02
#define BLT_RESET       0x04  /* **立てない** (1→0 で暴発する) */

/* GR32 (ROP) — 使うのは「転送元そのまま」だけ ([N] CIRRUS_ROP_SRC) */
#define BLT_ROP_SRC     0x0D

/* GR0B のビット */
#define GR_MODEEXT_DUALBANK 0x01
#define GR_MODEEXT_GRAN16K  0x20

/* CRTC ([W] 0DA5h の表) */
#define CR_H_TOTAL      0x00
#define CR_H_DISPEND    0x01
#define CR_H_BLANKSTART 0x02
#define CR_H_BLANKEND   0x03
#define CR_H_SYNCSTART  0x04
#define CR_H_SYNCEND    0x05
#define CR_V_TOTAL      0x06
#define CR_OVERFLOW     0x07
#define CR_PRESETROW    0x08
#define CR_CELLHEIGHT   0x09
#define CR_CURSOR_START 0x0A
#define CR_CURSOR_END   0x0B
#define CR_START_HI     0x0C
#define CR_START_LO     0x0D
#define CR_V_SYNCSTART  0x10
#define CR_V_SYNCEND    0x11  /* bit7 = CR00〜CR07 の書き込み保護 */
#define CR_V_DISPEND    0x12
#define CR_OFFSET       0x13  /* ピッチ / 8 の下位 8 ビット */
#define CR_UNDERLINE    0x14
#define CR_V_BLANKSTART 0x15
#define CR_V_BLANKEND   0x16
#define CR_MODECTL      0x17
#define CR_LINECOMPARE  0x18
#define CR_INTERLACE_END 0x19
#define CR_INTERLACE    0x1A  /* bit0 = インタレース (表示高さが 2 倍になる) */
#define CR_EXT_DISP     0x1B  /* bit0,2,3 = 表示先頭の上位 / bit4 = ピッチ bit8 */
#define CR_OVERLAY      0x1D  /* bit7 = 表示先頭 bit19 */
#define CR_CHIP_ID      0x27  /* R: チップ ID */

#define CR_V_SYNCEND_PROTECT 0x80
#define CR_OVERFLOW_VDE8     0x02
#define CR_OVERFLOW_VDE9     0x40
#define CR_EXT_DISP_PITCH8   0x10

/* Miscellaneous Output (3C2h write)。
 * bit0 = 1: CRTC を 3D4h/3D5h 側に置く (0 だと 3B4h/3B5h)。
 * bit1 = 1: CPU から VRAM をアクセス可。
 * ⚠ NP21/W はリセット後 msr = 0 なので、**これを立てないと 0DA4h/0DA5h が
 * 死んでいる** ([N] vga_ioport_write の 3b0-3bf / 3d0-3df ガード)。 */
#define MSR_640X480     0xE3

/* DAC の輝度は VGA 標準の 6bit ([N] vga_int.h c6_to_8: v &= 0x3f) */
#define DAC_MAX6        63

/* ------------------------------------------------------------------------ */
/*  インデックス付きレジスタの読み書き                                       */
/* ------------------------------------------------------------------------ */
static void sr_w(WabGlue *g, u8 idx, u8 val)
{
    g->out(VGA_SEQ_IDX, idx);
    g->out(VGA_SEQ_DATA, val);
}

static u8 sr_r(WabGlue *g, u8 idx)
{
    g->out(VGA_SEQ_IDX, idx);
    return g->in(VGA_SEQ_DATA);
}

static void gr_w(WabGlue *g, u8 idx, u8 val)
{
    g->out(VGA_GRC_IDX, idx);
    g->out(VGA_GRC_DATA, val);
}

static u8 gr_r(WabGlue *g, u8 idx)
{
    g->out(VGA_GRC_IDX, idx);
    return g->in(VGA_GRC_DATA);
}

static void cr_w(WabGlue *g, u8 idx, u8 val)
{
    g->out(VGA_CRTC_IDX, idx);
    g->out(VGA_CRTC_DATA, val);
}

static u8 cr_r(WabGlue *g, u8 idx)
{
    g->out(VGA_CRTC_IDX, idx);
    return g->in(VGA_CRTC_DATA);
}

/* ------------------------------------------------------------------------ */
/*  内部状態 (チップは 1 個しか居ない前提。9821 内蔵 1 枚 = 票 H3 の範囲)    */
/* ------------------------------------------------------------------------ */
static u32 s_bank_base   = 0xFFFFFFFFUL; /* 現在のバンクの先頭 VRAM オフセット */
static u32 s_pattern_off = 0;            /* 塗りつぶし用 8x8 パターンの位置 */
static u8  s_chip_id     = 0;

/* ------------------------------------------------------------------------ */
/*  probe                                                                    */
/*                                                                          */
/*  SR6 は Cirrus の拡張レジスタ解錠キー。12h を書くと 12h が読め、それ以外   */
/*  を書くと 0Fh が読める、という挙動そのものが Cirrus の指紋になる           */
/*  ([N] cirrus_hook_write_sr / cirrus_hook_read_sr case 0x06)。             */
/*  「0Fh → 12h → 0Fh」と往復するところまで見て、open bus を弾く。           */
/*  最後は解錠したまま抜ける (以降の設定で拡張レジスタを使うため)。          */
/* ------------------------------------------------------------------------ */
int wab_cirrus_probe(WabGlue *g)
{
    u8 locked, unlocked;

    if (!g || !g->out || !g->in) return 0;

    sr_w(g, SR_UNLOCK, SR_UNLOCK_LOCK);
    locked = sr_r(g, SR_UNLOCK);
    sr_w(g, SR_UNLOCK, SR_UNLOCK_KEY);
    unlocked = sr_r(g, SR_UNLOCK);

    if (locked != SR_UNLOCK_LOCKED_RB) return 0;
    if (unlocked != SR_UNLOCK_KEY) return 0;

    /* 解錠したので CR27 (チップ ID) が読める。値は機種で変わる
     * (Xe10 = CL-GD5430 → 0A0h) ので一致は求めず、記録だけしておく。 */
    s_chip_id = cr_r(g, CR_CHIP_ID);
    return 1;
}

u8 wab_cirrus_chip_id(WabGlue *g)
{
    (void)g;
    return s_chip_id;
}

/* ------------------------------------------------------------------------ */
/*  CPU 窓のバンク切替                                                       */
/*                                                                          */
/*  単一バンク・4KB 粒度 (GR0B = 0) に固定する。窓の大きさはボードが決める   */
/*  (glue->win_size)。GR09 が窓の先頭に当たる VRAM オフセット / 4096。       */
/*  ([N] cirrus_linear_memwnd_addr_convert: offset = gr[0x09]; 粒度は        */
/*   gr[0x0b] bit5 で 12/14 ビットシフト)                                    */
/*                                                                          */
/*  同じバンクなら OUT を出さない。小さい矩形を CPU で書くとき、行ごとに     */
/*  バンクを叩き直すと設定コストが本体を上回る (DESIGN §8)。                 */
/* ------------------------------------------------------------------------ */
u32 wab_cirrus_set_bank(WabGlue *g, u32 vram_off)
{
    u32 base = vram_off & ~(WAB_CIRRUS_BANK_GRAN - 1UL);

    if (base != s_bank_base) {
        gr_w(g, GR_MODEEXT, 0);                       /* 単一バンク / 4KB 粒度 */
        gr_w(g, GR_OFFSET0, (u8)(base / WAB_CIRRUS_BANK_GRAN));
        gr_w(g, GR_OFFSET1, (u8)(base / WAB_CIRRUS_BANK_GRAN));
        s_bank_base = base;
    }
    return vram_off - base;
}

/* ------------------------------------------------------------------------ */
/*  DAC パレット (3C8h に索引、3C9h に R,G,B を 3 回)                        */
/*  輝度は 6bit。8bit を書いても上位 2 ビットは表示時に捨てられる            */
/*  ([N] vga_int.h c6_to_8)。                                                */
/* ------------------------------------------------------------------------ */
void wab_cirrus_dac_set(WabGlue *g, int idx, u8 r6, u8 g6, u8 b6)
{
    g->out(VGA_DAC_WIDX, (u8)idx);
    g->out(VGA_DAC_DATA, (u8)(r6 & DAC_MAX6));
    g->out(VGA_DAC_DATA, (u8)(g6 & DAC_MAX6));
    g->out(VGA_DAC_DATA, (u8)(b6 & DAC_MAX6));
}

void wab_cirrus_set_fill_pattern(WabGlue *g, u32 vram_off)
{
    (void)g;
    /* 8 バイト境界に落とす。色展開パターンの開始行は srcaddr の下位 3 ビットで
     * 決まる ([N] cirrus_colorexpand_pattern_*: pattern_y = srcaddr & 7)。 */
    s_pattern_off = vram_off & ~7UL;
}

/* ------------------------------------------------------------------------ */
/*  モード設定 — 640x480 / 8bpp パックドピクセル                             */
/*                                                                          */
/*  NP21/W が実際に見るのは以下だけ ([N] cirrus_get_bpp /                    */
/*  cirrus_get_resolution / cirrusvga_drawGraphic):                          */
/*    SR7 bit0 = 1 かつ bit3-1 = 000b  → 8bpp (bit0 が 0 だと「描かない」)   */
/*    SR4 bit3 = 1 (chain4)            → VRAM がバイト線形に見える           */
/*    GR0B = 0                          → リニア変換で余計なシフトが入らない */
/*    CR01                              → 幅 = (CR01+1)*8                    */
/*    CR12 + CR07 bit1,6                → 高さ = 値+1                        */
/*    CR1A bit0 = 0                     → インタレースでない (高さ 2 倍防止) */
/*    CR13 + CR1B bit4                  → ピッチ = 値*8 バイト               */
/*    CR0C/CR0D/CR1B bit0,2,3/CR1D bit7 → 表示先頭 (**×4 される**)           */
/*    3C2h bit0 = 1                     → CRTC が 3D4h/3D5h に出る           */
/*  残りの CRTC タイミング (CR00/02〜06/08〜11/15〜17) は無視されるが、       */
/*  実機のために IBM VGA の 640x480 60Hz の標準値を入れておく。              */
/*                                                                          */
/*  Hidden DAC (3C6h) は触らない。8bpp では既定値のままでよく、書き込みに     */
/*  は「4 回読んでから 1 回書く」錠前の手順が要る。手順を踏み外すと           */
/*  標準 VGA では 3C6h = Pixel Mask なので、0 を書けば画面が真っ暗になる。    */
/*  NP21/W の錠前カウンタは初期値 5 で 3C8h を読むまで開かない               */
/*  ([N] cirrus_read_hidden_dac / vga_ioport_read case 0x3c8) — 触らないのが */
/*  両方にとって安全。                                                       */
/* ------------------------------------------------------------------------ */
int wab_cirrus_setup_8bpp(WabGlue *g, int width, int height,
                          u32 pitch, u32 start)
{
    u32 offset = pitch >> 3;   /* CR13 は「ピッチ / 8」 */
    u32 vde    = (u32)height - 1;
    u32 sa     = start >> 2;   /* 表示先頭は dword 単位 ([N] addroffset * 4) */
    u8  ov;

    if (!g || width <= 0 || height <= 0) return -1;
    if ((width & 7) != 0) return -1;               /* 幅は 8 の倍数 */
    if ((pitch & 7) != 0) return -1;               /* ピッチも 8 の倍数 */
    if (offset > 0x1FF) return -1;                 /* CR13 + CR1B bit4 で 9bit */
    if (vde > 0x3FF) return -1;

    /* --- チップの入口 --- */
    g->out(VGA_MISC_W, MSR_640X480);
    sr_w(g, SR_UNLOCK, SR_UNLOCK_KEY);

    /* --- Sequencer --- */
    sr_w(g, SR_RESET, 0x03);        /* 同期/非同期リセット解除 */
    sr_w(g, SR_CLOCKING, 0x01);     /* 8 ドット文字クロック / 画面 ON */
    sr_w(g, SR_PLANEMASK, 0x0F);    /* 全プレーン書き込み可 */
    sr_w(g, SR_CHARMAP, 0x00);
    sr_w(g, SR_MEMMODE,
         (u8)(SR_MEMMODE_EXTMEM | SR_MEMMODE_ODDEVEN | SR_MEMMODE_CHAIN4));
    /* SR7: 8bpp の Cirrus SVGA モード。**上位ニブルは残す** —
     * NP21/W は Xe10 では下位ニブルしか書き換えさせない
     * ([N] cirrus_hook_write_sr の reg_index==0x07 && gd54xxtype<0xff 分岐)
     * し、実機ではそこが ISA 窓アドレスなので壊してはいけない。 */
    sr_w(g, SR_EXTMODE,
         (u8)((sr_r(g, SR_EXTMODE) & (u8)~(SR_EXTMODE_BPPMASK | SR_EXTMODE_SVGA))
              | SR_EXTMODE_SVGA | SR_EXTMODE_BPP8));

    /* --- Graphics Controller --- */
    gr_w(g, GR_SETRESET, 0x00);
    gr_w(g, GR_SETRESET_EN, 0x00);
    gr_w(g, 0x02, 0x00);            /* Color Compare */
    gr_w(g, 0x03, 0x00);            /* Data Rotate */
    gr_w(g, 0x04, 0x00);            /* Read Map Select */
    gr_w(g, GR_MODE, 0x40);         /* 256 色シフト */
    gr_w(g, GR_MISC, 0x01);         /* グラフィックモード */
    gr_w(g, 0x07, 0x0F);            /* Color Don't Care */
    gr_w(g, 0x08, 0xFF);            /* Bit Mask */
    gr_w(g, GR_MODEEXT, 0x00);      /* 単一バンク / 4KB 粒度 */
    gr_w(g, GR_OFFSET0, 0x00);
    gr_w(g, GR_OFFSET1, 0x00);
    gr_w(g, GR_BLT_WRMASK, 0x00);   /* 左端スキップ 0 (色展開が参照する) */
    s_bank_base = 0;

    /* --- CRTC --- */
    /* CR11 bit7 を落としてから CR00〜CR07 を書く (VGA の書き込み保護)。 */
    cr_w(g, CR_V_SYNCEND, 0x0C);

    cr_w(g, CR_H_TOTAL,      0x5F);
    cr_w(g, CR_H_DISPEND,    (u8)((width / 8) - 1));
    cr_w(g, CR_H_BLANKSTART, 0x50);
    cr_w(g, CR_H_BLANKEND,   0x82);
    cr_w(g, CR_H_SYNCSTART,  0x54);
    cr_w(g, CR_H_SYNCEND,    0x80);
    cr_w(g, CR_V_TOTAL,      0x0B);

    ov = 0x3E & (u8)~(CR_OVERFLOW_VDE8 | CR_OVERFLOW_VDE9);
    if (vde & 0x100) ov |= CR_OVERFLOW_VDE8;
    if (vde & 0x200) ov |= CR_OVERFLOW_VDE9;
    cr_w(g, CR_OVERFLOW, ov);

    cr_w(g, CR_PRESETROW,    0x00);
    cr_w(g, CR_CELLHEIGHT,   0x40);   /* 行倍化なし */
    cr_w(g, CR_CURSOR_START, 0x00);
    cr_w(g, CR_CURSOR_END,   0x00);
    cr_w(g, CR_START_HI,     (u8)((sa >> 8) & 0xFF));
    cr_w(g, CR_START_LO,     (u8)(sa & 0xFF));
    cr_w(g, CR_V_SYNCSTART,  0xEA);
    cr_w(g, CR_V_DISPEND,    (u8)(vde & 0xFF));
    cr_w(g, CR_OFFSET,       (u8)(offset & 0xFF));
    cr_w(g, CR_UNDERLINE,    0x00);
    cr_w(g, CR_V_BLANKSTART, 0xE7);
    cr_w(g, CR_V_BLANKEND,   0x04);
    cr_w(g, CR_MODECTL,      0xC3);
    cr_w(g, CR_LINECOMPARE,  0xFF);
    cr_w(g, CR_INTERLACE_END, 0x00);
    cr_w(g, CR_INTERLACE,    0x00);   /* bit0 = 0: 高さが 2 倍にならない */

    /* CR1B: 表示先頭の上位 (bit0 = A16, bit3,2 = A18,A17) とピッチ bit8。
     * bit1 は Cirrus の拡張表示アドレス有効。表示先頭 0 / ピッチ 640 の
     * 現状ではどれも 0 だが、式どおりに組み立てておく。 */
    {
        u8 b1b = 0x02;
        if (sa & 0x10000UL) b1b |= 0x01;
        b1b |= (u8)(((sa >> 17) & 0x03) << 2);
        if (offset & 0x100) b1b |= CR_EXT_DISP_PITCH8;
        cr_w(g, CR_EXT_DISP, b1b);
    }
    cr_w(g, CR_OVERLAY, (u8)((sa & 0x80000UL) ? 0x80 : 0x00));

    /* --- Attribute Controller ---
     * 3C0h は「索引 → データ」を 1 本のポートで交互に出す。トグルの位相は
     * 3DAh を読むとリセットされる。最後に索引 20h を書いて画面出力を許可する
     * (VGA の作法。NP21/W も vga_ioport_write の 3c0 で同じ実装)。 */
    (void)g->in(VGA_STATUS1);
    {
        int i;
        for (i = 0; i < 16; i++) {
            g->out(VGA_ATTR_IDX, (u8)i);
            g->out(VGA_ATTR_IDX, (u8)i);
        }
        g->out(VGA_ATTR_IDX, 0x10); g->out(VGA_ATTR_IDX, 0x41); /* 256 色 */
        g->out(VGA_ATTR_IDX, 0x11); g->out(VGA_ATTR_IDX, 0x00);
        g->out(VGA_ATTR_IDX, 0x12); g->out(VGA_ATTR_IDX, 0x0F);
        g->out(VGA_ATTR_IDX, 0x13); g->out(VGA_ATTR_IDX, 0x00);
        g->out(VGA_ATTR_IDX, 0x14); g->out(VGA_ATTR_IDX, 0x00);
    }
    g->out(VGA_ATTR_IDX, 0x20);   /* パレットアドレスソース = 表示 ON */

    return 0;
}

void wab_cirrus_shutdown(WabGlue *g)
{
    if (!g) return;
    /* 拡張モードを抜けて標準 VGA へ。表示リレーを 98 側へ戻すのはグルーの
     * 仕事なので、ここでは画を止めるだけにする。 */
    sr_w(g, SR_UNLOCK, SR_UNLOCK_KEY);
    sr_w(g, SR_EXTMODE, (u8)(sr_r(g, SR_EXTMODE) & (u8)~SR_EXTMODE_SVGA));
    sr_w(g, SR_UNLOCK, SR_UNLOCK_LOCK);
    s_bank_base = 0xFFFFFFFFUL;
}

/* ------------------------------------------------------------------------ */
/*  BLT エンジン                                                             */
/* ------------------------------------------------------------------------ */
int wab_cirrus_wait_idle(WabGlue *g)
{
    long n;
    for (n = 0; n < WAB_CIRRUS_BUSY_LIMIT; n++) {
        if (!(gr_r(g, GR_BLT_STATUS) & BLT_BUSY)) return 0;
    }
    return -1;
}

/* 幅・高さ・ピッチ・アドレスをレジスタへ積む (共通部分)。
 * 幅と高さは「実値 - 1」を書く ([N] cirrus_bitblt_start が +1 する)。 */
static void blt_setup(WabGlue *g, u32 dst, u32 src,
                      u32 dpitch, u32 spitch, int w, int h)
{
    u32 wm1 = (u32)w - 1;
    u32 hm1 = (u32)h - 1;

    gr_w(g, GR_BLT_W_LO,  (u8)(wm1 & 0xFF));
    gr_w(g, GR_BLT_W_HI,  (u8)((wm1 >> 8) & 0x1F));
    gr_w(g, GR_BLT_H_LO,  (u8)(hm1 & 0xFF));
    gr_w(g, GR_BLT_H_HI,  (u8)((hm1 >> 8) & 0x1F));
    gr_w(g, GR_BLT_DPITCH_LO, (u8)(dpitch & 0xFF));
    gr_w(g, GR_BLT_DPITCH_HI, (u8)((dpitch >> 8) & 0x1F));
    gr_w(g, GR_BLT_SPITCH_LO, (u8)(spitch & 0xFF));
    gr_w(g, GR_BLT_SPITCH_HI, (u8)((spitch >> 8) & 0x1F));
    gr_w(g, GR_BLT_DST_LO,  (u8)(dst & 0xFF));
    gr_w(g, GR_BLT_DST_MID, (u8)((dst >> 8) & 0xFF));
    gr_w(g, GR_BLT_DST_HI,  (u8)((dst >> 16) & 0x3F));
    gr_w(g, GR_BLT_SRC_LO,  (u8)(src & 0xFF));
    gr_w(g, GR_BLT_SRC_MID, (u8)((src >> 8) & 0xFF));
    gr_w(g, GR_BLT_SRC_HI,  (u8)((src >> 16) & 0x3F));
}

/* GR31 の START を 0→1 にして起動する。
 * RESET (bit2) は絶対に立てない — NP21/W の非 5446 分岐では 1→0 の
 * 変化そのものが BLT を暴発させる ([N] cirrus_write_bitblt)。
 * 先に 0 を書いてから 0x02 を書くのは、前回の START が残っていても
 * 確実に 0→1 の変化を作るため。 */
static void blt_go(WabGlue *g)
{
    gr_w(g, GR_BLT_STATUS, 0x00);
    gr_w(g, GR_BLT_STATUS, BLT_START);
}

/* 矩形塗りつぶし。
 * CL-GD5430 では専用の SOLIDFILL が使えない (冒頭の注記) ので、
 * 「全ビット 1 の 8x8 モノクロパターン」をパターンコピー + 色展開で
 * 敷き詰める。展開後の色は前景色 = GR01 なので、背景色 (GR00) にも
 * 同じ値を入れておけばパターンが化けても単色で埋まる。 */
int wab_cirrus_fill(WabGlue *g, u32 dst_off, u32 pitch,
                    int w, int h, u8 color)
{
    if (!g || w <= 0 || h <= 0) return -1;
    if (w > WAB_CIRRUS_BLT_MAX || h > WAB_CIRRUS_BLT_MAX) return -1;

    if (wab_cirrus_wait_idle(g) != 0) return -1;

    gr_w(g, GR_SETRESET, color);      /* BLT 背景色 (8bpp は下位 1 バイト) */
    gr_w(g, GR_SETRESET_EN, color);   /* BLT 前景色 */
    gr_w(g, GR_BLT_WRMASK, 0x00);
    blt_setup(g, dst_off, s_pattern_off, pitch, pitch, w, h);
    gr_w(g, GR_BLT_ROP, BLT_ROP_SRC);
    gr_w(g, GR_BLT_MODE,
         (u8)(BLTMODE_PATTERNCOPY | BLTMODE_COLOREXPAND | BLTMODE_PIXW8));
    blt_go(g);

    return wab_cirrus_wait_idle(g);
}

/* 矩形転送 (VRAM → VRAM)。
 * 同じ面の中で下へずらすときだけ矩形が重なって壊れるので、そのときは
 * 後方転送 (BLTMODE_BACKWARDS) にして末尾から運ぶ。後方転送では
 * アドレスは矩形の**右下**を指す ([N] cirrus_do_copy のコメントと
 * cirrus_bkwd_rop)。 */
int wab_cirrus_copy(WabGlue *g, u32 dst_off, u32 src_off,
                    u32 dpitch, u32 spitch, int w, int h)
{
    int backwards;
    u32 d = dst_off;
    u32 s = src_off;

    if (!g || w <= 0 || h <= 0) return -1;
    if (w > WAB_CIRRUS_BLT_MAX || h > WAB_CIRRUS_BLT_MAX) return -1;

    backwards = (dst_off > src_off &&
                 dst_off < src_off + spitch * (u32)h) ? 1 : 0;
    if (backwards) {
        d += dpitch * (u32)(h - 1) + (u32)(w - 1);
        s += spitch * (u32)(h - 1) + (u32)(w - 1);
    }

    if (wab_cirrus_wait_idle(g) != 0) return -1;

    gr_w(g, GR_BLT_WRMASK, 0x00);
    blt_setup(g, d, s, dpitch, spitch, w, h);
    gr_w(g, GR_BLT_ROP, BLT_ROP_SRC);
    gr_w(g, GR_BLT_MODE,
         (u8)(BLTMODE_PIXW8 | (backwards ? BLTMODE_BACKWARDS : 0)));
    blt_go(g);

    return wab_cirrus_wait_idle(g);
}
