/* ======================================================================== */
/*  SERIAL_VFAST_HOST.C — drivers/serial_plan.c をそのままホストで回す      */
/*                                                                          */
/*  実物の判定を 1 行も写さずに #include する。serial_plan.c は I/O も      */
/*  tick_count も触らないので、模型は 1 つも要らない。                      */
/*                                                                          */
/*  見るのは実機でしか踏めない 3 つ (記録: tools/tests/serial_vfast_tdd.md):*/
/*    (a) V･FAST の速度→分周表 (013Ah bit3-0) — 表に無い速度は 0           */
/*    (b) 8253 の整数分周: 1.9968MHz の 38400 は 41600 に化ける (+8.3%)、   */
/*        2.4576MHz なら count 4 でちょうど出る                             */
/*    (c) TxRDY を待つ予算 (µs) = 2 × 10 ビット ÷ baud                      */
/*                                                                          */
/*  NP21/W は通信速度を模擬しないので、ここはエミュレータでは踏めない。     */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/serial_plan.c"
/* 番犬はシェル側 (userland/shell) に住む — 戻す判断をするのは
 * ゲストのシェルで、カーネルには周期フックが無いため。 */
#include "../../userland/shell/serial_watchdog.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* 実機 PC-9821Ra266 は 2.4576MHz 系 (TASK_FDC_REALHW §9-1)。 */
#define CLK_2458    2457600UL
#define CLK_1997    1996800UL

/* ------------------------------------------------------------------ */
/*  (a) V･FAST の速度→分周表 (io_rs.md 352〜380 行 / NP21/W speedtbl)   */
/* ------------------------------------------------------------------ */
static void vfast_table(void)
{
    /* 資料の表と NP21/W の rs232c_vfast_setrs232cspeed の speedtbl は
     * **完全に一致する**。片方だけを写していないことを両方向で見る。 */
    CHECK(serial_vfast_div(115200UL) == 0x1);
    CHECK(serial_vfast_div(57600UL)  == 0x2);
    CHECK(serial_vfast_div(38400UL)  == 0x3);
    CHECK(serial_vfast_div(28800UL)  == 0x4);
    CHECK(serial_vfast_div(19200UL)  == 0x6);
    CHECK(serial_vfast_div(14400UL)  == 0x8);
    CHECK(serial_vfast_div(9600UL)   == 0xC);

    /* 名前つき定数と数値がずれていないこと ([C4])。 */
    CHECK(SER_VFAST_DIV_115200 == 0x1);
    CHECK(SER_VFAST_DIV_9600   == 0xC);

    /* 表に無い速度は 0。**ここで適当な分周を返すと、実機は黙って
     * 別の速度で喋りはじめる** (V･FAST は bit7 を立てた時点で 8253 と
     * 無関係になるので、8253 側の設定では戻せない)。 */
    CHECK(serial_vfast_div(4800UL)   == 0);
    CHECK(serial_vfast_div(31250UL)  == 0);
    CHECK(serial_vfast_div(230400UL) == 0);
    CHECK(serial_vfast_div(0UL)      == 0);
    CHECK(serial_vfast_div(1UL)      == 0);

    /* 分周値は 4 ビットに収まる (013Ah bit6-4 は「常に 000b にする」)。 */
    CHECK((serial_vfast_div(115200UL) & ~SER_VFAST_DIV_MASK) == 0);
    CHECK((serial_vfast_div(9600UL)   & ~SER_VFAST_DIV_MASK) == 0);
}

