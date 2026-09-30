/* ======================================================================== */
/*  SERIAL.C — PC-98 RS-232C シリアル通信ドライバ                          */
/*                                                                          */
/*  μPD8251A内蔵RS-232Cポートを直接制御                                    */
/*  IRQ4割り込みで受信データをリングバッファに格納                            */
/*                                                                          */
/*  出典: PC9800Bible §2-10                                                 */
/*  参照: FreeBSD sys/pc98/cbus/sio.c                                       */
/*    - pc98_i8251_reset(): 0x00×3 → 0x40 → mode → cmd                    */
/*    - pc98_set_baud_rate(): PIT #2 (0x75/0x77), I/Oウェイト(0x5f)         */
/*    - pc98_ttspeedtab(): 8MHz系 1996800 / 16 / speed                     */
/*    - IRQ4固定、ポート {0x30, 0x32, 0x32, 0x33, 0x35}                    */
/*                                                                          */
/*  **0035h (8255 ポート C) は丸ごと書かない** — 割り込み許可 bit0-2 と同じ */
/*  バイトに BUZ・MCHKEN・SHUT0/1・PSTBM がいる。許可ビットは 0037h の BSR */
/*  で 1 ビットずつ操作する (ser_ien_bsr、POLICY_DEBUG §4-59)。          */
/* ======================================================================== */

#include "serial.h"
#include "serial_plan.h"   /* 速度と FIFO の純粋な判定 (ホストで試験する) */
#include "io.h"
#include "pc98.h"
#include "kprintf.h"

/* 外部: 起動時に判定したシステムクロック (kernel/sysclk.h)。drivers/ は
 * カーネルヘッダを見ない作法なので extern で引く (kbd.c / ide.c と同じ)。
 * **ここで 0000:0501h を読まない** — serial_init は KAPI 経由 (CPL=3 の
 * アプリ文脈) でも呼ばれるので、低位物理は起動時に読んだ保存値だけを使う。 */
extern unsigned long sysclk_hz(void);
extern int sysclk_is_8mhz(void);

/* 外部: irq_enable / irq_disable (idt.c で定義)。drivers/ は -Ikernel を
 * 持たないので、kbd.c / ide.c と同じ扱いでここに宣言する。 */
extern void irq_enable(unsigned int irq);
extern void irq_disable(unsigned int irq);

/* 外部: 校正済みマイクロ秒ディレイ (kernel/cpu_calibrate.h)。
 * drivers/ はカーネルヘッダを見ない作法なので extern で引く
 * (sysclk_* と同じ理由。ne2000.c は kernel/ を -I しているが、ここは
 * 宣言 1 行で足りる)。 */
extern void cpu_delay_us(u32 us);

/* 外部: PIT 100Hz が進める実時間 (kernel/idt.h)。送信の予算はこれで測る
 * — cpu_delay_us の校正が何倍ずれても狂わないため (往復 3)。 */
extern volatile u32 tick_count;

/* ======== 初期化状態 ======== */
static int ser_initialized = 0;

/* ======== 受信リングバッファ ======== */
static volatile u8  ser_buf[SER_BUF_SIZE];
static volatile int ser_head = 0;
static volatile int ser_tail = 0;
static volatile int ser_count = 0;

/* ======================================================================== */
/*  SerialFS セッションのゲート (票 TASK_SERIAL_HOSTFS §1-v3)                */
/*                                                                          */
/*  **呼び口で分ける。** ゲートが上がっているあいだ:                        */
/*   - 送信: `serial_putchar` / `serial_puts` (console の複写・KAPI・        */
/*     ime_dict・ISR の kprintf がすべてここを通る) は線へ出さずに下の保留   */
/*     リングへ溜める。線へ出せるのは `serial_gate_put` (SerialFS 専用) と   */
/*     `serial_puts_polled` (パニック・例外。そのときセッションは死んでいる) */
/*     だけ                                                                 */
/*   - 受信: `serial_getchar` / `serial_trygetchar` / `serial_peekchar` /    */
/*     `serial_has_data` (KAPI と kbd.c の 4 か所) は「無い」。読めるのは    */
/*     `serial_gate_get` (SerialFS の受信器) だけ                            */
/*   - `serial_init` / `serial_init_vfast` (速度変更) は断る                 */
/*  持ち主は fs/serialfs_session.c だけ (exec_exit / ring3_fault_kill は     */
/*  触らない)。                                                             */
/* ======================================================================== */
static volatile int s_gate = 0;
static u8  s_hold[SER_HOLD_SIZE];
static u32 s_hold_head = 0;       /* いちばん古いバイト */
static u32 s_hold_count = 0;
static u32 s_hold_dropped = 0;    /* 溢れて捨てた古いバイト */

/* 受信の誤り (§1-v2「ISR の計数」)。ISR がリセットする前に数える。
 * 実機の切り分け用に `serial` コマンドが出す (KAPI serial_diag)。 */
static u32 ser_err_oe = 0;
static u32 ser_err_fe = 0;
static u32 ser_err_pe = 0;

