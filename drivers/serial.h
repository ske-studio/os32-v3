/* ======================================================================== */
/*  SERIAL.H — PC-98 RS-232C シリアル通信ドライバ定義                       */
/*                                                                          */
/*  μPD8251A (USART) 内蔵RS-232Cポート制御                                 */
/*  出典: PC9800Bible §2-10, FreeBSD sys/pc98/cbus/sio.c                   */
/* ======================================================================== */

#ifndef __SERIAL_H
#define __SERIAL_H

#include "types.h"

/* ======== I/Oポート (FreeBSD if_8251_type[COM_IF_INTERNAL]) ======== */
#define SER_DATA    0x30    /* [0] 送受信データ */
#define SER_CMD     0x32    /* [1] コマンドライト / [2] ステータスリード */
#define SER_SIGNAL  0x33    /* [3] モデム信号線 (CI/CS/CD) */
#define SER_MASK    0x35    /* [4] 割り込みマスク (8255 ポート C)。
                                *     **読むだけ。書くのは 0037h の BSR** —
                                *     bit3-7 に BUZ・SHUT0/1 などが同居 */

/* PIT カウンタ#2 (ボーレート設定) */
#define SER_TIMER_CNT   0x75    /* カウンタ#2 データ */
#define SER_TIMER_MODE  0x77    /* PIT モードレジスタ */
#define SER_IO_WAIT     0x5F    /* I/Oウェイト (FreeBSD準拠) */

/* ======== ステータスビット (ポート0x32 リード) ======== */
#define STS_TXRDY   0x01    /* D0: 第2送信バッファ空 (送信可) */
#define STS_RXRDY   0x02    /* D1: 受信データあり */
#define STS_TXE     0x04    /* D2: 送信バッファ全空 */
#define STS_PE      0x08    /* D3: パリティエラー */
#define STS_OE      0x10    /* D4: オーバーランエラー */
#define STS_FE      0x20    /* D5: フレーミングエラー */
#define STS_BRK     0x40    /* D6: ブレーク検出 */
#define STS_DSR     0x80    /* D7: DSR信号 */

/* ======== コマンドビット (ポート0x32 ライト) ======== */
#define CMD_TXE     0x01    /* D0: 送信イネーブル */
#define CMD_DTR     0x02    /* D1: DTR (負論理) */
#define CMD_RXE     0x04    /* D2: 受信イネーブル */
#define CMD_SBRK    0x08    /* D3: ブレーク送信 */
#define CMD_ER      0x10    /* D4: エラーリセット */
#define CMD_RTS     0x20    /* D5: RTS (負論理) */
#define CMD_RESET   0x40    /* D6: 内部リセット */

/* ======== モードビット (ポート0x32, リセット直後の1回目) ======== */
/* D1-D0: 分周比 */
#define MOD_CLKx1   0x01    /* ×1モード */
#define MOD_CLKx16  0x02    /* ×16モード */
#define MOD_CLKx64  0x03    /* ×64モード */
/* D3-D2: キャラクタ長 */
#define MOD_5BIT    0x00
#define MOD_6BIT    0x04
#define MOD_7BIT    0x08
#define MOD_8BIT    0x0C
/* D4: パリティイネーブル */
#define MOD_PENAB   0x10
/* D5: パリティ種別 */
#define MOD_PEVEN   0x20
/* D7-D6: ストップビット */
#define MOD_STOP1   0x40
#define MOD_STOP15  0x80
#define MOD_STOP2   0xC0

/* ======== 割り込みマスクビット (ポート0x35 の bit0-2) ========
 * 書くときは 0037h の BSR (pc98.h の BSR_RXRE_* / BSR_TXEE_* / BSR_TXRE_*)。 */
#define IEN_RX      0x01    /* D0: 受信レディ割り込み */
#define IEN_TXEMP   0x02    /* D1: 送信エンプティ割り込み */
#define IEN_TX      0x04    /* D2: 送信レディ割り込み */

/* ======== モデム信号ビット (ポート0x33 リード) ======== */
#define SIG_CD      0x20    /* D5: CD (負論理) */
#define SIG_CS      0x40    /* D6: CS/CTS (負論理) */
#define SIG_CI      0x80    /* D7: CI/RI (負論理) */

