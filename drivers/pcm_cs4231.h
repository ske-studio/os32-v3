/* ======================================================================== */
/*  PCM_CS4231.H — CS4231 (MATE-X PCM) 再生ドライバの決め事                 */
/*                                                                          */
/*  16 ビット・ステレオ・44.1k / 22.05kHz の**再生だけ** (票 §0)。単位は     */
/*  frame (左右 1 組 = 4 バイト)。録音・ミキサ・PIO・V86 提供は含まない。    */
/*                                                                          */
/*  票  : docs/tasks/v3/TASK_PCM_CS4231.md (設計 v10 Approved)              */
/*  典拠: Crystal CS4231A データシート **DS139PP2**                          */
/*        (docs/hw/crystal/cs4231a.pdf — gitignore のミラー。頁は本文に)     */
/*        docs/hw/undocumented/io_sound.md「PC-9821X･N内蔵型」               */
/*  土台: TASK_HAL_WIRING §1-1 割り込み / §1-2 8237 / §1-3 プール / §1-5 時計 */
/*                                                                          */
/*  [C4]: 数はここに 1 か所だけ置く。drivers/pcm_cs4231*.c に裸の数は書かない。*/
/*  試験: tools/tests/test_pcm_cs4231.py / 記録 tools/tests/pcm_cs4231_tdd.md */
/* ======================================================================== */

#ifndef PCM_CS4231_H
#define PCM_CS4231_H

#include "types.h"
#include "os32_kapi_shared.h"   /* OS32_ERR_* (エラーは共通体系) */

/* ======================================================================== */
/*  1. I/O ポート (io_sound.md「PC-9821X･N内蔵型」の既定配置 0F40h〜)        */
/*     0C2Bh / 0C2Dh による再配置はしない (票 §4)。                         */
/* ======================================================================== */
#define PCM_PORT_ROUTE   0x0F40   /* 割り込み / DMA の経路 (W) */
#define PCM_PORT_WSSID   0x0F43   /* WSS ID (R。下位 6 ビット) */
#define PCM_PORT_R0      0x0F44   /* Index (INIT / MCE / TRD / IA4-0) */
#define PCM_PORT_R1      0x0F45   /* Data (間接レジスタ) */
#define PCM_PORT_R2      0x0F46   /* Status。**何を書いても INT が消える** */
#define PCM_PORT_R3      0x0F47   /* PIO — 使わない */

/*  0F40h: bit5-3 = INT (011 = INT41 = IRQ10)、bit2-0 = DMA (010 = #1)。
 *  IRQ10 は §1-1 の動的な線で、LGY-98 の既定 (3/5) と FDC (#2) を避ける。 */
#define PCM_ROUTE_INT41_DMA1  0x1A
#define PCM_IRQ               10
#define PCM_DMA_CHAN          1

/*  0F43h の WSS ID。下位 6 ビットが 000100b。 */
#define PCM_WSSID_MASK   0x3F
#define PCM_WSSID_VALUE  0x04

/*  R0 (0F44h) のビット (DS139PP2 p.19-21) */
#define PCM_R0_INIT      0x80   /* 1 = 初期化中。**全読みが 0x80、書きは無視** */
#define PCM_R0_MCE       0x40   /* Mode Change Enable */
#define PCM_R0_TRD       0x20   /* Transfer Request Disable — 使わない */
#define PCM_R0_IA_MASK   0x1F   /* IA4-0 (MODE2 のとき I0〜I31) */

/* ======================================================================== */
/*  2. 間接レジスタ (DS139PP2 §3.2)                                          */
/* ======================================================================== */
#define PCM_I_LDA        6    /* Left DAC (D7 LDM ミュート / D5-0 減衰) */
#define PCM_I_RDA        7    /* Right DAC */
#define PCM_I_FMT        8    /* Fs & Playback Data Format */
#define PCM_I_IFACE      9    /* Interface Configuration */
#define PCM_I_PINCTL     10   /* Pin Control */
#define PCM_I_ERRSTAT    11   /* Error Status (R/O) */
#define PCM_I_MODEID     12   /* MODE and ID */
#define PCM_I_BASE_HI    14   /* Playback Base 上位。**書くとロードされる** */
#define PCM_I_BASE_LO    15   /* Playback Base 下位 */
#define PCM_I_ALTFEAT1   16   /* Alternate Feature Enable I (MODE2) */
#define PCM_I_ALTSTAT    24   /* Alternate Feature Status (MODE2) */
#define PCM_I_VERSION    25   /* Version / ID (MODE2) */