/* ======================================================================== */
/*  PITモード値 (RS-232C通信速度設定用)                                    */
/*  PC9800Bible §2-3: カウンタ#2 = RS-232C通信速度                             */
/* ======================================================================== */
#define PIT_SER_MODE3  (PIT_SC_CNT2 | PIT_RL_LSBMSB | PIT_M_SQWAVE)  /* 0xB6 */
#define PIT_SER_MODE2  (PIT_SC_CNT2 | PIT_RL_LSBMSB | PIT_M_RATEGEN) /* 0xB4 */

/* ======================================================================== */
/*  serial_init — RS-232C初期化                                             */
/*                                                                          */
/*  FreeBSD pc98_i8251_reset() + pc98_set_baud_rate() 準拠                  */
/*  デフォルト: 8N1 (8bit, パリティなし, ストップビット1)                     */
/* ======================================================================== */
/* ======================================================================== */
/*  システムクロックは kernel/sysclk.c の保存値を見る (票 §1-0)。            */
/*  判定していなければ sysclk_hz() が従来の既定 1.9968MHz を返す。          */
/* ======================================================================== */
static struct serial_setup s_setup = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* ======================================================================== */
/*  いま使っているポートとビットマスク                                      */
/*                                                                          */
/*  **互換 (0030h/0032h) と FIFO (0130h/0132h) はビット位置まで違う。**      */
/*  取り違えを 1 か所に閉じ込めるために、serial_plan.c の選択子で引いた値を  */
/*  ここに持ち、送受信はこれだけを見る。初期値は従来の互換モード。          */
/* ======================================================================== */
static unsigned int s_port_data  = SER_DATA;
static unsigned int s_port_cmd   = SER_CMD;
static u8 s_mask_txrdy = STS_TXRDY;
static u8 s_mask_rxrdy = STS_RXRDY;
static u8 s_mask_err   = (u8)(STS_PE | STS_OE | STS_FE);
/* 割り込み許可 (0035h bit0-2 の望む値)。**ISR 末尾の再許可もこれを使う** — 直値だと
 * 切替でマスクを変えた瞬間に ISR が踏み潰す (Codex レビュー blocker 1)。 */
static u8 s_mask_ien   = IEN_RX;

/* ======================================================================== */
/*  割り込み許可を 0037h の BSR で 1 ビットずつ書く                         */
/*                                                                          */
/*  **0035h へ全体を書かない。** 同じバイトの bit3 が BUZ (0 = 鳴動)、      */
/*  bit7/5 が SHUT0/SHUT1 (リセット後の ITF の動作)、bit4 が MCHKEN、       */
/*  bit6 が PSTBM (docs/hw/undocumented/io_syste.md の I/O 0035h)。以前は   */
/*  ここで 0x00 → 0x01 を全体に書いていたので、受信のたびに BUZ = 0 と    */
/*  なり、実機 PC-9821Ra266 で rshell 中にビープが鳴り続けた (2026-09-24)。 */
/*                                                                          */
/*  `which` に立っているビットだけを触る。触らないビットの今の値は保つ。     */
/*  **TXRE (bit2) を用もなく書かない**: NP21/W の sysp_o37 は bit2 への BSR */
/*  書きを「送信要求」と読み、rs232c.result の状態しだいで IRQ4 を立てる。  */
/*  ISR の中で毎回書くと自分で割り込みを作り続けうる。                     */
/* ======================================================================== */
static void ser_ien_bsr(u8 which, u8 ien)
{
    if (which & IEN_RX)
        outp(SYSPORT_C_BSR, (ien & IEN_RX)    ? BSR_RXRE_ON : BSR_RXRE_OFF);
    if (which & IEN_TXEMP)
        outp(SYSPORT_C_BSR, (ien & IEN_TXEMP) ? BSR_TXEE_ON : BSR_TXEE_OFF);
    if (which & IEN_TX)
        outp(SYSPORT_C_BSR, (ien & IEN_TX)    ? BSR_TXRE_ON : BSR_TXRE_OFF);
}

/* TxRDY を待つ予算 [tick]。serial_init で実効速度から決める。
 * **µs の数え上げではなく tick** — cpu_delay_us の校正が丸めに負けていた
 * 実機で、2083µs のつもりの予算が約 200µs で尽きていた (往復 3、
 * serial_plan.h の注記)。初期化前でも putchar が呼ばれうる (パニック経路)
 * ので既定を入れておく。 */
static u32 s_tx_budget_ticks = (u32)SER_TX_BUDGET_TICKS_MIN;

/* FIFO 搭載判定はリセットまで変わらないので 1 回だけ行う。 */
static u8 s_fifo_probed = 0;

const struct serial_setup *serial_get_setup(void)
{
    return &s_setup;
}

/* ======================================================================== */
/*  I/O 0434h — 拡張RS-232C制御 (極性は機種依存。serial.h の注記を読むこと)  */
/* ======================================================================== */
int serial_get_ext_ctrl(void)
{
    return (int)(u8)inp(SER_EXT_CTRL);
}

int serial_set_div4(int bit_value)
{
    u8 v;

    /* **bit0 (ポート切り離し) を保つ。** 読んでから bit6 だけ差し替える。 */
    v = (u8)inp(SER_EXT_CTRL);
    if (bit_value) {
        v = (u8)(v | SER_EXT_DIV4);
    } else {
        v = (u8)(v & ~SER_EXT_DIV4);
    }
    outp(SER_EXT_CTRL, v);
    io_wait();
    return (int)(u8)inp(SER_EXT_CTRL);
}