/* ======================================================================== */
/*  FIFO モード / V･FAST モード (I/O 0130h〜013Ah)                          */
/*                                                                          */
/*  資料: docs/hw/undocumented/io_rs.md 227〜380 行。**Undocumented**。      */
/*  対象: FIFO は An 一部ロット・Np 以降、V･FAST は 115200bps 対応機         */
/*        (95 年 5 月以降の機種は統合 I/O に 16550 相当を内蔵)。            */
/*  搭載判定は 0136h (serial_fifo_detected)。**決め打ちで叩かない。**        */
/*                                                                          */
/*  ⚠ 1st CCU は 8251 のまま。FIFO は前段に入るので、モード/コマンドの      */
/*    書き込み手順そのものは互換モードと同じ (書き先が 0132h になるだけ)。   */
/* ======================================================================== */
#define SER_FIFO_DATA   0x0130  /* 送受信データ (従来互換の 0030h にあたる) */
#define SER_FIFO_STS    0x0132  /* [READ] ラインステータス                  */
                                /* [WRITE] コマンド — 資料には記述が無いが、 */
                                /*   NP21/W は 0032h と同じハンドラに繋ぐ    */
                                /*   (rs232c_bind: 0x132 out → rs232c_o32)。 */
#define SER_FIFO_MSR    0x0134  /* [READ] モデムステータス                  */
#define SER_FIFO_IIR    0x0136  /* [READ] 割り込み参照 + FIFO 搭載識別       */
#define SER_FIFO_FCR    0x0138  /* FIFO コントロール                        */
#define SER_VFAST_REG   0x013A  /* V･FAST モードレジスタ                    */

/* FIFO の深さ (資料 0130h の解説: 16 バイト)。 */
#define SER_FIFO_DEPTH  16

/* ---- 0132h ラインステータス ----
 * **互換の 0032h とビット位置が違う。** 0032h は bit0 TxRDY / bit1 RxRDY /
 * bit2 TxEMP、0132h は bit0 TxEMP / bit1 TxRDY / bit2 RxRDY。
 * 0x04 が両方に (TxEMP / RxRDY として) 存在するので、取り違えても例外は
 * 出ずに「送信が空くたびに受信データを読む」形で静かに壊れる。 */
#define SER_FSTS_TXEMP  0x01    /* bit0: 送信 FIFO 全空 */
#define SER_FSTS_TXRDY  0x02    /* bit1: 送信可 */
#define SER_FSTS_RXRDY  0x04    /* bit2: 受信データあり */
/* bit3〜7 は **資料と NP21/W が食い違う**。
 *   資料 : bit5 パリティ / bit4 オーバーラン / bit7 ブレーク / bit3 不明
 *   NP21/W: bit3 パリティ / bit4 オーバーラン / bit5 フレーミング /
 *           bit6 ブレーク (rs232c_i132 のコメントに「資料 Vol.2 の
 *           bit3〜7 の記載は誤り」と明記)。
 * どちらの並びでも「エラーが 1 つでも立ったらコマンドを打ち直す」だけなので、
 * 両方の解釈で共通して立ちうる bit3〜5 をエラーとして見る。 */
#define SER_FSTS_ERR    0x38
/* 内訳を数えるときは NP21/W の並びを採る (OE は資料とも一致、FE / PE は目安) */
#define SER_FSTS_PE     0x08
#define SER_FSTS_OE     0x10
#define SER_FSTS_FE     0x20

/* ---- 0136h 割り込み参照 / FIFO 搭載識別 ---- */
#define SER_IIR_ID1     0x40    /* bit6: 読むたびに反転する = 搭載の印 */
#define SER_IIR_ID2     0x20    /* bit5: 常に 0 */
#define SER_IIR_NONE    0x01    /* bit0: 1 = 割り込み要因なし */

/* ---- 0138h FIFO コントロール ---- */
#define SER_FCR_ENABLE  0x01    /* FCR0: 1 = FIFO モード / 0 = 従来互換 */
#define SER_FCR_RX_RST  0x02    /* FCR1: 受信 FIFO リセット */
#define SER_FCR_TX_RST  0x04    /* FCR2: 送信 FIFO リセット */
#define SER_FCR_OFF     0x00    /* 従来互換モードへ戻す */