/*  I6 / I7: D7 = LDM/RDM (ミュート)、D5-0 = 減衰 (1.5dB 刻み、0 = 0dB)。 */
#define PCM_DA_ATT_MASK  0x3F
#define PCM_DA_ATT_MAX   0x3F
#define PCM_DA_MUTE      0x80

/*  I8: D7-D5 = FMT1:FMT0:C/L (16 ビット LE 2 の補数 = 010)、D4 = S/M (ステレオ
 *  = 1) → 上位 nibble 0x5。D3-D1 = CFS、D0 = C2SL (1 = XTAL2 16.9344MHz)。
 *  44.1k = XTAL2 / 384 → CFS 5 → 0x5B、22.05k = XTAL2 / 768 → CFS 3 → 0x57
 *  (DS139PP2 p.33)。**XTAL2 の無い機械では出ない** (票 E6)。 */
#define PCM_FMT_44100    0x5B
#define PCM_FMT_22050    0x57

/*  I9: D4:D3 = CAL1:CAL0、D0 = PEN。CAL/SDC/PPIO は **MCE 中しか書けない**
 *  (DS139PP2 p.34)。0x08 = CAL 1 (Converter calibration、136 サンプル周期) —
 *  CS4231 (V=100) では 0x10 (DAC だけ) が予約なので両版に共通な値を採る。 */
#define PCM_IFACE_CAL1   0x08
#define PCM_IFACE_PEN    0x01

/*  I10: D1 = IEN (割り込みピン許可)。他は 0。 */
#define PCM_PINCTL_IEN   0x02

/*  I11 (R/O): D5 = ACI (校正中)、D4 = DRS (PDRQ/CDRQ 動作中)。
 *  **0x80 は「ACI=0」ではない** — INIT / 再同期中の読みは全部 0x80。 */
#define PCM_ERR_ACI      0x20
#define PCM_ERR_DRS      0x10

/*  I12: D6 = MODE2 (I16〜I31 が見える)、D3-0 = ID3-0 = 1010b。 */
#define PCM_MODE2        0x40
#define PCM_ID_MASK      0x0F
#define PCM_ID_VALUE     0x0A

/*  I14 / I15 = Playback Base。**NS - 1** (NS = 割り込み間の frame 数)。
 *  半バッファ 2048 frame なので 2047 = 0x07FF。**I15 → I14 の順**に書く。 */
#define PCM_BASE_LO_2048 0xFF
#define PCM_BASE_HI_2048 0x07

/*  I16: D0 = DACZ (アンダーランで 0 を出す)、D5 = SPE。**SPE=0** を MCE 中に
 *  書く (BIOS の旧設定で 1 だと DAC がシリアル入力を使う)。 */
#define PCM_ALT1_DACZ    0x01

/*  I24: D4 = PI (再生 DMA カウントの割り込み)。0 を書けば消える。 */
#define PCM_ALT_PI       0x10

/*  I25: V2-0 = D7-D5。100 = CS4231、101 = CS4231A (DS139PP2 p.36/38)。 */
#define PCM_VER_MASK     0xE0
#define PCM_VER_CS4231   0x80
#define PCM_VER_CS4231A  0xA0
#define PCM_VER_SHIFT    5

/* ======================================================================== */
/*  3. リングとステージングの寸法 (票 §2-1)                                  */
/* ======================================================================== */
#define PCM_FRAME_BYTES    4U       /* 16 ビット × 2ch */
#define PCM_RING_FRAMES    4096U
#define PCM_HALF_FRAMES    2048U
#define PCM_RING_BYTES     (PCM_RING_FRAMES * PCM_FRAME_BYTES)   /* 16KB */
/*  ステージングは **DMA しない**ので KHEAP (kmalloc) から取る。DMA プールは
 *  64KB しかなく 82557 の 16KB と分け合うため、リングだけをプールに置く。 */
