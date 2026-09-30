/* ======================================================================== */
/*  SERIAL_PLAN.H — シリアルの「速度をどう出すか」の純粋な判定              */
/*                                                                          */
/*  I/O も tick も触らない決め事だけを drivers/serial.c から切り出してある。 */
/*  実機でしか踏めない分岐 (8253 の整数分周、V･FAST の分周表、TxRDY の      */
/*  待ち予算) をホストで試験するため — **NP21/W は通信速度を模擬しない**    */
/*  ので、ここは「エミュレータで確認済み」が通用しない。                     */
/*                                                                          */
/*    試験: tools/tests/test_serial_vfast.py                                */
/*    記録: tools/tests/serial_vfast_tdd.md                                 */
/*    資料: docs/hw/undocumented/io_rs.md (0130h〜013Ah)                    */
/*    票  : docs/archive/realhw_v21/TASK_SERIAL_VFAST.md                          */
/* ======================================================================== */

#ifndef __SERIAL_PLAN_H
#define __SERIAL_PLAN_H

#include "serial.h"   /* ポート番地とステータスビット (互換 / FIFO の両方) */

#include "os32_kapi_shared.h"

/* ======== 送信 1 バイトの結果 ========
 * **「送れなかった」を黙って捨てない** ([V4])。rshell の番犬は「応答の EOT を
 * 送り終えた」ことを往復の証拠にしているので、捨てると「応答したつもり」で
 * 解除してしまう (Codex レビュー往復 3 ③)。 */
#define SER_TX_OK       KAPI_SER_TX_OK
#define SER_TX_DROPPED  KAPI_SER_TX_DROPPED

/* ======== 通信モードと初期化の結果 ========
 * **値の正典は共有の契約ヘッダ** `sdk/include/os32/os32_kapi_shared.h`。
 * カーネルとユーザランド (シェルの `serial` コマンド) の両方が同じ値を
 * 見るので、片方に写さない ([C4])。ここは短い別名を作るだけ。
 *
 * **REFUSED は「何もしなかった」** — ハードウェアには 1 バイトも書いていない
 * ので、いまの設定がそのまま生き残る。呼び手は速度を変えていない前提で
 * 続けてよい (Codex レビュー blocker 2)。 */
#define SER_MODE_COMPAT  KAPI_SER_MODE_COMPAT
#define SER_MODE_VFAST   KAPI_SER_MODE_VFAST

#define SER_INIT_VFAST   KAPI_SER_INIT_VFAST
#define SER_INIT_COMPAT  KAPI_SER_INIT_COMPAT
#define SER_INIT_REFUSED KAPI_SER_INIT_REFUSED


/* ======================================================================== */
/*  TxRDY を待つ予算                                                        */
/*                                                                          */
/*  直す前は「100 回スピン → `hlt` で次の 10ms tick まで寝る」だった。      */
/*  1 文字ごとに最悪 10ms 寝るので、9600 で 490B/s、38400 では 233B/s と     */
/*  **回線より遅くなる** (票 §0)。回線が 1 文字を押し出す時間は baud で     */
/*  決まるのだから、その時間だけ見てから寝ればよい。                        */
/*                                                                          */
/*    8N1 の 1 文字 = start 1 + data 8 + stop 1 = 10 ビット                 */
/*    予算 = 2 文字分 = 2 × 10 × 1e6 / baud [µs]                            */
/*      9600   → 2083µs / 38400 → 520µs / 115200 → 173µs                   */
/*                                                                          */
/*  予算のあいだは `cpu_delay_us(SER_TX_POLL_US)` を挟んで TxRDY を見る。    */
/*  超えたら従来どおり `hlt` (最大 SER_TX_HLT_RETRY 回) — 相手が            */
/*  ハードウェアフロー制御で止めている場合に CPU を焼かないため。           */
/* ======================================================================== */
#define SER_CHAR_BITS         10UL    /* 8N1 の 1 文字 = 10 ビット */
#define SER_TX_BUDGET_CHARS    2UL    /* 予算は 1 文字時間の 2 倍 */
#define SER_US_PER_SEC   1000000UL