/* ======================================================================== */
/*  FIFO 搭載判定 — 0136h を 2 回読んで bit6 の反転を見る                    */
/*                                                                          */
/*  資料 io_rs.md 304〜323 行。判定そのものは serial_plan.c にあり、ここは   */
/*  読むだけ。**リセットまで変わらないので 1 回しか行わない** (0136h の      */
/*  読みは NP21/W では pic_resetirq(4) の副作用を持つ)。                    */
/* ======================================================================== */
static void serial_probe_fifo(void)
{
    u8 a, b;

    if (s_fifo_probed) {
        return;
    }
    a = (u8)inp(SER_FIFO_IIR);
    io_wait();
    b = (u8)inp(SER_FIFO_IIR);
    s_setup.has_fifo = (u8)(serial_fifo_detected(a, b) ? 1 : 0);
    s_fifo_probed = 1;
}

/* ======================================================================== */
/*  serial_init_ex — 初期化の本体 (互換 / V･FAST 共通)                      */
/*                                                                          */
/*  want_vfast が 0 なら従来とまったく同じ経路を通る。1 のときだけ          */
/*  0138h / 013Ah を叩く。戻り 0 = V･FAST に入った / -1 = 互換。            */
/* ======================================================================== */
static int serial_init_ex(unsigned long baud, int want_vfast)
{
    struct serial_plan_out plan;
    u8  mode;
    unsigned long clk;
    unsigned int irqf;
    unsigned long keep;
    int rc;

    /* ==================================================================== */
    /*  切替のあいだは IRQ4 を止める                                        */
    /*                                                                      */
    /*  **割り込み許可 (0035h bit0-2) を BSR で落とすだけでは排他にならない。*/
    /*  NP21/W の `rs232c_callback` は許可ビットを見ずに `pic_setirq(4)` を  */
    /*  上げるし、こちらの ISR 末尾も無条件で BSR により再許可する。止めないと:*/
    /*    - `ser_head=0; ser_tail=0;` の直後に ISR が添字 0 へ書いて tail=1、 */
    /*      そのあと初期化側が `ser_count=0` を書く → 読み手は head=0 から   */
    /*      **古い値を 1 バイト返す**                                        */
    /*    - `s_port_*` / `s_mask_*` を 1 本ずつ差し替えている最中に ISR が   */
    /*      走ると、**新しいポートを古いビット位置で読む**                   */
    /*  PIC のマスクと IF の両方を落とす。IF は元の状態へ戻す                */
    /*  (`irq_save`/`irq_restore`) — 起動時は IF=0 で呼ばれることがある。    */
    /*                                                                      */
    /*  ⚠ **この区間で `kprintf` を呼んではいけない。** rshell 中の          */
    /*  `console.c` は kprintf をシリアルへも流し、`serial_putchar` は予算を */
    /*  使い切ると `_halt()` する。IF=0 の `hlt` は二度と起きない。          */
    /*  報告は区間を出てから行う。                                          */
    /* ==================================================================== */
    irqf = irq_save();
    irq_disable(4);

    /* ---- FIFO 搭載判定 (1 回だけ。0136h を読むだけで何も書かない) ---- */
    serial_probe_fifo();

    /* クロックは起動時に 0000:0501h から判定した保存値 (kernel/sysclk.c)。
     * 未判定なら sysclk_hz() が従来値 (TIMER_CLK_1997) を返す。 */
    clk = sysclk_hz();
    if (baud == 0) baud = SER_BAUD_DEFAULT;
    serial_plan(baud, (int)s_setup.has_fifo, clk, want_vfast, &plan);

    /* ==================================================================== */
    /*  出せない速度は **適用しない** (現状維持)                             */
    /*                                                                      */
    /*  以前は「WARN を出して実効値を適用」だった。それだと FIFO 非搭載機で  */
    /*  `serial 115200` を打ったとき、2.4576MHz 系では count=1 = 153600bps が */
    /*  そのまま入る。ホストは 115200 へ移ってしまうので、**戻すための       */
    /*  `serial 9600` すら届かなくなる** (Codex レビュー blocker 2)。        */
    /*  ここまでハードウェアには 1 バイトも書いていないので、そのまま戻れば  */
    /*  いまの設定が生き残る。                                              */
    /*                                                                      */
    /*  起動時の既定 (SYS_SERIAL_BAUD = 9600) は 1.9968MHz / 2.4576MHz の    */
    /*  どちらでも exact なので、この分岐を通らない。                        */
    /* ==================================================================== */
    if (plan.mode == SER_MODE_COMPAT && !plan.exact) {
        keep = s_setup.actual;
        if (ser_initialized) {
            irq_enable(4);   /* いまの設定を生かしたままにする */
        }
        irq_restore(irqf);
        if (ser_initialized) {
            kprintf(0x0E,
                    "[ser] refuse %ubps: 8253 では %ubps になる (現状維持 %ubps)\n",
                    (u32)baud, (u32)plan.actual, (u32)keep);
        } else {
            kprintf(0x0E,
                    "[ser] refuse %ubps: 8253 では %ubps になる (未初期化のまま)\n",
                    (u32)baud, (u32)plan.actual);
        }
        return SER_INIT_REFUSED;
    }

    /* ---- ここから実際に書く ---- */
    /* 全割り込みマスク。0035h を読み、**いま 1 のビットだけ** BSR で落とす
     * (0035h 全体は書かない)。0 のビットへ書かないのは、NP21/W の sysp_o37 が
     * bit2 (TXRE) への BSR 書きを送信要求と読み、余分な IRQ4 を 1 回起こし
     * うるため。8255 のポート C は出力ラッチを読み返せる。 */
    {
        u8 cur = (u8)(inp(SER_MASK) & (IEN_RX | IEN_TXEMP | IEN_TX));
        ser_ien_bsr(cur, 0);
    }

    /* ---- いまのモードから抜ける ----
     * V･FAST / FIFO から互換へ戻すときは **8251 を触る前に** 013Ah bit7 と
     * 0138h を落とす。落とさないと以後のコマンド書きが 0032h と 0132h の
     * どちらに効くのか決まらない。
     * FIFO 非搭載機では 0130h〜013Ah は存在しないので触らない。 */
    if (s_setup.has_fifo && plan.mode != SER_MODE_VFAST) {
        outp(SER_VFAST_REG, SER_VFAST_OFF); io_wait();
        outp(SER_FIFO_FCR, SER_FCR_OFF);    io_wait();
    }

    /* ---- ポートとビットマスクを新しいモードに合わせる ---- */
    s_port_data  = serial_data_port(plan.mode);
    s_port_cmd   = serial_cmd_port(plan.mode);
    s_mask_txrdy = serial_txrdy_mask(plan.mode);
    s_mask_rxrdy = serial_rxrdy_mask(plan.mode);
    s_mask_err   = serial_err_mask(plan.mode);

    /* ---- V･FAST に入る ----
     * 1. 0138h に FCR0|FCR1|FCR2 = FIFO モード + 送受信 FIFO リセット
     * 2. 013Ah に bit7 | 分周
     * 以後データは 0130h、ステータス/コマンドは 0132h。 */
    if (plan.mode == SER_MODE_VFAST) {
        outp(SER_FIFO_FCR,
             SER_FCR_ENABLE | SER_FCR_RX_RST | SER_FCR_TX_RST); io_wait();
        outp(SER_VFAST_REG,
             (unsigned)(SER_VFAST_ENABLE | plan.div)); io_wait();
    }

    /* ---- 8251A リセット (FreeBSD pc98_i8251_reset() 準拠) ----
     * FIFO は 8251 の**前段**に入るだけで 1st CCU は 8251 のまま (資料の
     * 「RS-232C クロック」図) なので、手順は互換モードと同じ。違うのは
     * 書き先が 0132h になることだけ。**資料は 0132h を [READ] としか
     * 書いていない**が、NP21/W は 0132h の out を 0032h と同じハンドラ
     * (rs232c_o32) に繋いでいる (rs232c_bind)。資料 0138h の関連欄も
     * 0030h / 0032h を挙げているので、これに従う。 */
    outp(s_port_cmd, 0x00); io_wait();   /* ダミー ×3 */
    outp(s_port_cmd, 0x00); io_wait();
    outp(s_port_cmd, 0x00); io_wait();
    outp(s_port_cmd, CMD_RESET); io_wait();   /* 内部リセット (0x40) */

    /* ブザー停止 (0037h BSR 07h = BUZ bit3 を 1)。極性は UNDOCUMENTED
     * (io_syste.md の I/O 0037h: 06h = 鳴動、07h = 停止) を採る。Bible §2-2 は
     * 逆に書いているが、NP21/W (sound/beepc.c) も UNDOCUMENTED と同じ向きで、
     * 以前ここにあった「NP21/W では極性逆」は読み違い (書く値 07h は正しかった)。 */
    outp(SYSPORT_C_BSR, BSR_BUZ_OFF);

    /* **要求どおりに出るかを記録して報告する** ([V4]: 黙ってずれたまま進まない)。
     * 互換モードの分周比は整数しか設定できないので、割り切れない速度は必ずずれる。
     * 例: 1.9968MHz で 38400 を頼むと count=3 になり実効 41600bps (+8.3%)。
     * UART の許容 (±3% 程度) を超えるので実機では通らない。
     * V･FAST は 8253 と無関係なので、表にある速度はちょうど出る。 */
    s_setup.want = baud;
    s_setup.clk = clk;
    s_setup.count = plan.count;
    s_setup.actual = plan.actual;
    s_setup.sysclk_8mhz = (u8)sysclk_is_8mhz();
    s_setup.exact = plan.exact;
    s_setup.mode = (u8)plan.mode;
    s_setup.vfast_div = plan.div;

    /* TxRDY を待つ予算は **実効速度** から決める (要求値ではない)。 */
    s_tx_budget_ticks = serial_tx_budget_ticks(s_setup.actual);

    /* ---- 8253 カウンタ#2 (互換モードだけ) ----
     * V･FAST 中は 013Ah bit7 がカウンタ#2 出力を無効にするので触らない
     * (資料 013Ah の解説)。互換へ戻すときはこの経路が必ず通るので、
     * 8253 は「互換に戻った時点で」正しい値に入る。 */
    if (plan.mode == SER_MODE_COMPAT) {
        /* PIT モード設定: カウンタ#2, LSB+MSB, Mode 3(方形波) */
        /* FreeBSD: count==3 のときだけ Mode 2 */
        if (plan.count != 3)
            outp(SER_TIMER_MODE, PIT_SER_MODE3);
        else
            outp(SER_TIMER_MODE, PIT_SER_MODE2);

        io_wait();
        outp(SER_TIMER_CNT, plan.count & 0xFF);
        io_wait();
        outp(SER_TIMER_CNT, (plan.count >> 8) & 0xFF);
    }

    /* ---- モードセット: 8N1, ×16分周 ---- */
    mode = MOD_CLKx16 | MOD_8BIT | MOD_STOP1;  /* 0x4E */
    outp(s_port_cmd, mode); io_wait();

    outp(s_port_cmd, CMD_TXE | CMD_DTR | CMD_RXE | CMD_RTS | CMD_ER);
    /* = 0x01 | 0x02 | 0x04 | 0x20 | 0x10 = 0x37 */

    /* ---- バッファクリア ---- */
    ser_head = 0;
    ser_tail = 0;
    ser_count = 0;

    /* ---- 受信割り込みを有効化 ----
     * **資料に FIFO モードでの割り込みマスクの記述は無い。** 0136h は
     * 「割り込み参照」で、許可/禁止のレジスタではない。NP21/W も 0035h
     * 以外でマスクしていないので、両モードとも従来どおり 0035h の bit0-2 を
     * (0037h の BSR 経由で) 使う。
     * ISR 末尾の再許可は s_mask_ien を書くので、ここで決めた値と食い違わない
     * (直値を書いていたころは、モードごとにマスクを変えた瞬間に ISR が
     * 1 回目の受信でそれを踏み潰す形になっていた)。 */
    s_mask_ien = IEN_RX;
    /* 3 ビットとも上で 0 にしてあるので、立てるビットだけ書く。 */
    ser_ien_bsr(s_mask_ien, s_mask_ien);

    /* ---- PIC IRQ4 有効化 ---- */
    irq_enable(4);

    /* ---- BUZ OFF 再確認 (PIT設定の副作用対策) ---- */
    outp(SYSPORT_C_BSR, BSR_BUZ_OFF);  /* 07h = 停止 (UNDOCUMENTED) */

    ser_initialized = 1;
    rc = (plan.mode == SER_MODE_VFAST) ? SER_INIT_VFAST : SER_INIT_COMPAT;

    /* ---- 危険区間はここまで。IF を元へ戻す ---- */
    irq_restore(irqf);

    /* **報告は区間の外で。** rshell 中はこの kprintf がシリアルへも流れ、
     * `serial_putchar` が予算を使い切ると `_halt()` する ([V4] の報告より
     * 先に、IF=0 の hlt で止まらないことが要る)。 */
    if (plan.mode == SER_MODE_VFAST) {
        kprintf(0x0A, "[ser] %ubps (V-FAST div %u, FIFO)\n",
                (u32)s_setup.actual, (u32)plan.div);
    } else {
        kprintf(0x0A, "[ser] %ubps (clk %uHz, count %u)%s\n",
                (u32)baud, (u32)clk, (u32)plan.count,
                s_setup.has_fifo ? " [FIFO available]" : "");
    }
    return rc;
}