/* ---- 013Ah V･FAST モードレジスタ ----
 * bit7 = 1 で 8253 カウンタ#2 と**無関係**に速度が決まる。bit6-4 は常に 000b。
 * 分周値は資料 352〜380 行の表 (NP21/W の speedtbl と完全に一致)。 */
#define SER_VFAST_ENABLE    0x80
#define SER_VFAST_OFF       0x00
#define SER_VFAST_DIV_MASK  0x0F
#define SER_VFAST_DIV_9600    0x0C
#define SER_VFAST_DIV_14400   0x08
#define SER_VFAST_DIV_19200   0x06
#define SER_VFAST_DIV_28800   0x04
#define SER_VFAST_DIV_38400   0x03
#define SER_VFAST_DIV_57600   0x02
#define SER_VFAST_DIV_115200  0x01

/* ======================================================================== */
/*  システムクロック — 8251 の速度は 8253 TCU カウンタ#2 の分周で決まる      */
/*                                                                          */
/*  判定は BIOS ワークエリア **0000:0501h bit 7**:                          */
/*    1 = 8MHz系    → タイマクロック 1.9968MHz                              */
/*    0 = 5/10MHz系 → タイマクロック 2.4576MHz                              */
/*                                                                          */
/*  **資料が食い違うので根拠を残す** (2026-09-18 に決着):                    */
/*  `docs/hw/undocumented/io_tcu.md` の表は上下 2 行が逆になっているが、     */
/*  `memsys.md` の 0000:0501h の項は自己整合する — 併記された SCLK1 の表が   */
/*  「値1 → 7.9872MHz / 値0 → 9.8304MHz」で、4 分周すると 1.9968 / 2.4576 に */
/*  なる。よって **memsys.md を採る** (io_tcu.md の 2 行は転記ミス)。        */
/*  FreeBSD の pc98_ttspeedtab (8MHz系 = 1996800) も memsys.md と一致する。  */
/*                                                                          */
/*  **ちょうど出る速度はクロックで変わる** (clk/16 が baud で割り切れるか):   */
/*    1.9968MHz (clk/16 = 124800): 9600 のみ。19200 も 38400 も割り切れない  */
/*                                 (38400 → count 3.25 → 実効 41600、+8.3%) */
/*    2.4576MHz (clk/16 = 153600): 9600 / 19200 / 38400 すべてちょうど       */
/* ======================================================================== */
/*  **番地・ビット・クロック値の定義は include/pc98.h 1 か所** ([C4])。      */
/*  ここは従来名の別名だけ。0000:0501h を実際に読むのは kernel/sysclk.c で、 */
/*  シリアルは `sysclk_hz()` / `sysclk_is_8mhz()` の保存値を見る。            */
/*  **このヘッダから pc98.h は include しない** — serial.h を引くだけの TU   */
/*  (kapi/kapi_generated.c) に pc98.h の定数が流れ込むと、tvram.h と         */
/*  TVRAM_BPR が二重定義になって警告が出る。使う側 (drivers/serial.c) が     */
/*  pc98.h を include しているので、別名はそこで展開される。                 */
/* ======================================================================== */
#define TIMER_CLK_1997  SYSCLK_1997   /* 8MHz系 (0501h bit7 = 1) */
#define TIMER_CLK_2458  SYSCLK_2458   /* 5/10MHz系 (0501h bit7 = 0) */

