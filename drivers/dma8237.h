/* ======================================================================== */
/*  DMA8237.H — 8237A (µPD8237A 相当) の共通部                              */
/*                                                                          */
/*  いままで DMA の設定は drivers/fdc.c の中に ch2 決め打ちで書かれていた。  */
/*  CS4231 の PCM (#1/#3、auto-init のリング) を載せるには、チャネルを引数に */
/*  取って「検査してから積む」層が要る。ここはその層の**決め事だけ**で、    */
/*  I/O を出すのは drivers/dma8237.c、算数は drivers/dma8237_math.c。        */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_HAL_WIRING.md §1-2                             */
/*  出典: UNDOCUMENTED 9801/9821 Vol.2「DMAコントローラ」                    */
/*        (docs/hw/undocumented/io_dma.md = /mnt/c/WATCOM/docs/undocumented/ */
/*         io_dma.md の 140〜160 行のポート表、205〜380 行の各レジスタ)。    */
/*        ポート番号をここ 1 か所に集める ([C4]) — fdc.h の DMA_CH2_* は     */
/*        この表の ch2 の行と同じもので、fdc.c はもう直に叩かない。          */
/*                                                                          */
/*  試験: tools/tests/test_dma8237.py / 記録 tools/tests/dma8237_tdd.md      */
/* ======================================================================== */

#ifndef DMA8237_H
#define DMA8237_H

#include "types.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* (エラーは共通体系) */

/* ======================================================================== */
/*  I/O ポート (io_dma.md 140〜160 行)。**奇数番地**に並ぶ                  */
/* ======================================================================== */
/*  チャネルごとのカレントアドレス / カレントワードカウント (R/W)。
 *  16bit を下位 → 上位の 2 回に分けて読み書きし、境目は
 *  DMA8237_FF_CLEAR (0019h) で揃える。 */
#define DMA8237_CH0_ADDR    0x01
#define DMA8237_CH0_COUNT   0x03
#define DMA8237_CH1_ADDR    0x05
#define DMA8237_CH1_COUNT   0x07
#define DMA8237_CH2_ADDR    0x09
#define DMA8237_CH2_COUNT   0x0B
#define DMA8237_CH3_ADDR    0x0D
#define DMA8237_CH3_COUNT   0x0F

/*  0011h: 読むとステータス (bit7-4 = RQ3-RQ0 / bit3-0 = TC3-TC0)、
 *  書くとコマンド。**読むと TC ビットは全チャネルぶん消える** —
 *  だからこの層だけが読む (§1-2 の TC の保持)。 */
#define DMA8237_STATUS      0x11   /* R: ステータス / W: コマンド */
#define DMA8237_REQUEST     0x13   /* W: ソフトウェアリクエスト (未使用) */
#define DMA8237_MASK1       0x15   /* W: シングルマスク (bit2 = MK, bit1-0 = ch) */
#define DMA8237_MODE        0x17   /* W: モード */
#define DMA8237_FF_CLEAR    0x19   /* W: バイトポインタ FF クリア (値は任意) */
#define DMA8237_MASTER_CLR  0x1B   /* W: マスタクリア (未使用) */
#define DMA8237_MASK_CLR    0x1D   /* W: 全マスク解除 (未使用) */
#define DMA8237_MASK_ALL    0x1F   /* W: 全マスク一括 (未使用) */

/*  バンクアドレス (W のみ)。**ch0 だけ 0027h で並びが飛ぶ**
 *  (io_dma.md 152〜155 行。ch1 = 0021h, ch2 = 0023h, ch3 = 0025h)。 */
#define DMA8237_CH0_BANK    0x27
#define DMA8237_CH1_BANK    0x21
#define DMA8237_CH2_BANK    0x23
#define DMA8237_CH3_BANK    0x25

/* ======================================================================== */
/*  レジスタのビット (io_dma.md 205〜360 行)                                */
/* ======================================================================== */
/*  0015h シングルマスク: bit2 = 1 でマスク、bit1-0 = チャネル */
#define DMA8237_MASK_SET_BIT   0x04

/*  0017h モード: bit7-6 = MS (PC-98 は 01b = シングル固定)、bit5 = ID
 *  (0 = 番地増分)、bit4 = AT (auto-init)、bit3-2 = TR、bit1-0 = ch。 */
#define DMA8237_MODE_SINGLE_SEL 0x40   /* MS = 01b シングルモード */
#define DMA8237_MODE_AUTOINIT   0x10   /* AT = 1 auto-init 許可 */
#define DMA8237_MODE_TR_WRITE   0x04   /* TR = 01b ライト転送 (I/O → メモリ) */
#define DMA8237_MODE_TR_READ    0x08   /* TR = 10b リード転送 (メモリ → I/O) */

/*  0011h ステータス: 下位 4 ビットが TC3〜TC0 */
#define DMA8237_STATUS_TC_MASK  0x0F

/* ======================================================================== */
/*  この層の値                                                              */
/* ======================================================================== */
#define DMA_CHAN_COUNT     4