void serial_init(unsigned long baud)
{
    /* **セッション中は速度を変えない** (§1-v3)。ホストは今の速度で
     * フレームを待っている。報告の kprintf は保留リングへ入る。 */
    if (s_gate) {
        kprintf(0x0E, "[ser] refuse serial_init during SerialFS session\n");
        return;
    }
    /* **従来の経路のまま。** 票の決裁「起動時の既定 9600 は互換モード」。
     * V･FAST 中に呼べば 013Ah bit7=0 / 0138h=0 を書いて互換へ戻る。 */
    (void)serial_init_ex(baud, 0);
}

int serial_init_vfast(unsigned long baud)
{
    if (s_gate) {
        kprintf(0x0E, "[ser] refuse serial_init during SerialFS session\n");
        return SER_INIT_REFUSED;
    }
    /* FIFO 非搭載 / 表に無い速度なら serial_plan が互換を返すので、
     * ここは「頼んだ」ことを渡すだけ。戻り -1 = 互換に落ちた。 */
    return serial_init_ex(baud, 1);
}

int serial_get_status(u32 *mode, u32 *baud, u32 *fifo)
{
    if (mode) *mode = (u32)s_setup.mode;
    if (baud) *baud = (u32)s_setup.actual;
    if (fifo) *fifo = (u32)s_setup.has_fifo;
    return ser_initialized ? 0 : -1;
}