/* ------------------------------------------------------------------ */
/*  (b) 互換モード: 8253 カウンタ#2 の整数分周                          */
/* ------------------------------------------------------------------ */
static void compat_exact(void)
{
    struct serial_plan_out p;

    /* 9600 は 1.9968MHz / 2.4576MHz のどちらでもちょうど出る唯一の
     * 標準速度 (drivers/serial.h の表)。 */
    serial_plan(9600UL, 0, CLK_1997, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.count == 13);          /* 1996800 / 16 / 9600 */
    CHECK(p.actual == 9600UL);
    CHECK(p.exact == 1);

    serial_plan(9600UL, 0, CLK_2458, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.count == 16);          /* 2457600 / 16 / 9600 */
    CHECK(p.actual == 9600UL);
    CHECK(p.exact == 1);

    /* 実機 Ra266 (2.4576MHz) の 38400 は count 4 でちょうど。 */
    serial_plan(38400UL, 0, CLK_2458, 0, &p);
    CHECK(p.count == 4);
    CHECK(p.actual == 38400UL);
    CHECK(p.exact == 1);
    /* 19200 も割り切れる。 */
    serial_plan(19200UL, 0, CLK_2458, 0, &p);
    CHECK(p.count == 8);
    CHECK(p.exact == 1);
}

static void compat_inexact(void)
{
    struct serial_plan_out p;

    /* **1.9968MHz の 38400 は 41600bps に化ける (+8.3%)。** UART の許容
     * (±3% 程度) を超えるので実機では通らない。黙って進まないために
     * exact を 0 にして実効値を返す ([V4])。 */
    serial_plan(38400UL, 0, CLK_1997, 0, &p);
    CHECK(p.count == 3);           /* 124800 / 38400 = 3.25 → 3 */
    CHECK(p.actual == 41600UL);
    CHECK(p.exact == 0);

    /* 19200 も 1.9968MHz では割り切れない (124800 / 19200 = 6.5)。 */
    serial_plan(19200UL, 0, CLK_1997, 0, &p);
    CHECK(p.count == 6);
    CHECK(p.actual == 20800UL);
    CHECK(p.exact == 0);

    /* 速すぎる要求は count 1 で頭打ち。実効値はクロックのまま出す。 */
    serial_plan(921600UL, 0, CLK_2458, 0, &p);
    CHECK(p.count == 1);
    CHECK(p.actual == 153600UL);   /* 2457600 / 16 / 1 */
    CHECK(p.exact == 0);

    /* 遅すぎる要求は count が 16 ビットに収まらない → 頭打ち。 */
    serial_plan(1UL, 0, CLK_2458, 0, &p);
    CHECK(p.count == SER_COUNT_MAX);
    CHECK(p.exact == 0);
}

/* ------------------------------------------------------------------ */
/*  (c) モードの選択 (FIFO 有無 × 速度 × 明示指定)                      */
/* ------------------------------------------------------------------ */
static void mode_choice(void)
{
    struct serial_plan_out p;

    /* FIFO 搭載 + 表にある速度 + 明示指定 → V･FAST。 */
    serial_plan(115200UL, 1, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_VFAST);
    CHECK(p.div == SER_VFAST_DIV_115200);
    CHECK(p.actual == 115200UL);
    CHECK(p.exact == 1);
    /* V･FAST では 8253 を触らない — count は 0 のまま。 */
    CHECK(p.count == 0);

    /* **FIFO が無ければ V･FAST に入らない** (013Ah は FIFO 搭載機の
     * レジスタ。無い機種で bit7 を立てても速度は変わらず、こちらだけ
     * 115200 のつもりで喋って会話が死ぬ)。 */
    serial_plan(115200UL, 0, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.div == 0);
    CHECK(p.exact == 0);           /* 153600 しか出ない */

    /* **明示指定が無ければ V･FAST に入らない** (票の決裁: 起動時の
     * 既定 9600 は実機で通った互換経路を守る)。 */
    serial_plan(9600UL, 1, CLK_2458, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.count == 16);
    /* 明示すれば 9600 でも V･FAST に入れる (戻しの試験用)。 */
    serial_plan(9600UL, 1, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_VFAST);
    CHECK(p.div == SER_VFAST_DIV_9600);

    /* 表に無い速度は FIFO 搭載機でも互換へ落とす。 */
    serial_plan(4800UL, 1, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.count == 32);
    CHECK(p.exact == 1);

    /* **V･FAST なら 1.9968MHz 機でも 38400 がちょうど出る。**
     * 互換経路の 41600 問題 (compat_inexact) を回避できるのがここ。 */
    serial_plan(38400UL, 1, CLK_1997, 1, &p);
    CHECK(p.mode == SER_MODE_VFAST);
    CHECK(p.actual == 38400UL);
    CHECK(p.exact == 1);
    serial_plan(38400UL, 1, CLK_1997, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.actual == 41600UL);
}

