/* ======================================================================== */
/*  V86_IO.C — V86 ゲストの I/O ポリシーとポートディスパッチ                */
/* ======================================================================== */

#include "v86_io.h"
#include "v86_pic.h"
#include "v86_kbd.h"
#include "tss.h"
#include "pc98.h"
#include "io.h"
#include "gfx.h"        /* PAL_IDX_PORT / PAL_G_PORT / PAL_R_PORT / PAL_B_PORT */
#include "palette.h"    /* palette_init() — セッション終了時の復帰 */
#include "console.h"    /* console_hw_cursor_enable() — DOS が変えた CSRFORM を戻す */
#include "snd_engine.h" /* snd_set_master() — FM をゲストに明け渡す */
#include "v86_gcap.h"   /* `v86 -g` の採取 (票 TASK_PEGC480_REALHW §3 段 1) */
#include "v86_gcap_math.h" /* v86g_port_passed */

/* GDC はコマンドとパラメータの間に I/O ウェイトが要る (gfx_internal.h と同じ) */
static void io_out(unsigned int port, unsigned int val)
{
    outp(port, val);
    io_wait();
}

static u32 io_trap_n = 0;
static u16 io_last_port = 0;

u32 v86_io_trap_count(void) { return io_trap_n; }
u16 v86_io_last_port(void)  { return io_last_port; }

/* ------------------------------------------------------------------------ */
/*  I/O トラップログ                                                        */
/*                                                                          */
/*  カウンタと「最後に触ったポート」だけでは、ポーリングループの中身が      */
/*  分からない。INT 1Bh でも同じところで詰まり、呼び出しログを並べて初めて  */
/*  原因が確定した (docs/tasks/v86v2/05 §4-2)。同じ手をここにも置く。       */
/*                                                                          */
/*  同じ組み合わせは積まずに回数だけ数える。**直前 1 件だけ見るのでは       */
/*  足りない。** ゲストの割り込みハンドラは                                 */
/*    OUT 08,20 → OUT 08,0B → IN 08 → OUT 00,20 → OUT 71 x2               */
/*  のような数命令の輪を回し続けるので、隣どうしは必ず違う値になる。        */
/*  直近 8 件と突き合わせて初めて畳める。畳めないとリングが定常ループで      */
/*  埋まり、**知りたい初期化部分が押し出されて消える**。                    */
/* ------------------------------------------------------------------------ */
#define IOLOG_LOOKBACK  8
#define V86_IOLOG_N     64
u32 v86_iolog_w = 0;                    /* 積んだエントリ数 (回り込む前) */
u32 v86_iolog[V86_IOLOG_N * 4];
/*  [0] dir<<24 | size<<16 | port      dir: 0=IN, 1=OUT
 *  [1] value                          IN なら返した値、OUT なら書かれた値
 *  [2] cs<<16 | ip                    どの命令から来たか
 *  [3] 同じ内容が連続した回数 */

/* #GP ハンドラが記録しているゲストの CS:IP (kernel/v86.c) */
extern u32 v86_gp_cs;
extern u32 v86_gp_ip;

static void iolog(u16 port, int size, int dir, u32 value)
{
    u32 key = ((u32)dir << 24) | ((u32)size << 16) | port;
    u32 loc = ((v86_gp_cs & 0xFFFFU) << 16) | (v86_gp_ip & 0xFFFFU);
    u32 *e;

    {
        u32 back;
        for (back = 1; back <= IOLOG_LOOKBACK && back <= v86_iolog_w; back++) {
            e = &v86_iolog[((v86_iolog_w - back) % V86_IOLOG_N) * 4];
            if (e[0] == key && e[1] == value && e[2] == loc) {
                e[3]++;
                return;
            }
        }
    }
    e = &v86_iolog[(v86_iolog_w % V86_IOLOG_N) * 4];
    e[0] = key;
    e[1] = value;
    e[2] = loc;
    e[3] = 1;
    v86_iolog_w++;
}

/* ------------------------------------------------------------------------ */
/*  素通しにするポート                                                      */
/*                                                                          */
/*  ゲスト専用のデバイスと、トラップすると性能が破綻するもの。              */
/*  PC-98 では PIT (奇数 0x71-0x77) と CRTC (偶数 0x70-0x7A) が交互に        */
/*  並んでいるので、範囲でまとめて開けてはならない。                        */
/* ------------------------------------------------------------------------ */
struct io_range {
    u16 start;
    u16 end;        /* 含む */
    u8  step;       /* 1 = 連続, 2 = 1 つ飛ばし */
};