/* ポーリングの刻み。短すぎると I/O ポートの読みで CPU を占有し、長すぎると
 * 115200 の予算 (173µs) を数回で使い切る。5µs なら 115200 でも 30 回以上見る。
 * cpu_delay_us の精度は ±10% 程度 (kernel/cpu_calibrate.h) なのでこれ以上
 * 細かくしても意味がない。 */
#define SER_TX_POLL_US        5UL
/* 予算の下限。**0 にしてはいけない** — 1 回も見ないうちに hlt へ落ちると
 * 直した意味が消える。刻みの 8 倍を下限にする。 */
#define SER_TX_BUDGET_MIN_US 40UL
/* 予算の上限。異常に遅い速度を渡されても 1 文字で 50ms は待たない
 * (従来の hlt 1 回分 = 10ms の 5 倍)。 */
#define SER_TX_BUDGET_MAX_US 50000UL
/* 予算を使い切ったあと `hlt` で待つ回数 (従来と同じ)。 */
#define SER_TX_HLT_RETRY      5

/* ======================================================================== */
/*  予算は **µs の数え上げではなく tick で測る** (往復 3)                   */
/*                                                                          */
/*  `waited += SER_TX_POLL_US` は「cpu_delay_us(5) が本当に 5µs 待つ」に     */
/*  寄りかかった数え方だった。実機 PC-9821Ra266 では校正が丸めに負けて       */
/*  cpu_delay_us(5) が 0.5µs しか待たず、2083µs のつもりの予算が実際には     */
/*  約 200µs で尽き、9600 の 1 文字時間 (1.04ms) すら待てずに `_halt()` へ   */
/*  落ちていた。その hlt が実測 ≒2ms で、**速度に依らない 1 バイト 2ms の    */
/*  固定費** (9600 で 389B/s、38400 でも 437B/s) の正体だった。             */
/*                                                                          */
/*  校正は往復 3 で直したが、**時間の判定を校正に依存させない**。            */
/*  `tick_count` は PIT の割り込みが進める実時間なので、校正が何倍ずれても   */
/*  予算は狂わない。cpu_delay_us を挟むのは「ポートを読む間隔」を空ける      */
/*  ためだけで、正確さは要らない。                                          */
/* ======================================================================== */
#define SER_TICK_US       10000UL   /* PIT 100Hz = 1 tick 10ms */

/* tick は「境界を何回跨いだか」なので、開始が tick の途中だと最初の 1 回は
 * 0〜10ms のどこでも立つ。**保証される実時間は (N-1) tick 分**なので、
 * 欲しい時間の切り上げに 1 を足す。 */
#define SER_TX_BUDGET_EDGE_TICKS  1UL
/* 下限 3 tick (= 保証 20ms 以上)。9600 の 1 文字は 1.04ms なので 2 文字ぶんの
 * 予算 (2083µs) には 1 tick でも足りるが、**TxRDY が立つのを待つ相手は回線
 * だけではない** (ホスト側のフロー制御・FTDI の遅延タイマ 16ms)。16ms を
 * またげる長さにしておかないと、結局 hlt に落ちて元の木阿弥になる。 */
#define SER_TX_BUDGET_TICKS_MIN   3UL

/* **IF=0 で呼ばれたときの回数上限。** 割り込み禁止区間では tick_count が
 * 進まないので時間で測れない。しかも `_halt()` は IF=0 では二度と起きない
 * ので、スピンだけで諦めるしかない。
 *
 * 1 周は `cpu_delay_us(SER_TX_POLL_US)` + ポート読み 1 回なので、**校正が
 * 正しければ 1 周 ≒ 5µs**。合計を 20ms 相当 (= 予算の下限 3 tick と同じ桁)
 * に揃えると 4000 回。校正を直す前 (往復 3) は 1 周 0.5µs だったので 20 万回
 * でも 0.1 秒だったが、**校正が正しくなったいま 20 万回は 1 秒/文字**になり、
 * パニック経路で画面が止まる。 */