/* ======================================================================== */
/*  拡張RS-232C制御レジスタ (I/O 0434h、Undocumented io_rs.md)              */
/*                                                                          */
/*  PC-9801P･NX/C、PC-9821Af･Ne 以降は 8251 のクロックが 2.4576 →           */
/*  9.8304MHz に変わり、19200bps 対応機には**入力クロック 4 分周回路**が     */
/*  ここに入っている。外せば 4 倍の速度が出せる。                            */
/*                                                                          */
/*  ⚠ **極性が機種依存で、しかも資料に異論が併記されている。**               */
/*    群 A (大半の PC-9821): 1 = 4分周しない / 0 = する (既定)              */
/*    群 B (An･Ap3･As3･Xa･Xt･Xf･Cf･Xa10･Xa9･Xa7･Xt13･Xa12･Xa7e･Na7･Nx):    */
/*          1 = しない (既定) / 0 = する                                    */
/*          — ただし 1999 年の読者指摘で「1 = する (既定) / 0 = しない」が   */
/*            正しいのではないか、と併記されている。**未決着。**             */
/*                                                                          */
/*  ⚠ **bit0 は「プライマリシリアルポート切り離し」。** 雑に書くとポートが   */
/*    消える。必ず read-modify-write で bit0 を保つこと。                    */
/*                                                                          */
/*  だから **自動では触らない。** 実機でどちらの向きか確かめたうえで         */
/*  `serial_set_div4()` を明示的に呼ぶ。                                     */
/* ======================================================================== */
#define SER_EXT_CTRL        0x0434
#define SER_EXT_DIV4        0x40    /* bit6: 入力クロック 4 分周 (極性注意) */
#define SER_EXT_DISCONNECT  0x01    /* bit0: ポート切り離し — 保つ */

/* 設定の結果。**要求どおりに出ないことがあるので呼び手に見せる** ([V4])。 */
struct serial_setup {
    unsigned long want;     /* 要求した速度 */
    unsigned long actual;   /* 実際に出る速度 (clk / 16 / count) */
    unsigned long clk;      /* 使ったタイマクロック */
    u16 count;              /* 8253 カウンタ#2 の分周比 (互換モードのみ) */
    u8  sysclk_8mhz;        /* 0000:0501h bit7 の値 */
    u8  exact;              /* 1 = ちょうど出る / 0 = ずれている */
    u8  has_fifo;           /* 0136h の判定 (1 = FIFO 搭載機) */
    u8  mode;               /* SER_MODE_COMPAT / SER_MODE_VFAST */
    u8  vfast_div;          /* 013Ah bit3-0 (V･FAST のときだけ) */
};

/* ======== 受信バッファ ======== */
#define SER_BUF_SIZE    4096

/* 1 回の IRQ4 で汲む上限 (互換モード)。割り込み嵐で ISR に居座らないため。
 * FIFO モードは FIFO 1 杯 = SER_FIFO_DEPTH で抜ける。 */
#define SER_IRQ_DRAIN_MAX   128

/* serial_puts が io_wait を挟む間隔 (バイト)。**2 の冪であること**
 * (`count & (N-1)` で判定している)。NP21/W のパイプ対策の名残。 */
#define SER_PUTS_WAIT_EVERY 16

/* パニック時のポーリング送信で TxRDY を待つ空回りの上限。
 * 校正 (cpu_delay_us) が当てにならない状況で呼ばれるので回数で切る。 */
#define SER_POLLED_SPIN_MAX 50000

/* ======== 公開API ======== */

/* 直前の `serial_init` の結果。まだ呼ばれていなければ want=0。 */
const struct serial_setup *serial_get_setup(void);

/* I/O 0434h の生読み。-1 = 読めない / 0..255 = 値。
 * **FFh かどうかで搭載を判断しないこと** (00BEh のデコードイメージが出る
 * 機種がある。io_fdd.md / io_rs.md の注意)。 */
int serial_get_ext_ctrl(void);

/* 0434h bit6 を書く (read-modify-write で bit0 を保つ)。
 * 戻りは読み戻した 0434h の値、または -1。
 * **極性は機種依存で未決着** (上の注記)。自動では呼ばれない。 */
int serial_set_div4(int bit_value);

/* 従来どおりの初期化 (**互換モード固定**)。シグネチャも意味も変えていない。
 * FIFO 搭載機でも V･FAST には入らない — 票の決裁「起動時の既定 9600 は
 * 実機で通った互換経路を守る」。V･FAST 中にこれを呼ぶと 013Ah bit7=0 と
 * 0138h=0 を先に書いて互換へ戻す。
 *
 * **8253 でちょうど出せない速度は適用しない** (SER_INIT_REFUSED と同じ判断)。
 * 戻り値が無いので結果は `serial_get_setup()->actual` を見ること。
 * 既定の 9600 は両クロックで exact なので起動経路は変わらない。 */