/* ======================================================================== */
/*  serial_irq_handler — IRQ4 割り込みハンドラ (Cレベル)                    */
/*                                                                          */
/*  PC-98ではRS-232Cの送受信が同一IRQ4を共有                                */
/*  ステータスを読んで受信か送信かを判定                                      */
/* ======================================================================== */
/* 受信バッファ溢れで捨てたバイト数 (kernel.map 経由で観測する)。
 * static にするとホストから読めないので意図的にグローバル。 */
u32 ser_overflow_n = 0;

void serial_irq_handler(void)
{
    u8 sts;
    u8 data;
    int loop_count = 0; /* 無限ループ防止用のカウンタ */
    /* FIFO モードでは 1 回の割り込みで FIFO 1 杯 (16 バイト) を汲む。
     * 互換モードは従来どおり 128 バイトまで (1 バイトずつしか来ないので
     * 実際には 1〜2 周で抜ける)。 */
    int loop_max = (s_setup.mode == SER_MODE_VFAST)
                 ? SER_FIFO_DEPTH : SER_IRQ_DRAIN_MAX;

    for (;;) {
        /* **ポートもビットも s_* 経由。** 互換 0032h は bit1 が RxRDY、
         * FIFO 0132h は bit2 が RxRDY で、0x04 は互換では TxEMP にあたる。
         * 直に書くと「送信が空くたびに受信データを読む」形で静かに壊れる。 */
        sts = (u8)inp(s_port_cmd);

        /* エラーがあれば数えてからリセット。ビットの意味はモードで違うので
         * serial_plan の選択子で引く (FIFO の 0132h は資料と NP21/W が
         * 食い違う — 数は目安)。 */
        if (sts & s_mask_err) {
            if (sts & serial_oe_mask(s_setup.mode)) ser_err_oe++;
            if (sts & serial_fe_mask(s_setup.mode)) ser_err_fe++;
            if (sts & serial_pe_mask(s_setup.mode)) ser_err_pe++;
            outp(s_port_cmd, CMD_TXE | CMD_DTR | CMD_RXE | CMD_RTS | CMD_ER);
        }

        if (!(sts & s_mask_rxrdy)) break;

        data = (u8)inp(s_port_data);

        /* バッファに格納。
         * 満杯なら捨てるしかないが、黙って捨てると「rshell の応答が
         * たまに欠ける」の原因が分からなくなるので回数を残す
         * (ISR 内なので kprintf は使えない — 出力先が自分自身)。 */
        if (ser_count < SER_BUF_SIZE) {
            ser_buf[ser_tail] = data;
            ser_tail = (ser_tail + 1) % SER_BUF_SIZE;
            ser_count++;
        } else {
            ser_overflow_n++;
        }

        /* 異常な割り込み嵐を防ぐため、上限まで読んだら一旦抜ける
         * (残っていれば次の割り込みで続きを汲む) */
        loop_count++;
        if (loop_count >= loop_max) break;
    }

    /* FIFO モードでは 0136h (割り込み参照) を読んで要因を落とす。
     * **資料は「取得を行う」としか書いていない**が、NP21/W の rs232c_i136 は
     * 読んだときに irqflag を畳んで pic_resetirq(4) を呼ぶ。読まないと同じ
     * 要因で割り込みが上がり続ける。互換モードにこのレジスタは無い。 */
    if (s_setup.mode == SER_MODE_VFAST) {
        (void)inp(SER_FIFO_IIR);
    }

    /* 許可ビットを一度落として戻し、IRQ4 の立ち上がりエッジを作り直す
     * (Bible §2-10「送受信両方の割り込みを利用する方法」の意図: 汲み残しが
     * あっても次の割り込みが上がるようにする)。
     * 再許可は **初期化が決めた値** を書く。直値 IEN_RX だと、将来モードごとに
     * マスクを変えたときに ISR が 1 回目の受信でそれを踏み潰す。
     * **0035h 全体へは書かない** — BUZ などが巻き添えで 0 になる (§4-59)。
     * 触るのは許可しているビットだけ (TXRE を用もなく書かない、ser_ien_bsr)。 */
    ser_ien_bsr(s_mask_ien, 0);
    ser_ien_bsr(s_mask_ien, s_mask_ien);
}