/* ------------------------------------------------------------------ */
/*  (d) TxRDY を待つ予算                                                */
/* ------------------------------------------------------------------ */
static void tx_budget(void)
{
    /* 8N1 は 1 文字 10 ビット。その 2 倍を予算にする。
     *   9600   → 20 * 1e6 / 9600   = 2083µs                            */
    CHECK(serial_tx_budget_us(9600UL) == 2083);
    /*   38400  → 520µs                                                 */
    CHECK(serial_tx_budget_us(38400UL) == 520);
    /*   115200 → 173µs                                                 */
    CHECK(serial_tx_budget_us(115200UL) == 173);

    /* **予算は 0 になってはいけない。** 0 だと 1 回も見ないうちに
     * hlt へ落ち、直した意味 (tick を待たない) が消える。 */
    CHECK(serial_tx_budget_us(115200UL) > 0);
    CHECK(serial_tx_budget_us(1000000UL) >= SER_TX_BUDGET_MIN_US);
    CHECK(serial_tx_budget_us(0UL) > 0);

    /* 異常に遅い速度でも上限で止める (1 文字 1 秒を待たない)。 */
    CHECK(serial_tx_budget_us(1UL) == SER_TX_BUDGET_MAX_US);

    /* 予算は刻み幅より必ず大きい (少なくとも数回は見る)。 */
    CHECK(serial_tx_budget_us(115200UL) > SER_TX_POLL_US);
    CHECK(SER_TX_BUDGET_MIN_US > SER_TX_POLL_US);

    /* 速いほど予算は短い (単調)。 */
    CHECK(serial_tx_budget_us(9600UL) > serial_tx_budget_us(38400UL));
    CHECK(serial_tx_budget_us(38400UL) > serial_tx_budget_us(115200UL));
}