static const struct io_range v86_io_allow[] = {
    /* FM 音源 OPN (PC-9801-26K/86)。実測で I/O の 7〜8 割がここ。
     * 大半は書き込み前の BUSY ポーリングで、トラップしたら成立しない。 */
    { 0x0188, 0x018F, 1 },

    /* I/O ウェイト。ダミーライトでバスを待つだけなので害がない。 */
    { 0x005F, 0x005F, 1 },

    /* テキスト GDC (status/data) と VSYNC トリガ、モードフリップフロップ。
     * 0x0064 は実測で 52/s = 1 フレーム 1 回叩かれていた。 */
    { 0x0060, 0x0060, 1 },
    { 0x0062, 0x0062, 1 },
    { 0x0064, 0x0064, 1 },
    { 0x0068, 0x0068, 1 },
    { 0x006A, 0x006A, 1 },
    { 0x006C, 0x006C, 1 },
    { 0x006E, 0x006E, 1 },

    /* CRTC (偶数のみ。奇数は PIT なので開けない) */
    { 0x0070, 0x007A, 2 },

    /* GRCG */
    { 0x007C, 0x007E, 2 },

    /* グラフィック GDC / パレット / ページフリップ */
    { 0x00A0, 0x00AE, 2 },

    /* EGC */
    { 0x04A0, 0x04AF, 1 },

    /* ビープ (PIT ch1 のゲート) — 音が出るだけで OS32 に影響しない */
    { 0x0037, 0x0037, 1 },

    /* カレンダ */
    { 0x0020, 0x0020, 1 },
};

