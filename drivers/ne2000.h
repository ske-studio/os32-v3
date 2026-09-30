/* ======================================================================== */
/*  NE2000.H — DP8390 (NE2000 互換) ドライバのカーネル内部 API                */
/*                                                                          */
/*  対象は NIC 1 枚、カーネル所有バッファのみ。KAPI は初版では追加しない。     */
/*  設計と進捗の正典: docs/tasks/network/PLAN.md (§3 内部 API, §4 実装)。    */
/*  カード固有のポート (LGY-98) は drivers/lgy98.h が ne2k_config に詰める。  */
/* ======================================================================== */

#ifndef NE2000_H
#define NE2000_H

#include "types.h"

/* ---- 結果コード (負値)。KAPI 化するときに OS32_ERR_* へ写像する ---- */
#define NE2K_OK             0
#define NE2K_ERR_INVAL     -1   /* 引数・設定が不正 */
#define NE2K_ERR_NODEV     -2   /* カード応答なし / PROM 署名不一致 */
#define NE2K_ERR_IO        -3   /* NIC RAM 試験や Remote DMA の不一致 */
#define NE2K_ERR_BUSY      -4   /* 送信中 */
#define NE2K_ERR_AGAIN     -5   /* 受信フレームなし */
#define NE2K_ERR_NOSPACE   -6   /* 容量不足 (*length に必要長、フレームは残す) */
#define NE2K_ERR_STOPPED   -7   /* 停止状態 (未初期化 / stop 後 / 復旧失敗) */
#define NE2K_ERR_IRQ       -8   /* IRQ が未対応か使用中 */
#define NE2K_ERR_TIMEOUT   -9   /* reset / RDC / TX の期限切れ */

/* ---- 状態 (ne2k_state) ---- */
#define NE2K_STATE_OFF      0   /* 未初期化 */
#define NE2K_STATE_RUNNING  1
#define NE2K_STATE_OVW_WAIT 2   /* overwrite 復旧の待機中 (poll が進める) */
#define NE2K_STATE_STOPPED  3   /* ne2k_stop 後 */
#define NE2K_STATE_FAILED   4   /* 有限回の再初期化に失敗、NIC だけ停止 */

/* ---- 設定 ---- */
struct ne2k_config {
    u16 reg_base;        /* DP8390 レジスタ (8bit, +0x00〜0x0F) */
    u16 data_port;       /* Remote DMA データポート (16bit) */
    u16 reset_port;      /* 読み出しでリセットするポート。0 = CR の STP だけ */
    u8  irq;             /* PIC IRQ。M3 まで PIC 側は有効化しない */
    u8  ram_page_first;  /* NIC RAM の先頭ページ (LGY-98: 0x40) */
    u8  ram_pages_max;   /* probe の上限ページ数 (0x80 = 32KB まで試す) */
    u8  flags;           /* NE2K_CFG_* */
};

#define NE2K_CFG_RX_COUNT_NO_FCS 0x01  /* 受信ヘッダの count に FCS 4B が含まれない (NP21/W) */
#define NE2K_CFG_DIAG_RAM        0x02  /* 初期化時に RAM 全域パターン試験 (停止中のみ) */
#define NE2K_CFG_DIAG_DMA        0x04  /* 初期化時に Remote DMA 往復試験 (奇数長・境界) */
#define NE2K_CFG_LOOPBACK        0x08  /* TCR を内部ループバックにして起動 */
#define NE2K_CFG_PROMISC         0x10  /* RCR PRO (既定は自 MAC + broadcast) */

/* ---- 統計 (ne2k_get_stats はスナップショット) ---- */
struct ne2k_stats {
    u32 tx_accepted;     /* send が受け付けた数 */
    u32 tx_bytes;
    u32 tx_ok;           /* PTX */
    u32 tx_err;          /* TXE または TSR に PTX 無し */
    u32 tx_collisions;   /* TSR.COL */
    u32 tx_aborted;      /* TSR.ABT (16 回衝突) */
    u32 tx_underrun;     /* TSR.FU */
    u32 tx_timeouts;
    u32 rx_frames;       /* ホストキューに入れた数 */
    u32 rx_bytes;
    u32 rx_err;          /* RXE / 受信ヘッダの status に PRX 無し */
    u32 rx_dropped;      /* ホストキュー満杯で捨てた数 */
    u32 rx_missed;       /* CNTR2 */
    u32 rx_crc;          /* CNTR1 */
    u32 rx_align;        /* CNTR0 */
    u32 overwrite;       /* OVW 発生 */
    u32 rdc_timeout;
    u32 bad_header;      /* 受信ヘッダ不整合 → 再同期 */
    u32 resync;          /* CURR に合わせた再同期の回数 */
    u32 reinit;          /* 再初期化の回数 */
    u32 irq_count;
    u32 irq_deferred;    /* busy 中に来た IRQ (M3) */
    u32 diag_ram_errors; /* NE2K_CFG_DIAG_RAM の不一致バイト数 */
    u32 diag_dma_errors; /* NE2K_CFG_DIAG_DMA の失敗ケース数 */
    u32 irq_rechecks;    /* ACK 後の再確認で追加処理した回数 (M3) */
    u32 rx_backlog_events; /* 予算超過で IMR をマスクしたまま返した回数 (M3) */
    u32 watchdog_frames; /* 100Hz ウォッチドッグが回収した (= IRQ が取りこぼした) フレーム数 (M4) */
    u32 watchdog_hits;   /* ウォッチドッグが 1 フレーム以上回収した tick 数 (M4) */
    u32 rx_pages_total;  /* 受信フレームがリングで消費した総ページ数 (§2-3 の実測用) */
    u32 rdc_retry;       /* Remote DMA の RDC 再発行回数 (一過性ハズレの吸収, M4/L1) */
};