/* ------------------------------------------------------------------ */
/*  (d2) 予算は tick で測る (往復 3)                                    */
/*                                                                      */
/*  µs の数え上げ (`waited += SER_TX_POLL_US`) は「cpu_delay_us(5) が    */
/*  本当に 5µs 待つ」に寄りかかっていた。実機 PC-9821Ra266 では校正が     */
/*  丸めに負けて 0.5µs しか待たず、2083µs のつもりの予算が実際には        */
/*  約 200µs で尽きて、9600 の 1 文字時間 (1.04ms) すら待てずに hlt へ    */
/*  落ちていた (1 バイト約 2ms の固定費)。tick は PIT が進める実時間なので */
/*  校正が何倍ずれても狂わない。                                          */
/* ------------------------------------------------------------------ */
static void tx_budget_ticks(void)
{
    /* **1 でも 0 でもない。** tick は「境界を何回跨いだか」なので、
     * 開始が tick の途中だと 1 tick の予算は実時間 0 になりうる。 */
    CHECK(serial_tx_budget_ticks(9600UL) >= 2);
    CHECK(serial_tx_budget_ticks(115200UL) >= 2);
    CHECK(serial_tx_budget_ticks(0UL) >= 2);

    /* 標準的な速度は下限の 3 tick (= 保証 20ms 以上)。2 文字時間そのものは
     * 9600 でも 2083µs で 1 tick に収まるが、**待つ相手は回線だけではない**
     * — ホスト側のフロー制御や FTDI の遅延タイマ (16ms) をまたげる長さが要る
     * (実機のホスト観測: 16ms ごとに 4〜10 バイトの塊で届いていた)。 */
    CHECK(serial_tx_budget_ticks(9600UL) == SER_TX_BUDGET_TICKS_MIN);
    CHECK(serial_tx_budget_ticks(38400UL) == SER_TX_BUDGET_TICKS_MIN);
    CHECK(serial_tx_budget_ticks(115200UL) == SER_TX_BUDGET_TICKS_MIN);
    CHECK(SER_TX_BUDGET_TICKS_MIN == 3);

    /* 保証される実時間は (N-1) tick ぶん。3 tick なら 20ms で、16ms の
     * 遅延タイマをまたげる。ここが 2 になると 10ms でまたげない。 */
    CHECK((SER_TX_BUDGET_TICKS_MIN - 1) * SER_TICK_US >= 16000UL);

    /* **遅い速度では tick も伸びる。** 2 文字時間が 10ms を超えたら
     * 切り上げ + 境界ずれのぶんだけ増える。上限 (50ms) で頭打ち。 */
    CHECK(serial_tx_budget_ticks(1UL)
          == (SER_TX_BUDGET_MAX_US + SER_TICK_US - 1) / SER_TICK_US
             + SER_TX_BUDGET_EDGE_TICKS);
    CHECK(serial_tx_budget_ticks(1UL) == 6);
    CHECK(serial_tx_budget_ticks(1UL) > serial_tx_budget_ticks(9600UL));

    /* 300bps (2 文字 = 66.6ms) も上限で頭打ちになる。 */
    CHECK(serial_tx_budget_ticks(300UL) == 6);

    /* 1200bps: 2 文字 = 16666µs → 切り上げ 2 tick + 境界 1 = 3。 */
    CHECK(serial_tx_budget_us(1200UL) == 16666UL);
    CHECK(serial_tx_budget_ticks(1200UL) == 3);
    /* 600bps: 2 文字 = 33333µs → 切り上げ 4 tick + 境界 1 = 5。 */
    CHECK(serial_tx_budget_us(600UL) == 33333UL);
    CHECK(serial_tx_budget_ticks(600UL) == 5);

    /* 単調性 (速いほうが短いか同じ)。 */
    CHECK(serial_tx_budget_ticks(600UL) >= serial_tx_budget_ticks(1200UL));
    CHECK(serial_tx_budget_ticks(1200UL) >= serial_tx_budget_ticks(9600UL));

    /* IF=0 の回数上限は 0 でない (1 回も見ないうちに諦めない)。
     * **合計 20ms 相当** — 1 周が cpu_delay_us(5µs) + ポート読みなので
     * 4000 回。校正を直した (往復 3) いま、20 万回だと 1 秒/文字になって
     * パニック経路で画面が止まる。 */
    CHECK(SER_TX_SPIN_MAX > 0);
    CHECK(SER_TX_SPIN_MAX == 4000UL);
    CHECK(SER_TX_SPIN_MAX * SER_TX_POLL_US == SER_TX_SPIN_BUDGET_US);
    CHECK(SER_TX_SPIN_BUDGET_US == 20000UL);
    /* 予算の下限 (3 tick = 30ms) と同じ桁であること。 */
    CHECK(SER_TX_SPIN_BUDGET_US <= SER_TX_BUDGET_TICKS_MIN * SER_TICK_US);
}

/* ------------------------------------------------------------------ */
/*  (e) ステータスのビット位置 — 互換 (0032h) と FIFO (0132h) は別物    */
/* ------------------------------------------------------------------ */
static void status_bits(void)
{
    /* 互換 0032h (8251): bit0 TxRDY / bit1 RxRDY / bit2 TxEMP          */
    CHECK(STS_TXRDY == 0x01);
    CHECK(STS_RXRDY == 0x02);
    CHECK(STS_TXE   == 0x04);
    /* FIFO 0132h:      bit0 TxEMP / bit1 TxRDY / bit2 RxRDY            */
    CHECK(SER_FSTS_TXEMP == 0x01);
    CHECK(SER_FSTS_TXRDY == 0x02);
    CHECK(SER_FSTS_RXRDY == 0x04);

    /* **取り違えたら会話が死ぬ組み合わせ**を名指しで押さえる。
     * FIFO の RxRDY (0x04) を互換の TxEMP (0x04) と同じ扱いにすると、
     * 送信バッファが空になるたびに受信データを読みに行く。 */
    CHECK(SER_FSTS_RXRDY != STS_RXRDY);
    CHECK(SER_FSTS_TXRDY != STS_TXRDY);
    CHECK(SER_FSTS_RXRDY == STS_TXE);   /* 同じ値 = 取り違えても落ちない */

    /* 選択子は「モードに対応する組」を返す。 */
    CHECK(serial_rxrdy_mask(SER_MODE_COMPAT) == STS_RXRDY);
    CHECK(serial_rxrdy_mask(SER_MODE_VFAST)  == SER_FSTS_RXRDY);
    CHECK(serial_txrdy_mask(SER_MODE_COMPAT) == STS_TXRDY);
    CHECK(serial_txrdy_mask(SER_MODE_VFAST)  == SER_FSTS_TXRDY);

    /* ポートも同じ組で切り替わる。 */
    CHECK(serial_data_port(SER_MODE_COMPAT) == SER_DATA);
    CHECK(serial_data_port(SER_MODE_VFAST)  == SER_FIFO_DATA);
    CHECK(serial_cmd_port(SER_MODE_COMPAT)  == SER_CMD);
    CHECK(serial_cmd_port(SER_MODE_VFAST)   == SER_FIFO_STS);
}

