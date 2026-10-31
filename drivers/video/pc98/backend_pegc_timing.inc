/* GDC timing control and boot synchronization recording. */
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
        gfx_counters.io_accesses++;
    }
    gfx_counters.io_accesses++;
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
    kprintf(KLOG_ATTR, "[pegc] hsync=%s 09a8=%02x bios054c.b5=%d bios0459.b0=%d%s\n",
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
    kprintf(KLOG_ATTR, "[pegc] gdcclk=%s clk1=%d clk2=%d bios054d.b2=%d pitch400=%d\n",
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