/* ------------------------------------------------------------------------ */
/*  仮想化 (= #GP で捕まえる) ポート                                        */
/*                                                                          */
/*  上の許可リストに入っていないものは既定で拒否なので、ここは              */
/*  「なぜ拒否なのか」を残すための表。コードとしては使っていない。          */
/*                                                                          */
/*    0x0000/0x0002  マスタ PIC   — OS32 の IRQ ルーティングを守る          */
/*    0x0008/0x000A  スレーブ PIC — 同上                                    */
/*    0x0071/0x0077  PIT ch0/mode — OS32 の 100Hz タイマを守る              */
/*    0x0041/0x0043  キーボード   — OS32 がスキャンコードを管理             */
/*    0x0090-0x0094  FDC          — ディスクイメージを仮想化する            */
/*    0x00BE         FDD 切替                                               */
/*    0x00F0         リセット     — ゲストにリセットさせない                */
/*    0x00F2         A20                                                    */
/*    0x0030-0x0035  RS-232C      — rshell の通信を守る                     */
/*    0x07EC/0x07EE  HostDrv      — ファイル I/O 経路を守る                 */
/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */
/*  グラフィックハードを「IPL 直後」の状態に戻してからゲストに渡す          */
/*                                                                          */
/*  ここを怠ると、**ゲストは自分が設定していない部分を OS32 の都合の状態の  */
/*  まま引き継ぐ**。ゲームは BIOS が用意した既定状態を前提に、必要な所だけ  */
/*  書き換えて動くので、前提が違うと画面だけが壊れる。                      */
/*                                                                          */
/*  実測 (Ys I): OS32 の gfx_init() は                                      */
/*    6Ah=16色 / 68h=400ライン / A4h=表示ページ0 / A6h=描画ページ1          */
/*  を設定したままにしていた。とくに **描画ページ 1 のまま渡す**のは実機の  */
/*  どの状態とも違う。素の NP21/W で同じイメージを起動して VRAM を比べると  */
/*  プレーンの中身はバイト単位で一致していたのに、画面だけが縦半分に潰れた  */
/*  絵が 2 枚並ぶ状態になっていた。描いた絵は正しく、見せ方だけが違う。      */
/*                                                                          */
/*  PC-9800 Bible (docs/PC9800Bible/2-7_グラフィック.md, 4-3_I_Oマップ.md): */
/*    68h  08h=高解像度(400ライン) / 09h=200ライン                          */
/*    6Ah  00h=8色 / 01h=16色                                              */
/*    A4h  表示ページ / A6h  描画ページ                                     */
/*    A2h  0Dh=表示開始 / 0Ch=表示停止                                      */
/* ------------------------------------------------------------------------ */
static void gfx_state_for_guest(void)
{
    u32 i;

    /* ゲストはグラフィックの設定を**一切しない**。
     *
     * 観測モード (v86_io_observe) で採った Ys のグラフィック I/O は
     * セッション全体でこの 4 回だけだった:
     *   OUT 62h,4Bh / OUT 62h,0Fh   テキスト GDC
     *   OUT A2h,0Dh x2              グラフィック表示開始
     *
     * 解像度もカラーモードもページも触らない = **BIOS が残した状態を
     * 丸ごと前提にしている**。だからこちらが BIOS 相当の初期状態を
     * 用意して渡す義務がある。
     *
     * 下の値は推測ではない。素の NP21/W で Ys を起動し、エミュレータ内部
     * の GDC 状態 (`emu_gdc`) を OS32 の V86 セッション中の同じ dump と
     * 突き合わせて採った。差は 4 つだけだった:
     *
     *   項目          素の NP21/W    OS32 の V86
     *   mode1 (68h)   0x99           0x89        bit4 = 200 ライン
     *   mode2 (6Ah)   0x00           0x01        8 色 / 16 色
     *   CSRFORM       01 00 00       00 00 00    LR = 2 ラスタ/行
     *   PRAM (SCROLL) 全部 0         LEN=400     表示分割の長さ
     *
     * 以前 68h だけ 200 ラインに戻して悪化したのは、CSRFORM を
     * 400 ライン用 (LR=1) のままにしていたから。mode1 bit4 は
     * 「奇数ラスタを捨てる」で、GDC 側の行あたりラスタ数とセットでないと
     * 飛び越し表示になる。**この 2 つは必ず一緒に変える。**
     * → docs/tasks/v86v2/07_gfx_state.md */

    /* 表示を止めてから触る。ゲストが自分で A2h=0Dh を出して開始する。 */
    io_out(GDC_GFX_CMD, GDC_CMD_STOP);

    io_out(MODE_FF2_PORT, MFF2_8COLOR);     /* 6Ah: 8 色モード */
    io_out(MODE_FF1_PORT, MFF1_200LINE);    /* 68h: 200 ライン */

    /* CSRFORM (4Bh): P1 の下位 5bit = LR-1。1 = 2 ラスタ/行 */
    io_out(GDC_GFX_CMD, GDC_GFX_400LINE);
    io_out(GDC_GFX_PARAM, 0x01);
    io_out(GDC_GFX_PARAM, 0x00);
    io_out(GDC_GFX_PARAM, 0x00);

    /* PRAM (70h): 表示分割を BIOS 直後と同じ「全部 0」に戻す。
     * OS32 は gfx_scroll_init() で LEN=400 を入れているので、
     * 消さないと 200 ライン表示と噛み合わない。 */
    io_out(GDC_GFX_CMD, GDC_CMD_SCROLL);
    for (i = 0; i < 8; i++) {
        io_out(GDC_GFX_PARAM, 0x00);
    }

    /* デジタルパレット (8 色モードの A8h/AAh/ACh/AEh)。
     * ゲストはパレットも設定しないので BIOS 既定値を入れる。
     * 素の NP21/W で Ys を起動したときの値と同じ:
     *   A8h=0x37  AAh=0x15  ACh=0x26  AEh=0x04
     * OS32 の palette_init() は 16 色アナログのつもりで同じポートを
     * 叩くため、8 色モードではこれが 0F0F0F0F に潰れて画面が
     * 白一色になる。 */
    io_out(PAL_IDX_PORT, 0x37);
    io_out(PAL_G_PORT,   0x15);
    io_out(PAL_R_PORT,   0x26);
    io_out(PAL_B_PORT,   0x04);

    io_out(GDC_DISP_PAGE, 0x00);
    io_out(GDC_ACCESS_PAGE, 0x00);
}

/* OS32 の描画環境を戻す。gfx_init() の末尾と同じ設定にする
 * (VRAM は消さない — ゲストが描いた内容を残すため)。 */
