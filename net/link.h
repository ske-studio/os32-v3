/* ======================================================================== */
/*  LINK.H — OS32 リンクプロトコル v2 (LGY-98 の上の独自 raw Ethernet)        */
/*                                                                          */
/*  契約の正典: docs/archive/network/TASK_N0.md §1b (ワイヤ v2) / §2 (駆動・  */
/*  排他・状態機械)。要約は docs/tasks/network/LINK_PLAN.md §4。             */
/*                                                                          */
/*  v1 (16B ヘッダ・同期 Stop-and-Wait) との違い:                            */
/*    - ヘッダ 20B、**LE アクセサで明示的に直列化** (構造体キャストをしない)  */
/*    - セッション (`sess`、Agent が採番) と再同期世代 (`epoch`) と           */
/*      要求 ID (`rid`) を全フレームに載せる                                  */
/*    - 3 way HELLO (SYN / SYN-ACK / CONFIRM / ESTABLISHED)                  */
/*    - プロトコルを進めるのは 100Hz の `link_tick()` **だけ**。KAPI は状態を  */
/*      読み書きするだけで待たない (非ブロッキング)                           */
/*                                                                          */
/*  ここは [C4] の「ワイヤ定数の正典」。KAPI 側と共有する値 (エラー番号) は    */
/*  sdk/include/os32/os32_kapi_shared.h が正典で、ここには置かない。          */
/* ======================================================================== */

#ifndef OS32_NET_LINK_H
#define OS32_NET_LINK_H

#include "types.h"

/* 独自 EtherType (IEEE 802.1 local experimental 1)。Host Agent と共通。 */
#define LINK_ETHERTYPE      0x88B5
#define LINK_ETH_ADDR_LEN   6
#define LINK_ETH_HDR_LEN    14      /* dst(6) + src(6) + type(2) */

/* ---- opcode (ヘッダ @0) ------------------------------------------------ */
#define LINK_OP_HELLO       1       /* 3 way handshake / 再同期 */
#define LINK_OP_REQUEST     2       /* OS32 → Host: 要求行 (rid, seq = 0) */
#define LINK_OP_RESPONSE    3       /* Host → OS32: status u16 + length u32 */
#define LINK_OP_DATA        4       /* Host → OS32: 本文ストリーム (rid, seq 1〜) */
#define LINK_OP_EOF         5       /* Host → OS32: ストリーム終端 (補助) */
#define LINK_OP_ACK         6       /* 双方: rid + 累積 ack */
#define LINK_OP_WINDOW      7       /* OS32 → Host: 絶対値 credit + 配送許可 */
#define LINK_OP_WDATA       8       /* OS32 → Host: 要求本文 (rid, seq 1〜) */
#define LINK_OP_STATUS      9       /* OS32 → Host: 結果の再提示 / 生存確認 */
#define LINK_OP_RELEASE     10      /* OS32 → Host: ハンドルを閉じた通知 */

/* ---- ヘッダ (20B)。**構造体にしない** ---------------------------------- */
/*  op u8 @0, flags u8 @1, epoch u16 @2, seq u32 @4, ack u32 @8,             */
/*  length u16 @12, rid u32 @14, sess u16 @18                               */
/*  rid が 4 バイト境界に載らないのは意図どおりで、だからこそ読み書きは       */
/*  バイト単位の LE アクセサで行う (構造体の詰め物に依存しない = 移植性)。    */
#define LINK_HDR_LEN        20
#define LINK_OFF_OP         0
#define LINK_OFF_FLAGS      1
#define LINK_OFF_EPOCH      2
#define LINK_OFF_SEQ        4
#define LINK_OFF_ACK        8
#define LINK_OFF_LENGTH     12
#define LINK_OFF_RID        14
#define LINK_OFF_SESS       18

/* 1 フレームに載せる payload の上限 (契約値。物理上限 1480 より小さく取る)。 */
#define LINK_MAX_PAYLOAD    1400
#define LINK_MIN_FRAME      60      /* 60B 未満はドライバがゼロ padding する */