/* ======================================================================== */
/*  公開API                                                                */
/* ======================================================================== */

int serial_is_initialized(void)
{
    return ser_initialized;
}

int serial_has_data(void)
{
    if (s_gate) return 0;          /* 受信はセッションの受信器だけのもの */
    return ser_count > 0;
}

/* ノンブロッキング受信 */
int serial_trygetchar(void)
{
    int ch;
    if (s_gate) return -1;         /* rshell / kbd.c / KAPI にフレームを渡さない */
    if (ser_count == 0) return -1;

    RING_DEQUEUE(ch, ser_buf, ser_head, ser_count, SER_BUF_SIZE);

    return ch;
}

/* 取り出さずに先頭だけ覗く (継承バグ: script_exec の ESC 監視)。
 * 無ければ -1。serial_trygetchar と違ってリングは 1 バイトも動かさないので、
 * 「ESC かどうかだけ見て、ESC でなければ次の読み手へ残す」が書ける。
 * IRQ4 は tail 側にしか触らないので、ser_count > 0 なら先頭は動かない。 */
int serial_peekchar(void)
{
    if (s_gate) return -1;
    if (ser_count == 0) return -1;
    return (int)ser_buf[ser_head];
}

/* ブロッキング受信 */
int serial_getchar(void)
{
    int ch;
    /* セッション中は待たずに「無い」(待つと SerialFS の応答を盗む) */
    if (s_gate) return -1;
    while (ser_count == 0) {
        if (s_gate) return -1;
        _halt();
    }

    RING_DEQUEUE(ch, ser_buf, ser_head, ser_count, SER_BUF_SIZE);

    return ch;
}