static void gfx_state_for_os32(void)
{
    u32 i;
    /* gfx_hardware_scroll(0) が 400 ライン・スクロール無しで出す値
     * (SAD=0 / SL=400 の 1 分割)。実測した OS32 側の PRAM と一致する。 */
    static const u8 pram_400[8] = { 0x00, 0x00, 0x00, 0x19,
                                    0x00, 0x00, 0x00, 0x00 };

    io_out(GDC_GFX_CMD, GDC_CMD_STOP);

    io_out(MODE_FF2_PORT, MFF2_16COLOR);
    io_out(MODE_FF1_PORT, MFF1_HIRES);

    io_out(GDC_GFX_CMD, GDC_GFX_400LINE);
    io_out(GDC_GFX_PARAM, 0x00);
    io_out(GDC_GFX_PARAM, 0x00);
    io_out(GDC_GFX_PARAM, 0x00);

    io_out(GDC_GFX_CMD, GDC_CMD_SCROLL);
    for (i = 0; i < 8; i++) {
        io_out(GDC_GFX_PARAM, pram_400[i]);
    }

    io_out(GDC_GFX_CMD, GDC_CMD_START);
    io_out(GDC_DISP_PAGE, 0x00);
    io_out(GDC_ACCESS_PAGE, 0x01);

    /* 16 色に戻した後でないとアナログパレットとして書き込まれない */
    palette_init();

    /* テキスト GDC のカーソル形状/位置もゲストが書き換えている
     * (DOS は DC=1 のブロックカーソルを最後の位置に残す)。OS32 の形に戻す。 */
    console_hw_cursor_enable();
}

/* ------------------------------------------------------------------------ */
/*  FM 音源をゲストに明け渡す                                               */
/*                                                                          */
/*  OPN のポート (0x188-0x18F) は素通しなので、OS32 の音源エンジンが        */
/*  動いたままだとゲストの音楽ドライバと同じチップを取り合う。              */
/*  `snd_tick()` は IRQ0 から毎回呼ばれ、BGM 再生中なら OPN に書く。        */
/*  しかも `bgm_persist` があるのでプログラムを跨いで鳴り続ける。            */
/*                                                                          */
/*  `snd_set_master(0)` は全チャンネルをキーオフしてから停止するので、      */
/*  鳴りっぱなしの音を残さずに明け渡せる。                                  */
/* ------------------------------------------------------------------------ */
static void snd_state_for_guest(void)
{
    snd_set_master(0);
}

/* ゲストが鳴らしっぱなしにした音を消してから OS32 に戻す。
 * `snd_set_master(0)` が全チャンネルのキーオフを兼ねているので、
 * 0 を経由してから 1 に戻す。これをやらないとセッション終了後も
 * ゲームの最後の和音が鳴り続ける (実測: 終了後も ch0-2 が発音中だった)。 */
static void snd_state_for_os32(void)
{
    snd_set_master(0);
    snd_set_master(1);
}

/* ------------------------------------------------------------------------ */
/*  観測モード — 素通しポートを「トラップして、実 I/O もする」              */
/*                                                                          */
/*  素通しにしたポートは #GP を起こさないので v86_iolog に何も残らない。    */
/*  ゲストがグラフィックをどう設定したのかを知りたいときに、この盲点が      */
/*  そのまま行き止まりになる。                                              */
/*                                                                          */
/*  そこで観測したいポートだけ I/O 許可ビットマップから落とし、             */
/*  ハンドラ側で **ログを取ってから実ポートへ中継する**。動作は素通しと     */
/*  同じままで、値と発行元 CS:IP が全部見えるようになる。                    */
/*                                                                          */
/*  常時有効にすると #GP が増えて重いので既定は 0。ホストから             */
/*    emu_write_mem addr=v86_io_observe hex=01000000                        */
/*  で立てて 1 セッションだけ観測する。 */
u32 v86_io_observe = 0;

static const struct io_range v86_io_observe_list[] = {
    { 0x0060, 0x006E, 2 },      /* テキスト GDC / モード F/F / ボーダー */
    { 0x0070, 0x007A, 2 },      /* CRTC */
    { 0x007C, 0x007E, 2 },      /* GRCG */
    { 0x00A0, 0x00AE, 2 },      /* グラフィック GDC / パレット / ページ */
    { 0x04A0, 0x04AF, 1 },      /* EGC */
};

static int io_is_observed(u16 port)
{
    u32 i;
    if (!v86_io_observe) {
        return 0;
    }
    for (i = 0; i < sizeof(v86_io_observe_list) /
                    sizeof(v86_io_observe_list[0]); i++) {
        const struct io_range *r = &v86_io_observe_list[i];
        u16 p;
        for (p = r->start; p <= r->end; p = (u16)(p + r->step)) {
            if (p == port) {
                return 1;
            }
        }
    }
    return 0;
}