/* ---- HELLO の段階 (flags) ---------------------------------------------- */
#define LINK_HS_SYN         0       /* OS32→Host  payload 0 */
#define LINK_HS_SYNACK      1       /* Host→OS32  payload 6 (agent/req_sess/req_epoch) */
#define LINK_HS_CONFIRM     2       /* OS32→Host  payload 0 */
#define LINK_HS_ESTAB       3       /* Host→OS32  payload 2 (agent) */
#define LINK_HELLO_SA_LEN   6
#define LINK_HELLO_ES_LEN   2

/* ---- flags bit0 -------------------------------------------------------- */
#define LINK_F_CTRL         0x01    /* RESPONSE: 制御結果 (業務結果ではない) */
#define LINK_F_RELACK       0x01    /* ACK: RELEASE への ACK */

/* ---- 制御 RESPONSE の status (リンク符号。HTTP とは別空間) -------------- */
#define LINK_CTL_PROCESSING 1
#define LINK_CTL_TOMBSTONE  2
#define LINK_CTL_NO_SLOT    3

/* RESPONSE の payload は status u16 + length u32 の 6B 固定 */
#define LINK_RESP_LEN       6
#define LINK_WINDOW_LEN     2

/* ---- タイマ定数 (100Hz tick 基準) -------------------------------------- */
#define LINK_RTO_TICKS      20      /* 再送間隔 (200ms)。NIC 受理 tick から */
#define LINK_TRIES          5       /* 再送上限。超えたら再同期 */
#define LINK_PROBE_TICKS    100     /* T_probe = 1 秒 */
#define LINK_PROBE_MAX      5       /* STATUS が k 回無応答 → 再同期 */
#define LINK_RX_BUDGET      16      /* 1 tick で dispatch する上限フレーム数 */

/* ---- 容量 -------------------------------------------------------------- */
#define LINK_HANDLES        2       /* 同時ハンドル */
#define LINK_STREAM_BUF     8192    /* 本文リング 1 本 */
#define LINK_DECL_MAX       65536   /* 宣言長の上限 (wseq が枯渇しない) */

/* L1 の credit 計算パラメータ (LINK_PLAN.md §2-2) */
#define LINK_MAXFRAME_PAGES 6
#define LINK_CREDIT_MARGIN  12
#define LINK_PAGE_BYTES     256     /* NIC リングの 1 ページ */

/* ---- ハンドルの状態 (TASK_N0 §2c) -------------------------------------- */
#define LINK_H_FREE         0
#define LINK_H_SENT         1
#define LINK_H_RESP         2
#define LINK_H_DONE         3
#define LINK_H_STALE        4

/* ---- セッションの状態 -------------------------------------------------- */
#define LINK_S_DOWN         0       /* 未確立 (SYN を出す) */
#define LINK_S_SYN          1       /* SYN 送信済み、SYN-ACK 待ち */
#define LINK_S_CONFIRM      2       /* CONFIRM 送信済み、ESTABLISHED 待ち */
#define LINK_S_UP           3

/* ---- 旧 L0 の結果コード (自己試験が使う) -------------------------------- */
#define LINK_OK             0
#define LINK_ERR            -1
#define LINK_ERR_TIMEOUT    -2
#define LINK_ERR_NOPEER     -3
#define LINK_ERR_TOOBIG     -4

/* ======================================================================== */
/*  API                                                                     */
/* ======================================================================== */

/* リンク層を初期化する (ドライバ attach 後に呼ぶ)。my_mac は NIC の MAC。
 * 反射モードでは呼ばない (link_tick も起動しない)。 */
void link_init(const u8 my_mac[6]);

/* 100Hz タイマ (kernel/isr_handlers.c の `ne2k_timer_tick()` の直後)。
 * **プロトコルを進めるのはここだけ** — 受信 dispatch、再送、制御の送出、
 * HELLO / 再同期。KAPI からは呼ばない。反射モードでは呼ばれない。 */
void link_tick(void);

/* ---- KAPI v51 の実体 (ラッパーは kapi/kapi_host.c) ----------------------
 * どれも待たない。ポインタはすべて**カーネル領域**で、CPL=3 のポインタ検証と
 * ユーザー領域への写しはラッパー側が行う。 */

/* 要求行 (カーネル領域、1〜1400B) を写して REQUEST をキューへ。h (0/1) / 負。 */
i32 link_host_open(const char *req, u32 len, int owner);

/* 業務 RESPONSE の status / length。出力先はカーネルの変数 (NULL 可)。 */
i32 link_host_status(i32 h, u32 *status, u32 *length, int owner);