void serial_init(unsigned long baud);

/* V･FAST (FIFO モード) で初期化する。**明示的に呼んだときだけ入る。**
 *   SER_INIT_VFAST   ( 0) = V･FAST に入った
 *   SER_INIT_COMPAT  (-1) = FIFO 非搭載か表に無い速度 → 互換で初期化した
 *                           (8253 でちょうど出る速度だった)
 *   SER_INIT_REFUSED (-2) = 8253 でも出せない速度 → **何もしていない**。
 *                           いまの設定がそのまま生きている
 * 戻しは serial_init(9600)。 */
int serial_init_vfast(unsigned long baud);

/* 現在の設定を 3 つの数で返す (CPL=3 から KAPI 越しに読むための口。
 * **カーネルのポインタは渡さない** — POLICY_DEBUG §4-13 の db_last_error と
 * 同じ事故を繰り返さないため)。NULL は飛ばす。戻りは 0 = 初期化済み、
 * -1 = まだ serial_init を呼んでいない (値は書くが当てにならない)。
 *   mode: SER_MODE_COMPAT / SER_MODE_VFAST
 *   baud: **実効値** (要求値ではない)
 *   fifo: 0136h の判定 (1 = FIFO 搭載機) */
int serial_get_status(u32 *mode, u32 *baud, u32 *fifo);

/* 1 バイト送る。**戻り値で成否を返す** (Codex レビュー往復 3 ③)。
 *   SER_TX_OK      (0) = UART へ書けた
 *   SER_TX_DROPPED(-1) = TxRDY の予算を使い切って諦めた (相手が読んでいない)
 * 既存の呼び手は戻り値を無視してよい。KAPI の `serial_putchar` は従来どおり
 * void で受ける (`kapi/kapi_generated.c` の wrapper が捨てる) ので、
 * 外部プログラムから見た ABI は変わっていない ([ABI2])。 */
int serial_putchar(char c);
void serial_puts(const char *str);
void serial_puts_polled(const char *str);
void serial_put_hex32_polled(u32 val);
int  serial_getchar(void);     /* ブロッキング */
int  serial_trygetchar(void);  /* ノンブロッキング: -1=なし */
int  serial_peekchar(void);    /* 覗くだけ (取り出さない): -1=なし */
int  serial_has_data(void);    /* 受信バッファにデータがあるか */
int  serial_is_initialized(void);

/* ======================================================================== */
/*  SerialFS セッションのゲート (票 TASK_SERIAL_HOSTFS §1-v3、serial.c の注記) */
/*  使うのは fs/serialfs_session.c だけ。                                   */
/* ======================================================================== */
/* セッション中の出力を溜める保留リング (溢れたら古い方を捨てて数える) */
#define SER_HOLD_SIZE   8192
/* ゲートの上げ下げで UART / FIFO から読み捨てる上限 (FIFO 16 バイト + 8251 の
 * 保持の余裕。相手が送り続けていても抜ける) */
#define SER_GATE_DRAIN_MAX  64

/* ゲートを上げ下げする。どちらでも受信リングを空にする。上げると保留
 * リングも空にする (下ろしても保留リングは残す — 呼び手が汲む)。 */
void serial_gate_set(int on);
int  serial_gate_active(void);
/* ゲートを通らずに線へ出す (SerialFS のフレーム)。SER_TX_OK / DROPPED */
int  serial_gate_put(const u8 *buf, u32 n);
/* ゲートを通らずに 1 バイト受ける (RxRDY を直接見て FIFO の残りも汲む)。
 * -1 = 無い */
int  serial_gate_get(void);
/* 保留リングから最大 max バイト取り出す */
u32  serial_hold_take(u8 *buf, u32 max);
/* 保留リングが溢れて捨てたバイト数 (ゲートを上げると 0) */
u32  serial_hold_dropped(void);
/* 受信の誤りの計数 (ISR とゲートの受信器が数える)。NULL は飛ばす */
void serial_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow);

/* IRQ4ハンドラ (ASMスタブから呼ばれる) */
void serial_irq_handler(void);

#endif /* __SERIAL_H */