/* ------------------------------------------------------------------ */
/*  (f) FIFO 搭載判定 (0136h を 2 回読む)                               */
/* ------------------------------------------------------------------ */
static void fifo_detect(void)
{
    /* 資料 304〜323 行: bit6 は読むたびに 1→0→1 と反転、bit5 は常に 0。
     * NP21/W の rs232c_i136 も `port136 ^= 0x40` で同じ。 */
    CHECK(serial_fifo_detected(0x40, 0x00) == 1);
    CHECK(serial_fifo_detected(0x00, 0x40) == 1);
    /* 下位 (割り込み要因) は何であれ判定に関係しない。 */
    CHECK(serial_fifo_detected(0x41, 0x01) == 1);
    CHECK(serial_fifo_detected(0x46, 0x06) == 1);

    /* **未実装ポートは 0xFF を返す** (io_fdd.md / io_rs.md の注意:
     * FFh かどうかで搭載を判断してはいけないが、bit6 が反転しないので
     * この判定なら安全に落ちる)。 */
    CHECK(serial_fifo_detected(0xFF, 0xFF) == 0);
    /* 0x00 固定 (デコードされていない) も非搭載。 */
    CHECK(serial_fifo_detected(0x00, 0x00) == 0);

    /* bit5 が立っていたら搭載ではない (識別ビット2 は常に 0)。 */
    CHECK(serial_fifo_detected(0x60, 0x20) == 0);
    CHECK(serial_fifo_detected(0x20, 0x60) == 0);

    /* bit6 以外が動いても、bit6 が反転していなければ搭載ではない。 */
    CHECK(serial_fifo_detected(0x41, 0x46) == 0);
    CHECK(serial_fifo_detected(0x01, 0x06) == 0);
}

/* ------------------------------------------------------------------ */
/*  (h) 出せない速度は適用しない (Codex レビュー blocker 2a)            */
/*                                                                      */
/*  以前は「WARN を出して実効値を適用」だった。それだと FIFO 非搭載機で  */
/*  `serial 115200` を打つと 2.4576MHz 系では count=1 = 153600bps が     */
/*  入ってしまい、ホストは 115200 へ移る。**戻すための `serial 9600`     */
/*  すら届かなくなる。** 呼び出し側が「適用しない」を選べるように、      */
/*  serial_plan は exact を必ず立てる/落とす。                           */
/* ------------------------------------------------------------------ */
static void refuse_inexact(void)
{
    struct serial_plan_out p;

    /* 1.9968MHz の 38400 → 拒否の材料 (exact=0)。実効は 41600。 */
    serial_plan(38400UL, 0, CLK_1997, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.exact == 0);
    CHECK(p.actual == 41600UL);

    /* 2.4576MHz の 38400 → そのまま適用してよい (count 4 ちょうど)。 */
    serial_plan(38400UL, 0, CLK_2458, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.exact == 1);
    CHECK(p.count == 4);

    /* **FIFO 無しの 115200 は 2.4576MHz でも拒否**。count=1 = 153600 で、
     * これを黙って入れると会話が二度と戻らない。 */
    serial_plan(115200UL, 0, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.exact == 0);
    CHECK(p.actual == 153600UL);
    CHECK(p.actual != 115200UL);

    /* 1.9968MHz でも同じ (124800 / 115200 = 1.08 → count 1 → 124800)。 */
    serial_plan(115200UL, 0, CLK_1997, 1, &p);
    CHECK(p.exact == 0);
    CHECK(p.actual == 124800UL);

    /* **FIFO があれば 115200 は exact** = 拒否されない。 */
    serial_plan(115200UL, 1, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_VFAST);
    CHECK(p.exact == 1);

    /* 起動時の既定 9600 は**どちらのクロックでも exact** — 拒否の分岐を
     * 通らない。ここが崩れると実機が起動時からシリアルを持たなくなる。 */
    serial_plan(9600UL, 0, CLK_1997, 0, &p);
    CHECK(p.exact == 1);
    serial_plan(9600UL, 0, CLK_2458, 0, &p);
    CHECK(p.exact == 1);
    serial_plan(9600UL, 1, CLK_1997, 0, &p);
    CHECK(p.exact == 1);
    serial_plan(9600UL, 1, CLK_2458, 0, &p);
    CHECK(p.exact == 1);
}