#define PCM_STG_FRAMES     4096U
#define PCM_STG_BYTES      (PCM_STG_FRAMES * PCM_FRAME_BYTES)    /* 16KB */
#define PCM_HALF_BYTES     (PCM_HALF_FRAMES * PCM_FRAME_BYTES)
#define PCM_POOL_ALIGN     4096U

/*  補充の余裕。装置の現在位置から半分の末尾まで**これ以上**あるときだけ
 *  写す (512 frame = 11.6ms @44.1k。コピー 8KB + I/O の最悪より十分長い)。 */
#define PCM_REFILL_MARGIN  512U

/*  レート。この 2 つだけ (票 §4)。 */
#define PCM_RATE_44100     44100U
#define PCM_RATE_22050     22050U

/*  待ちの期限 (tick = 10ms)。全部 IF=1 の poll。 */
#define PCM_WAIT_TICKS     5U   /* INIT / 再同期 / ACI (foreground) */
#define PCM_STOP_TICKS     3U   /* DRS の落ちるのを tick で待つ */
/*  close の 2 段目 (期限切れ → STOP_REQ) の待ちに足す余裕。入口の 3 tick に
 *  「入口を書いた tick の端数」と「証拠を読む 1 tick」を足す。 */
#define PCM_STOP_WAIT_SLACK 2U

/*  時間の単位。tick は PIT 100Hz (1-0 の後はどちらのクロックでも 10ms)。
 *  PCM_US_PER_TICK は sys_time_now が取れないときの µs 時計の代用。 */
#define PCM_MS_PER_TICK    10U
#define PCM_US_PER_TICK    10000U
#define PCM_MS_PER_SEC     1000U
#define PCM_US_PER_SEC     1000000U

/*  起動時の表示に出す版の番号 (CS4231 = 100、CS4231A = 101)。 */
#define PCM_VER_NAME_4231  100
#define PCM_VER_NAME_4231A 101

/* ------------------------------------------------------------------------ */
/*  診断 (kernel.map から読む。E3 の切り分け) — 番号の意味                   */
/* ------------------------------------------------------------------------ */
/* pcm_diag_fault_site: どこで FAULTED になったか (最初の 1 回だけ残す)。 */
#define PCM_DIAG_FAULT_TAIL      1   /* 停止の後続列が失敗 */
#define PCM_DIAG_FAULT_TAIL_DL   2   /* STOP_REQ の DRS 待ちが期限切れ */
#define PCM_DIAG_FAULT_EVID_DL   3   /* STOP_REQ の証拠が期限内に読めない */
#define PCM_DIAG_FAULT_CLOSE2    5   /* close の 2 段目 (停止待ち) が期限切れ */
#define PCM_DIAG_FAULT_RECLAIM   6   /* reclaim / close が証拠なしで解放 */
/* pcm_diag_evidence のビット (最後の証拠読み)。 */
#define PCM_DIAG_EV_INIT         0x01
#define PCM_DIAG_EV_PEN          0x02
#define PCM_DIAG_EV_PI           0x04
/* pcm_diag_df_site のビット (drain_failed をどこで立てたか)。 */
#define PCM_DIAG_DF_RS_STOP_DL   0x01   /* RS_STOP の期限切れ */
#define PCM_DIAG_DF_RESTART      0x02   /* RS_RESTART の失敗 */
#define PCM_DIAG_DF_CLOSE_DL     0x04   /* close の期限切れ */
#define PCM_DIAG_DF_OBS          0x08   /* 観測 (番犬・喪失) が先に立てていた */

/* ------------------------------------------------------------------------ */
/*  ホスト試験の割り込み点 (tools/tests/pcm_cs4231_host.c)                   */
/*                                                                          */
/*  カーネルでは PCM_PREEMPT は空。ホスト試験はこれを定義して「ここで IF=1   */
/*  なら tick が割り込める」を差し込む。**IF=0 の区間の中に置いた点は、試験  */
/*  側が割り込ませない** — それ自体が「この判定と公開は 1 つの禁止区間」の   */
/*  証明になる (Codex 実装レビューの blocker 2/3、2026-09-23)。             */
/* ------------------------------------------------------------------------ */
#define PCM_PP_CLOSE_ENTRY    1   /* close: 状態を読んだ後、DRAINING / close_pending を書く前 */
#define PCM_PP_CLOSE_TIMEOUT  2   /* close: 期限切れで STOP_REQ を公開した後、停止の入口の前 */
#define PCM_PP_WAIT           3   /* close: 停止待ちのループの 1 周 */