#define DMA_DIR_TO_MEM     0   /* 装置 → メモリ (8237 の write transfer) */
#define DMA_DIR_FROM_MEM   1   /* メモリ → 装置 (read transfer) */

#define DMA_MODE_SINGLE    0   /* auto-init 無し (FDC) */
#define DMA_MODE_CYCLIC    1   /* auto-init 有り (PCM のリング) */

/*  1 回の転送で積める上限。カウントレジスタは 16bit で、積むのは
 *  「バイト数 - 1」。0 バイトは積めない (65536 の意味になる)。 */
#define DMA_XFER_MIN_BYTES  1UL
#define DMA_XFER_MAX_BYTES  0x10000UL   /* 64KB */

/*  64KB バンク。バンクレジスタが上位 8bit を持つので、転送は 1 つの
 *  64KB の中に収まらなければならない ([HW2])。 */
#define DMA_BANK_SIZE       0x10000UL
#define DMA_BANK_MASK       0x0FFFFUL
#define DMA_BANK_SHIFT      16

/*  拡張バンク (0E05h〜) はこの票で扱わない。バンクレジスタ 8bit で届く
 *  のは 24bit = 16MB まで。 */
#define DMA_PHYS_LIMIT      0x1000000UL  /* 16MB (この番地以上は拒否) */

/*  dma_chan_remaining の安定読み: 2 回のカウント読みのあいだに進み得る
 *  転送量の上限。これを超える減りは「合成が壊れている」と読む。 */
#define DMA_READ_SLACK      64UL
#define DMA_READ_TRIES      3

/* ======================================================================== */
/*  I/O 0439h — DMA アクセス制御 (Undocumented)                             */
/*                                                                          */
/*  正典: docs/hw/undocumented/io_dma.md の「I/O 0439h DMAアクセス制御等」。 */
/*  対象は 80286 以上の機種 (PC-98XA を除く)。                              */
/*                                                                          */
/*    bit 2: DMA アドレスマスクレジスタ                                     */
/*           1 = 1M バイト以上のアドレスへの DMA アクセス**禁止**           */
/*           0 = 許可                                                       */
/*           **ノーマルモードの起動時設定は 1**(ハイレゾは 0)。             */
/*                                                                          */
/*  OS32 の dma_buffer はカーネル BSS (1MB 超) に置かれるので、立ったまま    */
/*  だと READ DATA が正常終了してもデータがバッファに届かない。             */
/*                                                                          */
/*  **必ず read-modify-write で他のビットを保つこと。** bit7 はプリンタ I/F  */
/*  選択で、0 を書くと本体内蔵プリンタインタフェースが切り離される機種が    */
/*  ある (PC-9821Ne/Bf/Bp/Bs/Be/Xt/Xa/Xn/Xp/Xs/Xe、PC-9801BA2/BS2/BX2 等)。 */
/* ======================================================================== */
#define SYSPORT_DMA_CTRL       0x0439
#define SYSPORT_DMA_MASK_1MB   0x04   /* bit2: 1 = 1MB 超への DMA 禁止 */

/*  **読みが FFh でも書く。** 一度は「ポートが浮いている印」として FFh を
 *  避ける案を採ったが、実機は bit7 = 1 (内蔵プリンタ)・bit2 = 1 (起動時
 *  設定)・「未使用(?)」の bit6/bit3 が 1 で読めれば **正当に FFh を返し得る**。
 *  そこで書かないと DMA 禁止が残ったままで、直したい root panic が直らない。
 *  RMW なら FFh → FBh で bit7 は 1 のまま保たれるので、プリンタ I/F を
 *  切り離す危険も無い。0439h を持たない機種 (8086/V30) は OS32 の対象外
 *  (386 以上が前提)。                                                      */

/* ------------------------------------------------------------------------ */
/*  3 値の診断 (票 §1-2)。**転送の可否には使わない** —                      */
/*  BLOCKED の機種では OS32 の FDC 自体が動かないので、W2 (実機の FD 起動)  */
/*  がそれを検出する。VERIFIED は「ビットが落ちた」であって動作実証では     */
/*  ない。3 値は起動行と pcidump 相当の診断に出す。                         */
/* ------------------------------------------------------------------------ */
#define DMA_A20_UNKNOWN     0   /* dma8237_init より前 */
#define DMA_A20_VERIFIED    1   /* 書き戻しで bit2 が落ちた */
#define DMA_A20_UNREADABLE  2   /* 読みが FFh のまま (ポートが浮いている) */
#define DMA_A20_BLOCKED     3   /* 書いても bit2 が立ったまま */

/* ======================================================================== */
/*  エラーコード                                                            */
/*                                                                          */
/*  独自の DMA_ERR_* は作らず、カーネル共通の OS32_ERR_* (types.h 経由で    */
/*  sdk/include/os32/os32_kapi_shared.h) を使う。「引数不正」と「後でもう    */
/*  一度」はまさに OS32_ERR_INVAL / OS32_ERR_AGAIN で、体系を 2 つ持つ      */
/*  理由が無い (票 §1-2 の -EINVAL / -EAGAIN はこれ)。                      */
/* ======================================================================== */
#define DMA_ERR_ARG      OS32_ERR_INVAL   /* -9  ch/dir/mode/bytes/phys */
#define DMA_ERR_AGAIN    OS32_ERR_AGAIN   /* -14 3 組で揃わなかった */
#define DMA_ERR_STATE    OS32_ERR_NOSYS   /* -10 マスクされていない / 未初期化 */