/* ------------------------------------------------------------------ */
/*  (i) 切替後の番犬 — 明示の合図 `serial ack` (往復 4 で設計変更)      */
/*                                                                      */
/*  往復 1〜3 は「受信した」「1 行往復した」を証拠にしようとして、その   */
/*  たびに穴が出た。**推定をやめて合図を明示にした**ので、ここで見るのは */
/*  「ack が来たか」「期限が来たか」「抜けるときどうするか」の 3 つだけ。 */
/* ------------------------------------------------------------------ */
static void watchdog(void)
{
    struct serial_watchdog w;

    /* ---- 判定そのもの ---- */
    /* 切替直後。ack がまだで期限内 → 待つ。 */
    CHECK(serial_watchdog_decide(0, 0) == SER_WD_WAIT);
    CHECK(serial_watchdog_decide(1, 0) == SER_WD_WAIT);
    CHECK(serial_watchdog_decide(SER_SWITCH_WATCHDOG_TICKS - 1, 0)
          == SER_WD_WAIT);

    /* ack が来れば解除。 */
    CHECK(serial_watchdog_decide(0, 1) == SER_WD_LINKED);
    CHECK(serial_watchdog_decide(10, 1) == SER_WD_LINKED);

    /* 期限まで ack なし → 元の設定へ戻す。 */
    CHECK(serial_watchdog_decide(SER_SWITCH_WATCHDOG_TICKS, 0)
          == SER_WD_REVERT);
    CHECK(serial_watchdog_decide(SER_SWITCH_WATCHDOG_TICKS + 100, 0)
          == SER_WD_REVERT);

    /* **ack は期限より先に見る。** 期限ちょうどに届いた ack を「無音だった」
     * と読み替えて戻すと、揃った足並みを自分で壊す。 */
    CHECK(serial_watchdog_decide(SER_SWITCH_WATCHDOG_TICKS, 1)
          == SER_WD_LINKED);
    CHECK(serial_watchdog_decide(0xFFFFFFFFUL, 1) == SER_WD_LINKED);

    /* 期限は 5 秒 (PIT 100Hz)。ホストは切替後 1.5 秒以内に `serial ack` を
     * 投げはじめ 0.5 秒ごとに繰り返すので、期限内に 8 回ほど機会がある。 */
    CHECK(SER_SWITCH_WATCHDOG_TICKS == 500);

    /* 3 つの答えは別の値 (呼び手が待つ/解除/戻すを区別できること)。 */
    CHECK(SER_WD_WAIT != SER_WD_LINKED);
    CHECK(SER_WD_WAIT != SER_WD_REVERT);
    CHECK(SER_WD_LINKED != SER_WD_REVERT);

    /* ---- 状態つきの口 ---- */
    serial_watchdog_arm(&w, 1000UL, 0UL, 9600UL);
    CHECK(w.armed == 1);
    CHECK(w.acked == 0);
    CHECK(w.prev_baud == 9600UL);
    CHECK(serial_watchdog_poll(&w, 1000UL) == SER_WD_WAIT);

    /* **切替行そのものでは下りない。** 切替の応答を送っただけでは ack では
     * ないので、番犬は仕掛かったまま (往復 3 ① が構造的に起きない)。 */
    CHECK(serial_watchdog_poll(&w, 1000UL + 100UL) == SER_WD_WAIT);

    /* `serial ack` を受けて実行 → 解除。 */
    serial_watchdog_ack(&w);
    CHECK(w.acked == 1);
    CHECK(serial_watchdog_poll(&w, 1000UL) == SER_WD_LINKED);
    /* 解除したら下りる。**2 度は返らない。** */
    CHECK(w.armed == 0);
    CHECK(serial_watchdog_poll(&w, 9999UL) == SER_WD_WAIT);

    /* ---- 期限切れの筋書き ---- */
    serial_watchdog_arm(&w, 1000UL, 0UL, 9600UL);
    CHECK(serial_watchdog_poll(&w, 1000UL + SER_SWITCH_WATCHDOG_TICKS - 1)
          == SER_WD_WAIT);
    CHECK(serial_watchdog_poll(&w, 1000UL + SER_SWITCH_WATCHDOG_TICKS)
          == SER_WD_REVERT);
    /* **REVERT も 2 度は返らない** — 返すと戻したあとにもう一度
     * serial_init を呼んでしまう。 */
    CHECK(w.armed == 0);
    CHECK(serial_watchdog_poll(&w, 1000UL + SER_SWITCH_WATCHDOG_TICKS + 500)
          == SER_WD_WAIT);

    /* ---- 仕掛かっていないときは ack の印を立てない ---- */
    serial_watchdog_ack(&w);
    CHECK(w.acked == 0);
    CHECK(w.armed == 0);

    /* arm は ack を 0 に戻す (前の切替の ack が次で即 LINKED にしない)。 */
    serial_watchdog_arm(&w, 2000UL, 0UL, 9600UL);
    serial_watchdog_ack(&w);
    CHECK(w.acked == 1);
    serial_watchdog_arm(&w, 3000UL, 0UL, 9600UL);
    CHECK(w.acked == 0);
    CHECK(serial_watchdog_poll(&w, 3000UL) == SER_WD_WAIT);

    /* ---- NULL を渡しても落ちない ---- */
    serial_watchdog_arm((struct serial_watchdog *)0, 0UL, 0UL, 0UL);
    serial_watchdog_ack((struct serial_watchdog *)0);
    CHECK(serial_watchdog_poll((struct serial_watchdog *)0, 0UL)
          == SER_WD_WAIT);
    CHECK(serial_watchdog_leave((struct serial_watchdog *)0) == SER_WD_WAIT);

    /* ---- tick が一周しても壊れない (u32 の巻き戻り) ---- */
    serial_watchdog_arm(&w, 0xFFFFFF00UL, 0UL, 9600UL);
    CHECK(serial_watchdog_poll(&w, 0xFFFFFF00UL + 10UL) == SER_WD_WAIT);
}