/* 本文を 1 回ぶん**カーネルの中継バッファへ**取り出す (成功確定点)。
 * 戻り値 > 0 なら *out に中継バッファの先頭が入る。0 = 完了、AGAIN、負 = 失敗。
 * 呼び出し側 (ラッパー) は戻った後 IF=1 でユーザー領域へ写す。 */
i32 link_host_read_stage(i32 h, u32 cap, const u8 **out, int owner);

/* 要求本文 (カーネル領域) を WDATA としてキューへ。受け付けた長さ / 負。 */
i32 link_host_write(i32 h, const void *buf, u32 len, int owner);

/* ハンドルを閉じる (RELEASE を専用スロットへ)。0 / 負。 */
i32 link_host_close(i32 h, int owner);

/* owner 回収 (exec_reclaim_owned から、kapi/kapi_host.c 経由)。 */
void link_host_owner_exit(int owner);

/* 要求行から宣言長を読む (ECHO / CLIP PUT / PRINT DATA / PUT)。0 = 本文無し。 */
u32 link_decl_len(const char *req, u32 len);

/* ---- 自己試験 (LGY98_FLAG_LINKTEST。非同期 API + IF=1 の hlt 待ちで書く) -- */
void link_selftest(int rounds);
void link_l1_bulk(unsigned int count, unsigned int payload);
void link_l2_stream(unsigned int total, unsigned int plen, unsigned int dropseq);
void link_l3_service(void);

/* ======================================================================== */
/*  ホストから kernel.map 経由で観測するカウンタ (static にしない)           */
/*  既存の tools/net_l*_test.py が読む名前は v2 でも同じ意味を保つ。         */
/* ======================================================================== */
extern u32 link_hello_ok;       /* セッション確立 (0/1) */
extern u32 link_rt_ok;          /* 現在の自己試験区間の成功往復数 (区間ごとに reset) */
extern u32 link_rt_fail;        /* 現在の区間の失敗往復数 */
extern u32 link_l0_ok;          /* L0 selftest 専用の成功往復数 (最終読み出し用) */
extern u32 link_l0_fail;        /* L0 selftest 専用の失敗往復数 */
extern u32 link_retransmits;    /* 再送回数 (REQUEST / WDATA / RELEASE / CONFIRM) */
extern u32 link_rx_frames;      /* 受け取ったリンクフレーム数 */
extern u32 link_rx_dropped;     /* 検査に落ちて捨てた数 */
extern u8  link_peer_mac[6];    /* 確立した Host Agent の MAC */
extern u16 link_epoch;          /* 現在の再同期世代 */

/* v2 で増えた観測点 */
extern u16 link_sess;           /* 現在のセッション ID (Agent 採番) */
extern u16 link_agent_gen;      /* Agent の起動世代 */
extern u32 link_resyncs;        /* 再同期した回数 */
extern u32 link_tombstones;     /* 制御 TOMBSTONE を受けた回数 */
extern u32 link_no_slots;       /* 制御 NO_SLOT を受けた回数 */
extern u32 link_processing;     /* 制御 PROCESSING を受けた回数 */
extern u32 link_tx_deferred;    /* NIC が受けず次の周回へ持ち越した回数 */

/* L1 (WINDOW/Credit の bulk 受信) 観測用 */
extern u32 link_l1_recv;
extern u32 link_l1_bytes;
extern u32 link_l1_ooo;
extern u32 link_l1_windows;
extern u32 link_l1_max_credit;
extern u32 link_l1_min_credit;
extern u32 link_l1_done;
extern u32 link_l1_meas_pages;

/* L2 (ストリーミング) 観測用 */
extern u32 link_l2_bytes;
extern u32 link_l2_read;
extern u32 link_l2_gaps;
extern u32 link_l2_bad;
extern u32 link_l2_eof;
extern u32 link_l2_overflow;

/* L3 (Host Services) 観測用 */
extern u32 link_l3_get_status;
extern u32 link_l3_get_len;
extern u32 link_l3_get_read;
extern u32 link_l3_get_bad;
extern u32 link_l3_404;
extern u32 link_l3_http_status;
extern u32 link_l3_http_read;
extern u32 link_l3_time_len;

#endif /* OS32_NET_LINK_H */
