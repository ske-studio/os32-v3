/* ======================================================================== */
/*  SERIAL_PLAN.C — シリアルの「速度をどう出すか」の純粋な判定              */
/*                                                                          */
/*  ここには副作用のある行を 1 つも置かない。drivers/serial.c から切り出して */
/*  あるのは、**NP21/W が通信速度を模擬しない** 判定をホストで試験するため。 */
/*    試験: tools/tests/test_serial_vfast.py                                */
/*    記録: tools/tests/serial_vfast_tdd.md                                 */
/*                                                                          */
/*  出典: docs/hw/undocumented/io_rs.md (0130h〜013Ah の各項)               */
/*        NP21/W src/io/serial.c rs232c_vfast_setrs232cspeed / i132 / i136  */
/* ======================================================================== */

#include "serial_plan.h"

/* ======================================================================== */
/*  V･FAST の速度→分周表 (013Ah bit3-0)                                     */
/*                                                                          */
/*  資料 (io_rs.md 352〜380 行) と NP21/W の speedtbl は完全に一致する。     */
/*  資料に無い速度は **入れてはいけない** — V･FAST は bit7 を立てた時点で   */
/*  8253 と無関係になるので、間違えると 8253 側の設定では戻せない。         */
/* ======================================================================== */
struct ser_vfast_entry {
    unsigned long baud;
    u8 div;
};

static const struct ser_vfast_entry ser_vfast_table[] = {
    { 115200UL, SER_VFAST_DIV_115200 },
    {  57600UL, SER_VFAST_DIV_57600  },
    {  38400UL, SER_VFAST_DIV_38400  },
    {  28800UL, SER_VFAST_DIV_28800  },
    {  19200UL, SER_VFAST_DIV_19200  },
    {  14400UL, SER_VFAST_DIV_14400  },
    {   9600UL, SER_VFAST_DIV_9600   }
};

#define SER_VFAST_TABLE_N \
    ((int)(sizeof(ser_vfast_table) / sizeof(ser_vfast_table[0])))

int serial_vfast_div(unsigned long baud)
{
    int i;

    for (i = 0; i < SER_VFAST_TABLE_N; i++) {
        if (ser_vfast_table[i].baud == baud) {
            return (int)ser_vfast_table[i].div;
        }
    }
    /* 表に無い = V･FAST では出せない。0 を返して呼び手に互換へ落とさせる。 */
    return 0;
}

/* ======================================================================== */
/*  速度の出し方を決める                                                    */
/* ======================================================================== */
void serial_plan(unsigned long baud, int has_fifo, unsigned long clk,
                 int want_vfast, struct serial_plan_out *out)
{
    unsigned long count;
    int div;

    if (out == 0) {
        return;
    }
    out->mode = SER_MODE_COMPAT;
    out->actual = 0;
    out->count = 0;
    out->div = 0;
    out->exact = 0;

    if (baud == 0) {
        baud = SER_BAUD_DEFAULT;
    }

    /* ---- V･FAST に入れるか ----
     * 3 つ揃ったときだけ。1 つでも欠けたら黙って互換へ落とす。
     *   has_fifo   : 013Ah / 0138h は FIFO 搭載機のレジスタ (資料 304〜323 行)。
     *                無い機種で bit7 を立てても速度は変わらないので、
     *                こちらだけ 115200 のつもりで喋って会話が死ぬ。
     *   want_vfast : 票の決裁 — 起動時の既定 9600 は実機で通った互換経路を守る。
     *   表にある速度: 上の表のとおり。 */
    if (want_vfast && has_fifo) {
        div = serial_vfast_div(baud);
        if (div > 0) {
            out->mode = SER_MODE_VFAST;
            out->div = (u8)div;
            out->actual = baud;
            out->exact = 1;
            /* 8253 は触らない (bit7 を立てるとカウンタ#2 出力と無関係になる。
             * 資料 013Ah の解説)。count は 0 のまま = 「書かない」の印。 */
            return;
        }
    }

    /* ---- 互換モード: 8253 カウンタ#2 の整数分周 ----
     * 分周比は整数しか設定できないので、割り切れない速度は必ずずれる。
     * 例: 1.9968MHz で 38400 を頼むと count=3 になり実効 41600bps (+8.3%)。
     * UART の許容 (±3% 程度) を超えるので実機では通らない ([V4]: 黙って
     * ずれたまま進まない)。 */
    if (clk == 0) {
        /* クロックが分からない = 何も決められない。exact=0 のまま返す。 */
        return;
    }
    count = clk / SER_CLK_DIVISOR / baud;
    if (count == 0) {
        count = 1;
    }
    if (count > (unsigned long)SER_COUNT_MAX) {
        count = (unsigned long)SER_COUNT_MAX;
    }
    out->count = (u16)count;
    out->actual = clk / SER_CLK_DIVISOR / count;
    out->exact = (u8)((out->actual == baud) ? 1 : 0);
}