/* ======================================================================== */
/*  serial_putchar — ポーリング送信 (予算つき)                              */
/*                                                                          */
/*  **直す前は 100 回スピンしてから `hlt` で次の 10ms tick まで寝ていた。**  */
/*  1 文字ごとに最悪 10ms 寝るので、9600 で 490B/s、38400 では 233B/s と     */
/*  回線より遅くなっていた (票 TASK_SERIAL_VFAST §0)。                      */
/*                                                                          */
/*  回線が 1 文字を押し出す時間は baud で決まるのだから、その時間だけ見て    */
/*  から寝ればよい。予算 = 1 文字時間 × 2 を **tick に切り上げた値**        */
/*  (serial_plan.h の `serial_tx_budget_ticks`)。予算のあいだは             */
/*  `cpu_delay_us(SER_TX_POLL_US)` を挟んで TxRDY を見る。                  */
/*                                                                          */
/*  ⚠ **時間は `tick_count` で測る。** µs を数え上げる書き方は              */
/*  「cpu_delay_us(5) が本当に 5µs 待つ」に寄りかかっており、実機で校正が    */
/*  丸めに負けていたとき (往復 3) に予算が 1/10 になって、9600 の 1 文字     */
/*  時間すら待てずに hlt へ落ちていた。tick は PIT が進める実時間なので、    */
/*  校正が何倍ずれても予算は狂わない。                                      */
/*                                                                          */
/*  予算を超えたら従来どおり `hlt` で 1 割り込み分待つ (最大                 */
/*  SER_TX_HLT_RETRY 回) — 相手がフロー制御で止めているときに CPU を         */
/*  焼かないため。**ここは IF=1 でしか呼べない** (割り込み禁止区間からは     */
/*  serial_puts_polled を使う)。                                            */
/*                                                                          */
/*  cpu_calibrate() の前は cpu_delay_us が即座に返る (s_loops_per_tick=0)。  */
/*  そのときは予算の tick が尽きるまで素のスピンになるだけで、待ち時間       */
/*  そのものは変わらない (これも tick で測る利点)。                          */
/* ======================================================================== */
static int ser_tx_byte(char c)
{
    u32 start;
    u32 spin;
    int retry;
    int can_halt;

    /* **IF=0 で呼ばれることがある** (パニック経路・割り込み禁止区間)。
     * そこでは tick_count が進まないので時間で測れず、`_halt()` は
     * 二度と起きない。回数上限のスピンだけで諦める。 */
    can_halt = _irq_enabled();

    for (retry = 0; retry < SER_TX_HLT_RETRY; retry++) {
        start = tick_count;
        spin = 0;
        for (;;) {
            if (inp(s_port_cmd) & s_mask_txrdy) {
                outp(s_port_data, (unsigned)(u8)c);
                return SER_TX_OK;
            }
            if (can_halt) {
                /* **実時間で測る。** tick_count は PIT の割り込みが進めるので、
                 * cpu_delay_us の校正が何倍ずれても予算は狂わない
                 * (往復 3: 校正が 1/10 で予算が 200µs に化けていた)。 */
                if ((u32)(tick_count - start) >= s_tx_budget_ticks) break;
            } else {
                if (spin >= (u32)SER_TX_SPIN_MAX) return SER_TX_DROPPED;
                spin++;
            }
            /* ポートを読む間隔を空けるだけ。正確さは要らない。 */
            cpu_delay_us((u32)SER_TX_POLL_US);
        }
        /* 予算を使い切った = 相手が読んでいない。次の割り込みまで寝る。 */
        _halt();
    }
    /* タイムアウト: 送信を諦める。**呼び手に知らせる** — rshell の番犬は
     * 「EOT を送り終えた」ことを往復の証拠にしているので、ここで黙って
     * 捨てると「応答したつもり」で番犬を解除してしまう (Codex ③)。 */
    return SER_TX_DROPPED;
}

/* 保留リングへ 1 バイト。溢れたら**いちばん古い**バイトを捨てて数える
 * (hsync の結果行と終了コードは最後に来るので、残すのは新しい方)。
 * ISR の kprintf からも来るので IF を落として触る。 */
static void ser_hold_push(u8 c)
{
    unsigned int f = irq_save();
    if (s_hold_count >= (u32)SER_HOLD_SIZE) {
        s_hold_head = (s_hold_head + 1) % (u32)SER_HOLD_SIZE;
        s_hold_count--;
        s_hold_dropped++;
    }
    s_hold[(s_hold_head + s_hold_count) % (u32)SER_HOLD_SIZE] = c;
    s_hold_count++;
    irq_restore(f);
}

int serial_putchar(char c)
{
    /* セッション中は線へ出さずに溜める (後で長さ付きのフレームで運ぶ) */
    if (s_gate) {
        ser_hold_push((u8)c);
        return SER_TX_OK;
    }
    return ser_tx_byte(c);
}