/* ---- API (docs/tasks/network/PLAN.md §3) ---- */

/* 初期化。設定検証 → reset → PROM → RAM probe → リング設定 → START。
 * 失敗時は NIC を停止状態にして負値を返す。呼び出し側は OS 起動を続ける。 */
int  ne2k_init(const struct ne2k_config *config);

/* NIC 割り込みを止め、未完了送信と受信キューを破棄して停止する。 */
void ne2k_stop(void);

/* 送信。frame は Ethernet ヘッダ込み・FCS 無し、14〜1514 バイト。
 * 60 バイト未満はゼロで padding する。戻れば frame は再利用できる。
 * 成功 = NIC RAM へのコピーと送信受付。回線上の完了は統計で見る。 */
int  ne2k_send(const void *frame, unsigned int length);

/* 受信。ホストキューから 1 フレームをコピーする。
 * AGAIN = 無し。NOSPACE = capacity 不足 (*length = 必要長、フレームは残る)。 */
int  ne2k_recv(void *frame, unsigned int capacity, unsigned int *length);

/* NIC の状態を進める: 送信完了の記録、受信リングの回収 (budget フレームまで)、
 * カウンタ、OVW 復旧、送信タイムアウト。M2 までは診断ループが頻繁に呼ぶ。 */
void ne2k_poll(unsigned int budget);

/* 共有 IRQ の口 (kernel/irq.c の表から呼ばれる。票 TASK_HAL_WIRING §1-1)。
 * busy 中は NIC レジスタに触らず pending を立てて IRQ_DEFERRED。要因が無ければ
 * IRQ_NONE。それ以外は NE2K_IRQ_BUDGET フレームまで回収し、ACK 後に ISR / CURR を
 * 再確認する。予算超過は IMR をマスクしたまま IRQ_DEFERRED (回収は ne2k_timer_tick)。
 * 専用スタブ irq_stub_nic_* とそれを呼んでいた ne2k_irq() は廃止した。 */
int ne2k_irq_shared(unsigned int irq, void *arg);

/* IRQ 駆動に切り替える。呼び出し側が先に irq_register() でハンドラを結び
 * (PIC のマスクは登録数が持つ)、この後で IMR を開ける。 */
void ne2k_irq_enable(void);

/* 100Hz タイマ補助 (kernel/isr_handlers.c)。予算超過の残り・OVW 復旧・送信
 * タイムアウトだけを進める。busy なら延期して再入しない。プロトコル処理はしない。 */
void ne2k_timer_tick(void);

/* リンク層が Credit を作るための空き容量 (LINK_PLAN.md §2-2)。ドライバは空きを
 * 返すだけで Credit は計算しない。NIC には触れず last_curr / rx_next から見積もる。 */
unsigned int ne2k_rx_ring_free_pages(void);
unsigned int ne2k_rx_queue_free(void);

/* foreground 操作中 (タイマ文脈から send/recv を呼ぶ前に確認する) */
int  ne2k_is_busy(void);

void ne2k_get_stats(struct ne2k_stats *stats);
int  ne2k_state(void);
void ne2k_get_mac(u8 mac[6]);
unsigned int ne2k_ram_bytes(void);   /* probe で確定した NIC RAM 容量 */

/* ---- ne2000_io.asm (System V i386, cdecl) ----
 * nbytes 分を 16bit PIO で転送する。奇数末尾: read は最後の word を AX に
 * 読んで下位 1 バイトだけ格納、write は最後の 1 バイトを上位ゼロの word で
 * 送る。呼び出し元バッファを 1 バイトも読み越さない。 */
void ne2k_pio_read(unsigned int port, void *buf, unsigned int nbytes);
void ne2k_pio_write(unsigned int port, const void *buf, unsigned int nbytes);

#endif /* NE2000_H */