/* ======================================================================== */
/*  TxRDY を待つ予算 [µs]                                                   */
/* ======================================================================== */
u32 serial_tx_budget_us(unsigned long baud)
{
    unsigned long us;

    if (baud == 0) {
        baud = SER_BAUD_DEFAULT;
    }
    us = (SER_TX_BUDGET_CHARS * SER_CHAR_BITS * SER_US_PER_SEC) / baud;
    /* **0 にしない** — 1 回も TxRDY を見ないうちに hlt へ落ちると、
     * 10ms tick を待たないようにした意味が消える。 */
    if (us < SER_TX_BUDGET_MIN_US) {
        us = SER_TX_BUDGET_MIN_US;
    }
    if (us > SER_TX_BUDGET_MAX_US) {
        us = SER_TX_BUDGET_MAX_US;
    }
    return (u32)us;
}

/* ======================================================================== */
/*  TxRDY を待つ予算 [tick]                                                 */
/*                                                                          */
/*  **µs の数え上げをやめて tick で測る** 理由は serial_plan.h の注記。      */
/*  tick は「境界を何回跨いだか」なので、開始が tick の途中だと最初の 1 回は */
/*  0〜10ms のどこでも立つ。保証される実時間は (N-1) tick 分なので、欲しい   */
/*  時間の切り上げに 1 を足す。                                             */
/* ======================================================================== */
u32 serial_tx_budget_ticks(unsigned long baud)
{
    unsigned long us;
    unsigned long ticks;

    us = (unsigned long)serial_tx_budget_us(baud);
    /* 切り上げ。 */
    ticks = (us + SER_TICK_US - 1UL) / SER_TICK_US;
    /* 境界ずれのぶん。 */
    ticks += SER_TX_BUDGET_EDGE_TICKS;
    /* 下限。相手のフロー制御や FTDI の遅延タイマ (16ms) をまたげる長さ。 */
    if (ticks < SER_TX_BUDGET_TICKS_MIN) {
        ticks = SER_TX_BUDGET_TICKS_MIN;
    }
    return (u32)ticks;
}

/* ======================================================================== */
/*  FIFO 搭載判定 (0136h を 2 回読む)                                       */
/*                                                                          */
/*  資料 304〜323 行: bit6 は「読み出すたびに 1→0→1→…と変化する」、        */
/*  bit5 は「常に 0 を返す」。NP21/W の rs232c_i136 も `port136 ^= 0x40`。   */
/*                                                                          */
/*  **FFh かどうかで搭載を判断してはいけない** (io_fdd.md / io_rs.md の      */
/*  注意: デコードイメージが出る機種がある) が、未実装ポートの 0xFF は       */
/*  bit6 が反転しないのでこの判定なら安全に非搭載へ落ちる。                  */
/* ======================================================================== */
int serial_fifo_detected(u8 first, u8 second)
{
    /* 識別ビット2 (bit5) はどちらの読みでも 0 でなければならない。 */
    if ((first & SER_IIR_ID2) != 0 || (second & SER_IIR_ID2) != 0) {
        return 0;
    }
    /* 識別ビット1 (bit6) が反転していること。 */
    if ((first & SER_IIR_ID1) == (second & SER_IIR_ID1)) {
        return 0;
    }
    return 1;
}

/* ======================================================================== */
/*  モードに対応するポートとビットマスク                                    */
/*                                                                          */
/*  **互換 (0032h) と FIFO (0132h) はビット位置が違う。**                    */
/*    互換 0032h: bit0 TxRDY / bit1 RxRDY / bit2 TxEMP                      */
/*    FIFO 0132h: bit0 TxEMP / bit1 TxRDY / bit2 RxRDY                      */
/*  取り違えると「送信バッファが空になるたびに受信データを読みに行く」に     */
/*  なる (0x04 が両方に存在するので落ちずに壊れる)。選択子を通して 1 か所に  */
/*  閉じ込める。                                                            */
/* ======================================================================== */
unsigned int serial_data_port(int mode)
{
    return (mode == SER_MODE_VFAST) ? SER_FIFO_DATA : SER_DATA;
}

unsigned int serial_cmd_port(int mode)
{
    /* FIFO モードではコマンドの書き込みも 0132h。資料には「ラインステータス
     * レジスタ [READ]」としか無いが、NP21/W は 0132h の out を 0032h と
     * **同じハンドラ** (rs232c_o32) に繋いでいる (src/io/serial.c
     * rs232c_bind)。資料 0138h の関連欄も 0030h / 0032h を挙げている。 */
    return (mode == SER_MODE_VFAST) ? SER_FIFO_STS : SER_CMD;
}

u8 serial_txrdy_mask(int mode)
{
    return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_TXRDY : STS_TXRDY);
}

u8 serial_rxrdy_mask(int mode)
{
    return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_RXRDY : STS_RXRDY);
}

u8 serial_err_mask(int mode)
{
    return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_ERR
                                         : (u8)(STS_PE | STS_OE | STS_FE));
}

u8 serial_oe_mask(int mode)
{
    return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_OE : STS_OE);
}

u8 serial_fe_mask(int mode)
{
    return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_FE : STS_FE);
}

u8 serial_pe_mask(int mode)
{
    return (u8)((mode == SER_MODE_VFAST) ? SER_FSTS_PE : STS_PE);
}