/* ------------------------------------------------------------------------ */
/*  エラーの写し (票 §2-2 の BUSY / NOMEM)                                  */
/*                                                                          */
/*  `OS32_ERR_*` には BUSY も NOMEM も無いので、意味のいちばん近い既存の     */
/*  番号へ写す。**新しい番号は足さない** (KAPI_SPEC §3-2 の予約表を汚さない)。*/
/*    BUSY  = 他の owner が open 中 / IRQ10 に結べない → 資源が満杯          */
/*    NOMEM = DMA プールが取れない                    → 空き不足            */
/* ------------------------------------------------------------------------ */
#define PCM_ERR_BUSY   OS32_ERR_FULL    /* -13 */
#define PCM_ERR_NOMEM  OS32_ERR_NOSPC   /* -4  */

/*  音量: percent 1〜100 を 6 ビットの減衰へ線形に写す。 */
#define PCM_VOL_MAX        100U

/*  カウンタの飽和 (pcm_status の詰め方)。 */
#define PCM_CNT_U8_MAX     255U
#define PCM_CNT_U16_MAX    65535U
#define PCM_CNT_UND_SHIFT  24
#define PCM_CNT_REP_SHIFT  16

/* ======================================================================== */
/*  4. 状態機械 (票 §2-1 の表)                                               */
/* ======================================================================== */
#define PCM_ST_CLOSED      0
#define PCM_ST_OPENING     1
#define PCM_ST_OPEN        2
#define PCM_ST_RUNNING     3
#define PCM_ST_DRAINING    4
#define PCM_ST_RS_STOP     5
#define PCM_ST_RS_RESTART  6
#define PCM_ST_STOP_REQ    7
#define PCM_ST_STOP_DONE   8
#define PCM_ST_FAULTED     9

/*  drain の 3 段階 (+ 出た)。 */
#define PCM_DRAIN_WAIT     0   /* last_data_half に入るのを待つ */
#define PCM_DRAIN_IN       1   /* 読んでいる */
#define PCM_DRAIN_SIL      2   /* 無音の半分を読んでいる */
#define PCM_DRAIN_OUT      3   /* 出た */

/*  連続性の判定 (位置だけ。PI は使わない — 票 往復 8)。 */
#define PCM_CONT_SAME      0   /* 境界なし */
#define PCM_CONT_SWITCH    1   /* 切り替え 1 回 */
#define PCM_CONT_LOST      2   /* 同じ半分で戻った = 1 周 (2 境界) */

/* ======================================================================== */
/*  5. ポート書きの列 (純粋部が組み、drivers/pcm_cs4231.c が実行する)        */
/*                                                                          */
/*  順序そのものを試験で固定するために、列を**データ**にする。               */
/* ======================================================================== */
#define PCM_OP_END         0
/*  REG: a = Index に書く値 (**bit6 = MCE を立てたまま**にするかを含む)、
 *  b = Data。素の idx なら MCE は落ちる。ハードの R0 の並びそのもの。 */
#define PCM_OP_REG         1
/*  PORT: a = 0F40h からのオフセット (0 = 経路、4 = R0、6 = R2)、b = 値。
 *  R0 だけを書く「MCE を落とす」も、R2 に書いて INT を消すのもこれ。 */