#define SER_TX_SPIN_BUDGET_US 20000UL
#define SER_TX_SPIN_MAX      (SER_TX_SPIN_BUDGET_US / SER_TX_POLL_US)

/* 8253 カウンタ#2 は 16 ビット。 */
#define SER_COUNT_MAX    0xFFFFU
/* 8251 の ×16 モード (MOD_CLKx16) なので、ボーレートはクロック/16/count。 */
#define SER_CLK_DIVISOR  16UL
/* 速度を渡されなかったときの既定 (include/config.h の SYS_SERIAL_BAUD と
 * 同じ値。ドライバはカーネル設定ヘッダを見ないのでここに持つ)。 */
#define SER_BAUD_DEFAULT 9600UL

/* 決めた結果。**要求どおりに出ないことがあるので呼び手に見せる** ([V4])。 */
struct serial_plan_out {
    int  mode;             /* SER_MODE_COMPAT / SER_MODE_VFAST */
    unsigned long actual;  /* 実際に出る速度 */
    u16  count;            /* 8253 カウンタ#2 (COMPAT のときだけ / VFAST は 0) */
    u8   div;              /* 013Ah bit3-0 (VFAST のときだけ / COMPAT は 0) */
    u8   exact;            /* 1 = 要求どおり出る / 0 = ずれている */
};

/* V･FAST の分周値 (013Ah bit3-0)。表に無い速度は 0。 */
int serial_vfast_div(unsigned long baud);

/* 速度の出し方を決める。
 *   has_fifo   0136h の判定 (serial_fifo_detected)
 *   clk        8253 のタイマクロック (TIMER_CLK_1997 / TIMER_CLK_2458)
 *   want_vfast 呼び手が V･FAST を明示的に頼んだか (票の決裁: 起動時の
 *              既定 9600 は互換のまま。V･FAST は `serial N` で明示的に入る)
 * V･FAST に入るのは **has_fifo かつ want_vfast かつ表にある速度** のときだけ。
 * それ以外は 8253 の整数分周 (割り切れなければ exact = 0)。 */
void serial_plan(unsigned long baud, int has_fifo, unsigned long clk,
                 int want_vfast, struct serial_plan_out *out);

/* TxRDY を待つ予算 [µs]。上の式そのもの (tick 版の材料)。 */
u32 serial_tx_budget_us(unsigned long baud);

/* TxRDY を待つ予算 [tick]。`serial_putchar` が実際に使うのはこちら。
 *   ceil(2 文字時間 / 10ms) + 1 (境界ずれ)、下限 SER_TX_BUDGET_TICKS_MIN
 * **0 にも 1 にもならない** — 1 tick では保証される実時間が 0 になりうる。 */
u32 serial_tx_budget_ticks(unsigned long baud);

/* 0136h を 2 回読んだ値から FIFO 搭載を判定する (資料 304〜323 行)。
 * bit6 が反転し、かつ bit5 がどちらも 0 なら搭載。
 * 未実装ポートの 0xFF も 0x00 固定も、この判定なら非搭載に落ちる。 */
int serial_fifo_detected(u8 first, u8 second);

/* モードに対応するポートとビットマスク。**互換と FIFO でビット位置が違う**
 * ので、選択子を通して取り違えを 1 か所に閉じ込める。 */
unsigned int serial_data_port(int mode);
unsigned int serial_cmd_port(int mode);
u8 serial_txrdy_mask(int mode);
u8 serial_rxrdy_mask(int mode);
u8 serial_err_mask(int mode);
/* 誤りの内訳 (ISR が数える、§1-v2「ISR の計数」)。互換 0032h は D4 OE /
 * D5 FE / D3 PE。FIFO 0132h は **資料と NP21/W が食い違う** (serial.h の
 * SER_FSTS_ERR の注記) ので NP21/W の並び (bit4 OE / bit5 FE / bit3 PE) を採る。
 * OE (bit4) は両者一致、FE と PE は目安。 */
u8 serial_oe_mask(int mode);
u8 serial_fe_mask(int mode);
u8 serial_pe_mask(int mode);

#endif /* __SERIAL_PLAN_H */