void v86_io_apply_policy(void)
{
    u32 i;
    int capture = v86_gcap_active();

    io_trap_n = 0;
    io_last_port = 0;
    v86_iolog_w = 0;
    v86_pic_reset();
    v86_kbd_reset();
    /* `v86 -g` の採取は画面も音源も**触らずに**始める — 記録したいのは
     * 「今のモードから ROM が何を出すか」で、ここで 200 ライン等へ変えると
     * ROM が見る出発点が変わる。戻しも専用 (v86_io_reset_policy)。 */
    if (!capture) {
        gfx_state_for_guest();
        snd_state_for_guest();
    }

    tss_iomap_deny_all();

    for (i = 0; i < sizeof(v86_io_allow) / sizeof(v86_io_allow[0]); i++) {
        const struct io_range *r = &v86_io_allow[i];
        u32 p;
        for (p = r->start; p <= (u32)r->end; p += r->step) {
            tss_iomap_allow((u16)p);
        }
    }

    /* 採取中は「実機へ通して記録する」ポートを落とす (捕まえるため)。
     * 表の正本は v86g_port_passed (0x0A00 未満にしか無い)。観測モードは使わない。 */
    if (capture) {
        u32 p;
        for (p = 0; p < 0x0A00U; p++) {
            if (v86g_port_passed(p)) {
                tss_iomap_deny((u16)p);
            }
        }
        return;
    }

    /* 観測対象は許可リストの後で落とす (許可より観測が優先) */
    if (v86_io_observe) {
        for (i = 0; i < sizeof(v86_io_observe_list) /
                        sizeof(v86_io_observe_list[0]); i++) {
            const struct io_range *r = &v86_io_observe_list[i];
            u32 p;
            for (p = r->start; p <= (u32)r->end; p += r->step) {
                tss_iomap_deny((u16)p);
            }
        }
    }
}

void v86_io_reset_policy(void)
{
    tss_iomap_deny_all();
    /* 採取の戻しは v86_gcap.c が ROM の AH=30h と CUI の作り直しで行う。
     * gfx_state_for_os32 は 400 ライン・16 色へ決め打ちで戻すだけで、
     * 同期 (SYNC / 09A8h) を戻さない — ここで走らせると ROM の戻しを
     * 上書きする。音源は明け渡していないので戻すものが無い。 */
    if (v86_gcap_active()) {
        return;
    }
    gfx_state_for_os32();
    snd_state_for_os32();
}

/* ------------------------------------------------------------------------ */
/*  仮想化ポートのアクセス                                                  */
/*                                                                          */
/*  Phase 2 の時点では「ゲストを殺さずに素通りさせる」ことだけを保証する。  */
/*  読みは 0xFF (何も繋がっていないときの実機の値)、書きは破棄。            */
/*  PIC / PIT / FDC の実際の仮想化は Phase 3 で載せる。                     */
/* ------------------------------------------------------------------------ */
u32 v86_io_in(u16 port, int size)
{
    u32 v;

    if (v86_gcap_pass_in(port, size, &v)) {
        /* `v86 -g`: 実機から幅どおりに読んで数えた */
    } else if (io_is_observed(port)) {
        v = inp(port);                  /* 観測: 実ポートを読んで中継 */
    } else if (v86_pic_is_port(port)) {
        v = v86_pic_in(port);
    } else if (v86_kbd_is_port(port)) {
        v = v86_kbd_in(port);
    } else {
        v = (size == 1) ? 0xFFU : 0xFFFFU;
    }
    /* 採取中だけ・通さなかったポートだけ数える (通したものは上で数えた) */
    v86_gcap_note_emul_in(port, size, v);

    io_trap_n++;
    io_last_port = port;
    iolog(port, size, 0, v);
    return v;
}

void v86_io_out(u16 port, int size, u32 value)
{
    if (v86_gcap_pass_out(port, size, value)) {
        /* `v86 -g`: 記録してから実機へ幅どおりに書いた */
    } else if (io_is_observed(port)) {
        outp(port, value);              /* 観測: ログを取ってから実ポートへ */
    } else if (v86_pic_is_port(port)) {
        v86_pic_out(port, value);
    } else if (v86_kbd_is_port(port)) {
        v86_kbd_out(port, value);
    }
    /* 採取中だけ・通さなかったポートだけ積む (通したものは上で積んだ) */
    v86_gcap_note_emul_out(port, size, value);

    io_trap_n++;
    io_last_port = port;
    iolog(port, size, 1, value);
}