/* ======================================================================== */
/*  純粋関数 (drivers/dma8237_math.c) — I/O を 1 つも出さない               */
/* ======================================================================== */

/* 物理番地を 16bit のオフセットと 8bit のバンクに割る。 */
void dma_split_addr(u32 phys, u16 *addr16, u8 *bank8);

/* [phys, phys+bytes) が 64KB バンクをまたぐか。1 = またぐ。
 * bytes == 0 は「またがない」ではなく **またぐ扱い (1)** — 0 バイトの転送は
 * そもそも積めないので、ここで拾えば呼び手の検査漏れが止まる。 */
int dma_crosses_64k(u32 phys, u32 bytes);

/* カウントレジスタの値 → バイト数 (count + 1)。 */
u32 dma_count_to_bytes(u32 count);

/* バイト数 → カウントレジスタの値 (bytes - 1)。bytes は検査済みが前提。 */
u32 dma_bytes_to_count(u32 bytes);

/* 2 回読んだカウントを採用してよいか。limit は積んだカウント値 (bytes-1)。
 *   1 = 採用 (後の値 c2 を使う) / 0 = 不採用 (読み直す)
 * 採用条件: c1 == c2、または c1 > c2 かつ c1 - c2 <= DMA_READ_SLACK。
 * どちらかが limit を超えていたら合成が壊れているので不採用。 */
int dma_accept_pair(u32 c1, u32 c2, u32 limit);

/* ポート表の引き。ch が範囲外なら負を返して *port を触らない。 */
int dma_port_addr(unsigned int ch, u16 *port);
int dma_port_count(unsigned int ch, u16 *port);
int dma_port_bank(unsigned int ch, u16 *port);

/* 0015h に書くシングルマスクのバイト。set != 0 でマスク。 */
u8 dma_mask_byte(unsigned int ch, int set);

/* 0017h に書くモードバイト。引数は検査済みが前提。 */
u8 dma_mode_byte(unsigned int ch, int dir, int mode);

/* dma_chan_setup の引数検査だけを切り出したもの (検査順は票 §1-6:
 * ch → dir/mode → bytes/phys → 終端の 64KB / 16MB 判定)。
 * 0 = 通った / DMA_ERR_ARG。 */
int dma_check_args(unsigned int ch, u32 phys, u32 bytes, int dir, int mode);

/* ======================================================================== */
/*  I/O を出す側 (drivers/dma8237.c)                                        */
/* ======================================================================== */

/* fdc_init より前、sysclk_detect の次に kernel.c から 1 回だけ呼ぶ。
 * 0439h bit2 (1MB 超への DMA 禁止) を RMW で落とし、3 値の診断を記録する。 */
void dma8237_init(void);

/* DMA_A20_* のいずれか。dma8237_init より前は DMA_A20_UNKNOWN。 */
int dma_above_1mb_state(void);

/* 起動行に出した 0439h の前後の値 (どちらも NULL 可)。 */
void dma_above_1mb_raw(u8 *pre, u8 *post);

/* チャネルを積む。**成功しても積んだだけでマスクしたまま返す** —
 * 呼び手は装置の準備を終えてから dma_chan_unmask する。
 * 呼ぶ前に dma_chan_mask を通っていること (マスクされていなければ負)。
 *   0 = 積んだ / DMA_ERR_ARG = 引数 / DMA_ERR_STATE = マスクされていない
 * 失敗した setup は自チャネルの done / tc_event を**消さない**。 */
int dma_chan_setup(unsigned int ch, u32 phys, u32 bytes, int dir, int mode);

void dma_chan_mask(unsigned int ch);
void dma_chan_unmask(unsigned int ch);

/* 残バイト数と TC の通知。手順と返却の規則は票 §1-2。
 *   0 = *bytes_left / *tc_seen を埋めた
 *   DMA_ERR_ARG   = ch が範囲外 / 出力が NULL
 *   DMA_ERR_AGAIN = 3 組とも揃わなかった (出力は触らない) */
int dma_chan_remaining(unsigned int ch, u32 *bytes_left, int *tc_seen);

/* 保持している TC の通知を消す (done は次の setup まで残る)。 */
void dma_chan_ack_tc(unsigned int ch);

/* 保持している状態をそのまま覗く (kselftest / 診断用。I/O は出さない)。
 * done / tc_event は NULL 可。ch が範囲外なら負。 */
int dma_chan_state(unsigned int ch, int *done, int *tc_event);

/* dma8237_init を通ったか。0 = まだ (dma_chan_setup は断る)。 */
int dma8237_ready(void);

#endif /* DMA8237_H */