/* ======================================================================== */
/*  ゲートの口 (fs/serialfs_session.c だけが使う)                          */
/* ======================================================================== */
void serial_gate_set(int on)
{
    unsigned int f = irq_save();
    if (on) {
        s_hold_head = 0;
        s_hold_count = 0;
        s_hold_dropped = 0;
    }
    /* 上げるときも下ろすときも受信リングを空にする。上げるとき = 前の
     * 会話の残り、下ろすとき = 隔離の後に届いたセッションの残り
     * (rshell にフレームの断片を 1 バイトも渡さない)。
     * **UART / FIFO に残っているバイトも先に読み捨てる** — ISR がまだ汲んで
     * いない分 (FIFO モードの閾値未満の末尾) は、下ろした直後に ISR が
     * リングへ入れて rshell に渡してしまう (レビュー往復 1、Codex 6)。 */
    {
        int n;
        for (n = 0; n < SER_GATE_DRAIN_MAX; n++) {
            u8 sts = (u8)inp(s_port_cmd);
            if (sts & s_mask_err)
                outp(s_port_cmd, CMD_TXE | CMD_DTR | CMD_RXE | CMD_RTS | CMD_ER);
            if (!(sts & s_mask_rxrdy)) break;
            (void)inp(s_port_data);
        }
    }
    ser_head = 0;
    ser_tail = 0;
    ser_count = 0;
    s_gate = on ? 1 : 0;
    irq_restore(f);
}

int serial_gate_active(void)
{
    return s_gate;
}

int serial_gate_put(const u8 *buf, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (ser_tx_byte((char)buf[i]) != SER_TX_OK) return SER_TX_DROPPED;
    }
    return SER_TX_OK;
}

/* RxRDY を**直接**見て、FIFO に残った末尾も汲み出してから 1 バイト返す
 * (FIFO モードでは ISR が閾値未満の数バイトを取り残す。NP21/W では出ない)。
 * ISR と同じポートを読むので IF を落として行う。 */
int serial_gate_get(void)
{
    unsigned int f;
    int ch = -1;
    int n;

    f = irq_save();
    for (n = 0; n < SER_FIFO_DEPTH; n++) {
        u8 sts = (u8)inp(s_port_cmd);
        if (sts & s_mask_err) {
            if (sts & serial_oe_mask(s_setup.mode)) ser_err_oe++;
            if (sts & serial_fe_mask(s_setup.mode)) ser_err_fe++;
            if (sts & serial_pe_mask(s_setup.mode)) ser_err_pe++;
            outp(s_port_cmd, CMD_TXE | CMD_DTR | CMD_RXE | CMD_RTS | CMD_ER);
        }
        if (!(sts & s_mask_rxrdy)) break;
        {
            u8 data = (u8)inp(s_port_data);
            if (ser_count < SER_BUF_SIZE) {
                ser_buf[ser_tail] = data;
                ser_tail = (ser_tail + 1) % SER_BUF_SIZE;
                ser_count++;
            } else {
                ser_overflow_n++;
            }
        }
    }
    if (ser_count > 0) {
        RING_DEQUEUE(ch, ser_buf, ser_head, ser_count, SER_BUF_SIZE);
    }
    irq_restore(f);
    return ch;
}

u32 serial_hold_take(u8 *buf, u32 max)
{
    unsigned int f = irq_save();
    u32 n = 0;
    while (n < max && s_hold_count > 0) {
        buf[n++] = s_hold[s_hold_head];
        s_hold_head = (s_hold_head + 1) % (u32)SER_HOLD_SIZE;
        s_hold_count--;
    }
    irq_restore(f);
    return n;
}

u32 serial_hold_dropped(void)
{
    return s_hold_dropped;
}

void serial_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{
    if (oe) *oe = ser_err_oe;
    if (fe) *fe = ser_err_fe;
    if (pe) *pe = ser_err_pe;
    if (overflow) *overflow = ser_overflow_n;
}

/* 文字列送信。
 * 16 バイトごとの `io_wait` は **NP21/W のパイプバッファ対策**として
 * 入ったもの (ホスト側の commng がデータを掃けるまでの間合い)。実機では
 * 要らないが、1 バイトあたり 0.6µs 程度なので 115200 (1 文字 87µs) でも
 * 影響は 1% 未満。残しておく。 */
void serial_puts(const char *str)
{
    int count = 0;
    while (*str) {
        serial_putchar(*str);
        str++;
        count++;
        if ((count & (SER_PUTS_WAIT_EVERY - 1)) == 0) {
            io_wait();
            io_wait();
        }
    }
}

/* ======================================================================== */
/*  ポーリング専用送信 (パニック/例外時用)                                   */
/*  割り込みが無効化されている状態で hlt を使用するとフリーズするため、      */
/*  タイムアウトまでスピンのみで待機する。                                   */
/* ======================================================================== */
static void serial_putchar_polled(char c)
{
    int spin;
    for (spin = 0; spin < SER_POLLED_SPIN_MAX; spin++) {
        /* 予算も cpu_delay_us も使わない (校正が壊れている可能性がある
         * 状況で呼ばれる)。ポートとビットだけモードに合わせる。 */
        if (inp(s_port_cmd) & s_mask_txrdy) {
            outp(s_port_data, (unsigned)(u8)c);
            return;
        }
    }
}

void serial_puts_polled(const char *str)
{
    int count = 0;
    while (*str) {
        serial_putchar_polled(*str);
        if (*str == '\n') {
            serial_putchar_polled('\r');
        }
        str++;
        count++;
        if ((count & 0xF) == 0) {
            io_wait();
            io_wait();
        }
    }
}

void serial_put_hex32_polled(u32 val)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    int i;
    buf[0] = '0';
    buf[1] = 'x';
    for (i = 7; i >= 0; i--) {
        buf[i+2] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = '\0';
    serial_puts_polled(buf);
}