/* ------------------------------------------------------------------ */
/*  (i2) rshell を抜けるとき (往復 4 B4)                                */
/*                                                                      */
/*  抜けたあとは `ser_wd_poll()` を呼ぶ者が居ない。番犬が仕掛かったまま   */
/*  忘れられると、**確認の取れていない速度のまま会話が死ぬ**。            */
/*  期限を待たずにその場で戻す — 待っても誰も見に来ないのだから。         */
/* ------------------------------------------------------------------ */
static void watchdog_leave(void)
{
    struct serial_watchdog w;

    /* 仕掛かっていて未確認 → **期限前でも戻す**。 */
    serial_watchdog_arm(&w, 1000UL, 0UL, 9600UL);
    CHECK(serial_watchdog_leave(&w) == SER_WD_REVERT);
    /* 戻したら下りる (2 度は返らない)。 */
    CHECK(w.armed == 0);
    CHECK(serial_watchdog_leave(&w) == SER_WD_WAIT);

    /* 確認済みなら何もしない (速度はそのままでよい)。 */
    serial_watchdog_arm(&w, 1000UL, 0UL, 9600UL);
    serial_watchdog_ack(&w);
    CHECK(serial_watchdog_leave(&w) == SER_WD_WAIT);
    CHECK(w.armed == 0);

    /* 一度 poll で解除済みなら、抜けるときには何もしない。 */
    serial_watchdog_arm(&w, 1000UL, 0UL, 9600UL);
    serial_watchdog_ack(&w);
    CHECK(serial_watchdog_poll(&w, 1000UL) == SER_WD_LINKED);
    CHECK(serial_watchdog_leave(&w) == SER_WD_WAIT);

    /* 一度 poll で戻し済みなら、抜けるときに二重に戻さない。 */
    serial_watchdog_arm(&w, 1000UL, 0UL, 9600UL);
    CHECK(serial_watchdog_poll(&w, 1000UL + SER_SWITCH_WATCHDOG_TICKS)
          == SER_WD_REVERT);
    CHECK(serial_watchdog_leave(&w) == SER_WD_WAIT);

    /* **仕掛けていなければ何もしない。** ローカル CUI で `serial N` を
     * 打ったあと `rshell` に入って抜けても、そこで速度が戻ってはいけない
     * (CUI の切替は arm しない = 戻す相手が居ない。往復 4 B3)。 */
    w.armed = 0;
    w.acked = 0;
    CHECK(serial_watchdog_leave(&w) == SER_WD_WAIT);
}