#define PCM_OP_PORT        2
#define PCM_OP_WAIT_INIT   3   /* R0 != 0x80 になるまで */
#define PCM_OP_WAIT_ACI    4   /* I11 の ACI が落ちるまで */
#define PCM_OP_WAIT_DRS    5   /* R0 != 0x80 を確かめてから I11 の DRS = 0 */
#define PCM_OP_CHK_MODE2   6   /* I12 を読み戻して MODE2 を確認 */
#define PCM_OP_DMA_MASK    7
#define PCM_OP_DMA_UNMASK  8
#define PCM_OP_DMA_SETUP   9   /* dma_chan_setup(1, ring, 16KB, FROM_MEM, CYCLIC) */
#define PCM_OP_RING_CLEAR  10  /* リングを 0 で埋め、filled[] = 0 */
#define PCM_OP_RING_FILL   11  /* ステージングから半分 0 (と 1) へ配る */
#define PCM_OP_TIMEBASE    12  /* half/pos/gen と基準時刻・証拠の初期化 */
#define PCM_OP_DEADLINE    13  /* 期限 = 今 + b tick (**入口でだけ**) */
#define PCM_OP_ACK_TC      14  /* dma_chan_ack_tc (診断) */
/*  PI の ack は専用の op を作らない — `PCM_OP_REG` で I24 に 0 を書くのが
 *  まさにそれ (DS139PP2 p.39)。op を 1 つ減らすと実行側の分岐も 1 つ減る。 */

/*  PORT の a (0F40h からのオフセット)。 */
#define PCM_POFS_ROUTE     0
#define PCM_POFS_R0        4
#define PCM_POFS_R2        6

#define PCM_SEQ_MAX        20  /* いちばん長い列 (init) が収まる長さ */

struct pcm_op {
    u8 kind;
    u8 a;
    u8 b;
};

/* ======================================================================== */
/*  6. 純粋部が触る状態 (drivers/pcm_cs4231_math.c)                          */
/* ======================================================================== */

/*  ステージング (DMA しない 16KB)。foreground が書き、advance が読む。 */
struct pcm_stg {
    u32 w;        /* 公開済みの書き込み位置 (frame) */
    u32 r;        /* 読み出し位置 (frame) */
    u32 staged;   /* 公開済みで未転送の frame 数 */
};

/*  リング 1 周ぶんの観測と判定に要るものだけ。I/O は 1 つも持たない。 */
struct pcm_core {
    u8  state;            /* PCM_ST_* */
    u8  half;             /* h0 = 前回観測した半分 */
    u8  drain;            /* PCM_DRAIN_* */
    u8  last_data_half;   /* 最後に正の frame を置いた半分 */
    u8  drain_failed;     /* drain が番犬 / 喪失 / 期限で終わった */
    u8  close_pending;    /* RS_RESTART の後に DRAINING へ */
    u8  finalized;        /* 解放を 1 度だけに固定する */
    u8  pad;
    u32 pos;              /* p0 = 前回観測した frame 番号 (0..4095) */
    u32 gen;
    u32 filled[2];        /* 各半分に置いた実データの frame 数 */
    u32 rate;
    u32 last_progress;    /* 位置が進んだ最後の時刻 (µs) */
    u32 now;              /* 最後に取れた時刻 (µs) */
    u32 underruns;
    u32 repeats;
    u32 resyncs;
    struct pcm_stg stg;
};

/*  advance が driver に頼む仕事 (メモリのコピーは driver 側)。 */
struct pcm_act {
    u8  refill;      /* 1 = 半分 to_half を書き直す */
    u8  to_half;
    u8  entered;     /* 1 = 停止の入口の列を流す (RS_STOP / STOP_REQ に入った) */
    u8  pad;
    u32 src_off;     /* ステージングの frame オフセット */
    u32 frames;      /* 写す frame 数 (残りは 0 で埋める) */
};

/* ------------------------------------------------------------------------ */
/*  純粋関数 (drivers/pcm_cs4231_math.c) — I/O を 1 つも出さない            */
/* ------------------------------------------------------------------------ */

/* レート → I8 の値。0 = 入れた / OS32_ERR_INVAL (出力は触らない)。 */
int pcm_fmt_for_rate(u32 rate, u8 *fmt);

/* 半周期 (2048 frame) のミリ秒と、番犬の期間 (2 半周期) のマイクロ秒。 */
u32 pcm_half_ms(u32 rate);
u32 pcm_watchdog_us(u32 rate);

/* close の期限 (tick)。(ceil(staged/2048) + 3) × H + 3 tick を切り上げ。 */
u32 pcm_close_ticks(u32 staged_frames, u32 rate);

/* percent (0〜100) → I6/I7 の値。0 = 入れた / OS32_ERR_INVAL。 */
int pcm_vol_reg(u32 percent, u8 *reg);