/* ------------------------------------------------------------------ */
/*  (g) 実機の筋書き: 9600 起動 → 115200 → 9600 へ戻す                  */
/* ------------------------------------------------------------------ */
static void real_hw_story(void)
{
    struct serial_plan_out p;

    /* 1. 起動。ui.c が serial_init(9600) を呼ぶ = 明示指定なし。
     *    FIFO 搭載機でも互換のまま (票の決裁)。 */
    serial_plan(9600UL, 1, CLK_2458, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.count == 16);
    CHECK(p.exact == 1);
    /* 送信の予算は 2083µs。従来は 10ms tick を待っていたので 490B/s。 */
    CHECK(serial_tx_budget_us(p.actual) == 2083);

    /* 2. `serial 115200` = 明示指定。FIFO があるので V･FAST。 */
    serial_plan(115200UL, 1, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_VFAST);
    CHECK(p.div == SER_VFAST_DIV_115200);
    CHECK((SER_VFAST_ENABLE | p.div) == 0x81);   /* 013Ah へ書く値 */
    CHECK(serial_tx_budget_us(p.actual) == 173);

    /* 3. `serial 9600` = 互換へ戻す。013Ah bit7=0 / 0138h=0 を先に書く。 */
    serial_plan(9600UL, 1, CLK_2458, 0, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.count == 16);
    CHECK(serial_tx_budget_us(p.actual) == 2083);

    /* 4. FIFO の無い古い機種で `serial 115200` を打った場合。
     *    V･FAST には入らず、互換で出せる最速 (153600) になる。
     *    **exact が 0 なので呼び出し側は WARN を出せる。** */
    serial_plan(115200UL, 0, CLK_2458, 1, &p);
    CHECK(p.mode == SER_MODE_COMPAT);
    CHECK(p.exact == 0);
    CHECK(p.actual != 115200UL);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "vfast_table")) vfast_table();
    else if (!strcmp(argv[1], "compat_exact")) compat_exact();
    else if (!strcmp(argv[1], "compat_inexact")) compat_inexact();
    else if (!strcmp(argv[1], "mode_choice")) mode_choice();
    else if (!strcmp(argv[1], "tx_budget")) tx_budget();
    else if (!strcmp(argv[1], "tx_budget_ticks")) tx_budget_ticks();
    else if (!strcmp(argv[1], "status_bits")) status_bits();
    else if (!strcmp(argv[1], "fifo_detect")) fifo_detect();
    else if (!strcmp(argv[1], "refuse_inexact")) refuse_inexact();
    else if (!strcmp(argv[1], "watchdog")) watchdog();
    else if (!strcmp(argv[1], "watchdog_leave")) watchdog_leave();
    else if (!strcmp(argv[1], "real_hw_story")) real_hw_story();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