/* 残バイト数 → リング全体の frame 番号 (0..4095)。 */
u32 pcm_pos_frames(u32 bytes_left);

/* 連続性の判定 (位置だけ)。PCM_CONT_*。 */
int pcm_cont(u32 h0, u32 p0, u32 h1, u32 p1);

/* 現在位置 p1 からその半分の末尾まで REFILL_MARGIN 以上あるか。1 = ある。 */
int pcm_refill_ok(u32 p1);

/* drain の段階を**1 段だけ**進める (切り替えを観測したときだけ呼ぶ)。 */
int pcm_drain_next(int stage, u32 h1, u32 last_data_half);

/* pcm_start の配り方。n0 / n1 と last_data_half と drain の初期段階。 */
void pcm_start_plan(u32 staged, u32 *n0, u32 *n1, u32 *last_half,
                    int *drain_stage);

/* ステージング。frame 単位。 */
u32  pcm_stg_free(const struct pcm_stg *s);
u32  pcm_stg_reserve(const struct pcm_stg *s, u32 frames, u32 *off);
void pcm_stg_publish(struct pcm_stg *s, u32 frames);
u32  pcm_stg_consume(struct pcm_stg *s, u32 frames, u32 *off);
/* 物理末尾 (PCM_STG_FRAMES) を跨ぐときの 2 分割。n1 + n2 == n。 */
void pcm_split(u32 off, u32 n, u32 *n1, u32 *n2);

/* 1 回の観測。have_pos = 0 は -EAGAIN (位置を更新しない)。 */
void pcm_obs(struct pcm_core *c, int have_pos, u32 p1, u32 now,
             struct pcm_act *act);

/* カウンタ 3 本を 1 つの u32 に詰める (8/8/16 ビット、飽和)。 */
u32 pcm_counters_pack(u32 underruns, u32 repeats, u32 resyncs);

/* 列を引く。戻り値 = op の数、*out = 表の先頭 (**書き換えない**)。
 * 表そのものを返すので、呼び手は写しのためのスタックを持たない。 */
int pcm_seq_prologue(const struct pcm_op **out);   /* open の (2)〜(4) */
int pcm_seq_start(const struct pcm_op **out);      /* pcm_start */
int pcm_seq_stop_entry(const struct pcm_op **out); /* RS_STOP / STOP_REQ の入口 */
int pcm_seq_stop_tail(const struct pcm_op **out);  /* 後続の tick の確認 */
int pcm_seq_restart(const struct pcm_op **out);    /* RS_RESTART */
int pcm_seq_abort(const struct pcm_op **out);      /* reclaim の待たない abort */
/* これだけは I8 の値が引数なので**写して差し替える** (out は PCM_SEQ_MAX 個)。 */
int pcm_seq_init(u8 fmt, struct pcm_op *out);      /* open の (8)〜(10) */

/* ------------------------------------------------------------------------ */
/*  I/O を出す側 (drivers/pcm_cs4231.c)                                     */
/* ------------------------------------------------------------------------ */

/* 起動時 (pci_bind_all の後)。検出だけ。装置が無くても静かに戻る。 */
void pcm_init(void);

/* tick フック (kernel/isr_handlers.c の timer_handler)。 */
void pcm_tick(void);

/* 所有者の回収 (exec/exec.c の exec_reclaim_owned)。 */
void pcm_reclaim(int owner);

/* KAPI の実体 (kapi/ の __cdecl ラッパから呼ばれる — [C3])。 */
int pcm_open(u32 rate);
/* write の受付の検査だけ (owner と状態)。0 / OS32_ERR_INVAL。
 * KAPI の wrapper が**入力の検査より先に**呼ぶ — 未 open・非 owner は
 * bytes = 0 でも負を返す契約 (票 §2-2)。 */
int pcm_write_check(void);
int pcm_write(const void *buf, u32 bytes);
int pcm_status(u32 *free_bytes, u32 *counters);
int pcm_close(void);
int pcm_set_volume(u32 percent);

/* kselftest / 診断の覗き口 (KAPI にはしない)。 */
int pcm_state(void);
int pcm_present(void);

#endif /* PCM_CS4231_H */
