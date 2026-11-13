#ifdef TICK_PROBE
#include "kernel/time/tick_probe.h"
#endif
#include "tick.h"
#include "tick_order.h"
_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "PC98 tick slots exceed table");
/* ======================================================================== */
/*  NE2000.C — DP8390 (NE2000 互換) ドライバ本体                              */
/*                                                                          */
/*  8390 初期化、TX/RX、Remote DMA 制御、エラー復旧、統計。                    */
/*  カード固有部分 (ポート配置、reset) は ne2k_config で受け取る (LGY-98:     */
/*  drivers/net/ne2000/lgy98.c)。転送ループは drivers/net/ne2000/ne2000_io.asm、リング計算は        */
/*  drivers/net/ne2000/ne2000_ring.c。                                                   */
/*                                                                          */
/*  設計と進捗の正典: docs/tasks/network/PLAN.md (§4 初期化・送受信,         */
/*  §5 IRQ と処理時間)。出典: DP8390D データシート §7 §8 §9 §11。            */
/*                                                                          */
/*  排他の規則:                                                             */
/*   - CR の page と Remote DMA は共有資源。NIC を触る経路 (send / poll /     */
/*     irq) は ne2k_enter()/ne2k_leave() で 1 つだけにする。出入りは page0・ */
/*     DMA idle を保証する。                                                 */
/*   - busy 中に来た IRQ は pending を立てるだけで NIC レジスタを変えない。   */
/*   - 長い完了待ち (reset / RDC) は校正済み cpu_delay_us と有限回ポーリング。 */
/*     IF=1・IRQ の外でだけ待つ。IRQ の中は印を立てて bh に渡す。           */
/* ======================================================================== */

#include "ne2000.h"
#include "ne2000_regs.h"
#include "ne2000_ring.h"
#include "io.h"
#include "idt.h"
#include "irq.h"      /* IRQ_NONE / IRQ_HANDLED / IRQ_DEFERRED */
#include "kprintf.h"
#include "kstring.h"
#include "cpu_calibrate.h"
#include "cpu.h"
#include "kernel/idle_work.h"
#include "os32_kapi_shared.h"

/* ====================================================================== */
/*  ローカル定数                                                            */
/* ====================================================================== */

#define NE2K_RDMA_ATTEMPTS  2
#define NE2K_RAM_TEST_SEED_BASE 0x11
#define NE2K_DMA_GUARD_BYTE  0xA5
#define NE2K_PROBE_PSTOP     0xFF   /* probe 中の PSTOP (Remote DMA が途中で折り返さないよう上限へ) */
#define NE2K_TX_PAGES        6      /* 1536B = 1 スロット。TX 二重化はしない */
#define NE2K_TX_BUF_LEN      (NE2K_ETH_MAX_LEN + NE2K_PIO_PAD_BYTES)     /* 偶数丸めの余白 */

#define NE2K_TXQ_SLOTS       4

#define NE2K_RXQ_SLOTS       8
#define NE2K_RXQ_SLOT_BYTES  (NE2K_ETH_MAX_LEN + NE2K_PIO_PAD_BYTES)     /* 奇数末尾 word の余白 */

/* 待ち時間の上限 (M0 で決めた値。実機余裕は M5 で見直す) */
#define NE2K_RESET_WAIT_US   20000  /* DP8390 のリセット完了 ~1.6ms に対し 10 倍 */
#define NE2K_RESET_POLL_US   100
#define NE2K_RDC_WAIT_US     4000   /* 1536B の 16bit PIO 後に RDC が立つまで */
#define NE2K_RDC_POLL_US     4
#define NE2K_OVW_WAIT_TICKS  2      /* 同じ待ちを tick で数える場合 (IF=1) */
#define NE2K_TX_TIMEOUT_TICKS 5     /* 50ms。1514B @10Mbps は 1.2ms + 衝突退避 */
#define NE2K_TX_REINIT_MAX   3      /* 連続タイムアウトでの再初期化上限 */
#define NE2K_BAD_HDR_MAX     4      /* 連続する不整合ヘッダで再初期化 */
#define NE2K_OVW_DRAIN_BUDGET 64    /* OVW 復旧時のリング回収上限 (16KB なら十分) */
#define NE2K_BH_ROUNDS       8      /* 最大 32 受信件で協調処理を他の仕事に戻す */
#define NE2K_IRQ_BUDGET      4      /* IRQ 1 回 / タイマ補助 1 回で回収する受信フレーム数 (§5) */
#define NE2K_WATCHDOG_BUDGET 4      /* 100Hz ウォッチドッグ 1 回で回収する上限 (取りこぼし自己回復, M4) */
#define NE2K_WORK_WATCHDOG  0x80   /* software tag; RST is not in NE2K_IMR_MASK */
#define NE2K_IMR_MASK        (NE2K_ISR_PRX | NE2K_ISR_PTX | NE2K_ISR_RXE | NE2K_ISR_TXE | \
                              NE2K_ISR_OVW | NE2K_ISR_CNT)   /* RDC は同期待ちなので割り込みにしない */

/* ====================================================================== */
/*  デバイス状態                                                            */
/* ====================================================================== */

struct ne2k_dev {
    struct ne2k_config cfg;
    u8  mac[NE2K_ETH_ADDR_LEN];
    u8  prom_doubled;        /* PROM が 1 バイトおきに重複していた (16bit カード) */
    u8  ram_pages;           /* probe で確定した RAM ページ数 (0x40 / 0x80) */
    u8  tx_page;
    u8  rx_start, rx_stop;   /* PSTART / PSTOP (排他的終端) */
    u8  rx_next;             /* 次に読むページ (BNRY の 1 つ先) */
    int state;

    volatile u8 busy;
    volatile u8 irq_pending;
    u8  imr_mask;            /* 復帰時に入れたい IMR (ne2k_irq_enable 後は NE2K_IMR_MASK) */
    u8  imr_written;         /* **実 IMR に最後に書いた値** (ne2k_imr_sync だけが更新)。
                              * 共有 IRQ のアダプタが「装置をマスク中か」を判定するのは
                              * こちら — imr_mask は「戻したい値」であって実状態ではない
                              * (票 TASK_HAL_WIRING §1-1 の R1 / 1-6 の往復 7 B3)。 */
    u8  irq_on;              /* IRQ 駆動 (IDT 登録 + IMR 有効化済み) */
    u8  in_irq;              /* reserved: diagnostic layout compatibility; never context */
    u8  rx_backlog;          /* 予算切れか受信列満杯でリングに未回収が残る */

    /* TX */
    u8  tx_busy;
    u8  tx_reinit_run;       /* 連続タイムアウト数 */
    u32 tx_start_tick;

    /* OVW 復旧の状態機械 */
    u8  ovw_txp_was;         /* STP 時に TXP が立っていた */
    u8  ovw_tx_done;         /* その送信は既に PTX/TXE で終わっていた */
    u32 ovw_tick;

    u8  bad_hdr_run;
    u8  last_curr;           /* 直近に読んだ CURR (getter が NIC に触れず空きを見積もる, M4) */

    /* ホスト側受信キュー (単一生産者 / 単一消費者) */
    u8  rxq_head, rxq_tail, rxq_count;
    u16 rxq_len[NE2K_RXQ_SLOTS];
    u8  rxq_buf[NE2K_RXQ_SLOTS][NE2K_RXQ_SLOT_BYTES];

    u8  tx_buf[NE2K_TX_BUF_LEN];

    struct ne2k_stats st;

    /* ISR bits survive the short ACK path until the cooperative pass. */
    u8 need_rx, need_tx, need_reinit;
    u8 txq_head, txq_tail, txq_count;
    u16 txq_len[NE2K_TXQ_SLOTS];
    u8 txq_buf[NE2K_TXQ_SLOTS][NE2K_TX_BUF_LEN];
};

/* static にするとホストから読めない (kernel.map に載らない) ので意図的にグローバル。
 * emu_read_mem で state / mac / ram_pages / st を観測する (serial.c の ser_overflow_n と同じ流儀)。 */
struct ne2k_dev ne2k_nic;
#define nic ne2k_nic

/* ====================================================================== */
/*  レジスタアクセス                                                        */
/* ====================================================================== */

static u8 rd(u8 reg)
{
    return (u8)inp(nic.cfg.reg_base + reg);
}

static void wr(u8 reg, u8 val)
{
    outp(nic.cfg.reg_base + reg, val);
}

/* CR は常に完全な値を書く。TXP は ne2k_send だけが立てる。 */
static u8 cr_run_bits(void)
{
    return (nic.state == NE2K_STATE_RUNNING) ? NE2K_CR_STA : NE2K_CR_STP;
}

static void cr_page0_idle(void)
{
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | cr_run_bits());
}

static void cr_page1_idle(void)
{
    wr(NE2K_CR, NE2K_CR_PAGE1 | NE2K_CR_RD2 | cr_run_bits());
}

static int eflags_if(void)
{
    return _irq_enabled();
}

/* ====================================================================== */
/*  IMR と shadow の同期                                                    */
/*                                                                          */
/*  **IMR を書くのはここだけ** (票 1-6、往復 7 B3 / 往復 8 の中継 3)。       */
/*  通常 IRQ の出口・foreground の leave・tick・OVW・FAILED・再初期化成功の  */
/*  すべての最終出口がこれを通る。実 IMR と shadow がずれると、共有 IRQ の   */
/*  アダプタが「復旧待ちで装置マスク中」を IRQ_HANDLED と誤判定し、残件が    */
/*  誰にも回収されないまま線が上がったままになる。                          */
/*  先に shadow、次に実 IMR — 間で NIC が割り込みを上げることは無い          */
/*  (マスクを開ける向きでは実 IMR がまだ 0)。                               */
/* ====================================================================== */
static void ne2k_imr_sync(u8 value)
{
    nic.imr_written = value;
    wr(NE2K_P0_IMR, value);
}

/* ====================================================================== */
/*  排他                                                                    */
/* ====================================================================== */

/* **busy を立てるのと装置をマスクするのは 1 つの irq_save の中で**
 * (票 §1-1「busy とマスクの境界」、往復 5 B2)。分かれていたころは
 * 「busy だが装置は未マスク」の窓があり、そこで IRQ が来るとアダプタは
 * IRQ_DEFERRED を返すのに装置側は要因を保持したままだった。 */
static void ne2k_enter(void)
{
    unsigned int f = irq_save();
    nic.busy = 1;
    ne2k_imr_sync(0);
    irq_restore(f);
}

static int service(unsigned int budget);
static u8 read_curr(void);

static void ne2k_bh(void);

/* Only latch/ACK here: leave holds IF=0, including when called by bh. */
static void mark_work(void)
{
    u8 isr = rd(NE2K_P0_ISR) & NE2K_IMR_MASK;
    nic.need_rx |= isr;
    if (isr) wr(NE2K_P0_ISR, isr);
    if (read_curr() != nic.rx_next) nic.need_rx |= NE2K_ISR_PRX;
    if (nic.tx_busy || nic.txq_count) nic.need_tx = 1;
}

static void ne2k_leave(void)
{
    unsigned int f = irq_save();
    cr_page0_idle();
    if (nic.state == NE2K_STATE_RUNNING || nic.state == NE2K_STATE_OVW_WAIT) {
        if (nic.irq_pending) nic.st.irq_rechecks++;
        nic.irq_pending = 0;
        mark_work();
    }
    /* CNT may remain asserted until counters are read; OVW needs recovery.
     * Mask those pending causes while allowing PTX to wake the next bh. */
    ne2k_imr_sync((nic.irq_on && nic.state == NE2K_STATE_RUNNING &&
                   !nic.need_reinit)
                  ? (u8)(nic.imr_mask & ~(nic.need_rx & (NE2K_ISR_CNT | NE2K_ISR_OVW))) : 0);
    nic.busy = 0;
    irq_restore(f);
}

/* Defensive boundary for every synchronous hardware wait. */
static int wait_allowed(void)
{
    if (!eflags_if() || kctx_irq_depth != 0) {
        nic.need_reinit = 1;
        return 0;
    }
    return 1;
}

/* ====================================================================== */
/*  Remote DMA                                                             */
/* ====================================================================== */

/* RDC を期限付きで待ち、立っていれば消して 0。期限切れは DMA を止めて負値。 */
static int wait_rdc(void)
{
    unsigned int waited = 0;
    if (!wait_allowed()) return NE2K_ERR_TIMEOUT;
    for (;;) {
        if (rd(NE2K_P0_ISR) & NE2K_ISR_RDC) {
            wr(NE2K_P0_ISR, NE2K_ISR_RDC);
            return NE2K_OK;
        }
        if (waited >= NE2K_RDC_WAIT_US) break;
        cpu_delay_us(NE2K_RDC_POLL_US);
        waited += NE2K_RDC_POLL_US;
    }
    nic.st.rdc_timeout++;
    cr_page0_idle();                    /* RD2 = abort */
    return NE2K_ERR_TIMEOUT;
}

/* NIC RAM addr と buf の間で len バイトを転送。page0 前提。
 * len が奇数でも RBCR は偶数丸め、PIO は末尾を安全に扱う。
 * RDC が期限内に立たない一過性のハズレは、リング全体を捨てる reinit ではなく
 * ここで 1 回だけ Remote DMA を再発行して吸収する (M4/L1 の堅牢性)。 */
#ifdef TICK_PROBE
#define dma_transfer dma_transfer_impl
#endif
static int dma_transfer(u16 addr, void *buf, u16 len, int write)
{
    u16 n = (u16)((len + 1) & ~1);
    int try_i;
    if (len == 0) return NE2K_OK;
    for (try_i = 0; try_i < NE2K_RDMA_ATTEMPTS; try_i++) {
        if (try_i > 0) nic.st.rdc_retry++;
        wr(NE2K_P0_ISR, NE2K_ISR_RDC);      /* 古い RDC を消す */
        wr(NE2K_P0_RBCR0, (u8)(n & 0xFF));
        wr(NE2K_P0_RBCR1, (u8)(n >> 8));
        wr(NE2K_P0_RSAR0, (u8)(addr & 0xFF));
        wr(NE2K_P0_RSAR1, (u8)(addr >> 8));
        wr(NE2K_CR, NE2K_CR_PAGE0 | (write ? NE2K_CR_RD1 : NE2K_CR_RD0) | NE2K_CR_STA); /* STA で発行 */
#ifdef TICK_PROBE
        tick_probe_pio_begin();
#endif
        if (write) ne2k_pio_write(nic.cfg.data_port, buf, len);
        else ne2k_pio_read(nic.cfg.data_port, buf, len);
#ifdef TICK_PROBE
        tick_probe_pio_end();
#endif
        if (wait_rdc() == NE2K_OK) return NE2K_OK;
    }
    return NE2K_ERR_TIMEOUT;
}

#ifdef TICK_PROBE
#undef dma_transfer
static int dma_transfer(u16 addr, void *buf, u16 len, int write)
{
    struct tp_device_mark m = tick_probe_device_start();
    int rc = dma_transfer_impl(addr, buf, len, write);
    tick_probe_device_end(TP_DMA, m, len);
    return rc;
}
#endif

static int dma_read(u16 addr, void *buf, u16 len)
{
    return dma_transfer(addr, buf, len, 0);
}

static int dma_write(u16 addr, const void *buf, u16 len)
{
    return dma_transfer(addr, (void *)buf, len, 1);
}

/* ====================================================================== */
/*  リセット・存在確認・PROM                                                 */
/* ====================================================================== */

static int hw_reset(void)
{
    unsigned int waited = 0;
    if (!wait_allowed()) return NE2K_ERR_TIMEOUT;

    if (nic.cfg.reset_port) {
        (void)inp(nic.cfg.reset_port);  /* LGY-98: 読み出しで reset */
    }
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);

    /* ISR.RST を期限付きで待つ。カード不在は 0xFF が読めて通過するので、
     * 存在確認は presence_check() で別に行う。 */
    for (;;) {
        if (rd(NE2K_P0_ISR) & NE2K_ISR_RST) break;
        if (waited >= NE2K_RESET_WAIT_US) return NE2K_ERR_TIMEOUT;
        cpu_delay_us(NE2K_RESET_POLL_US);
        waited += NE2K_RESET_POLL_US;
    }
    wr(NE2K_P0_ISR, NE2K_ISR_ALL);
    return NE2K_OK;
}

/* PSTART / PSTOP を書き、page2 から読み返す。不在なら 0xFF が返る。
 * probe 中は PSTOP を上限に置き、Remote DMA が RAM 途中で折り返さないようにする
 * (8390 の Remote DMA は PSTOP で PSTART に戻る)。 */
static int presence_check(void)
{
    u8 a, b;
    u8 ps = nic.cfg.ram_page_first;
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    wr(NE2K_P0_PSTART, ps);
    wr(NE2K_P0_PSTOP,  NE2K_PROBE_PSTOP);
    wr(NE2K_P0_BNRY,   ps);
    wr(NE2K_CR, NE2K_CR_PAGE2 | NE2K_CR_RD2 | NE2K_CR_STP);
    a = rd(NE2K_P2_PSTART);
    b = rd(NE2K_P2_PSTOP);
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    if (a != ps || b != NE2K_PROBE_PSTOP) return NE2K_ERR_NODEV;
    return NE2K_OK;
}

/* PROM 32 バイトを Remote DMA で読み、MAC と署名を確認する。 */
static int read_prom(void)
{
    u8 prom[NE2K_PROM_LEN];
    int i, doubled = 1, all0 = 1, allf = 1;
    int rc;

    wr(NE2K_P0_DCR, NE2K_DCR_DEFAULT);      /* WTS を先に立てる */
    rc = dma_read(0, prom, NE2K_PROM_LEN);
    if (rc) return rc;

    for (i = 0; i < NE2K_PROM_RAW_LEN; i++) {
        if (prom[NE2K_PROM_STRIDE * i] != prom[NE2K_PROM_STRIDE * i + 1]) doubled = 0;
    }
    nic.prom_doubled = (u8)doubled;
    for (i = 0; i < NE2K_ETH_ADDR_LEN; i++) {
        nic.mac[i] = doubled ? prom[NE2K_PROM_STRIDE * i] : prom[i];
        if (nic.mac[i] != 0x00) all0 = 0;
        if (nic.mac[i] != 0xFF) allf = 0;
    }
    /* 署名 'W' (byte 14/15、重複なら 28..31) */
    if (doubled) {
        if (prom[NE2K_PROM_STRIDE * NE2K_PROM_SIG_OFF] != NE2K_PROM_SIG || prom[NE2K_PROM_STRIDE * (NE2K_PROM_SIG_OFF + 1)] != NE2K_PROM_SIG) return NE2K_ERR_NODEV;
    } else {
        if (prom[NE2K_PROM_SIG_OFF] != NE2K_PROM_SIG || prom[NE2K_PROM_SIG_OFF + 1] != NE2K_PROM_SIG) return NE2K_ERR_NODEV;
    }
    /* OUI だけで判定しない: 全 0 / 全 1 / multicast ビットを拒否 */
    if (all0 || allf || (nic.mac[0] & 0x01)) return NE2K_ERR_NODEV;
    return NE2K_OK;
}

/* ====================================================================== */
/*  NIC RAM の probe と診断                                                  */
/* ====================================================================== */

static void fill_pattern(u8 *buf, unsigned int len, u8 seed)
{
    unsigned int i;
    for (i = 0; i < len; i++) buf[i] = (u8)(seed ^ (u8)i ^ (u8)(i >> 3));
}

/* 1 ページを書いて読み返す。0 = 一致。 */
static int page_roundtrip(u8 page, u8 seed)
{
    u8 *w = nic.tx_buf;
    u8 *r = nic.rxq_buf[0];
    unsigned int i;
    fill_pattern(w, NE2K_PAGE_SIZE, seed);
    if (dma_write((u16)page << 8, w, NE2K_PAGE_SIZE)) return -1;
    kmemset(r, 0, NE2K_PAGE_SIZE);
    if (dma_read((u16)page << 8, r, NE2K_PAGE_SIZE)) return -1;
    for (i = 0; i < NE2K_PAGE_SIZE; i++) {
        if (w[i] != r[i]) return -1;
    }
    return 0;
}

/* RAM 容量: 先頭ページと 16KB 末尾が通り、32KB 側が通って先頭を潰さなければ 32KB。 */
static int probe_ram(void)
{
    u8 first = nic.cfg.ram_page_first;
    u8 last16 = (u8)(first + NE2K_RAM_PAGES_16K - 1);
    u8 first32 = (u8)(first + NE2K_RAM_PAGES_16K);
    u8 last32 = (u8)(first + 2 * NE2K_RAM_PAGES_16K - 1);
    u8 keep[NE2K_PAGE_SIZE];
    unsigned int i;

    if (page_roundtrip(first, 0x5A)) return NE2K_ERR_IO;
    if (page_roundtrip(last16, 0xA5)) return NE2K_ERR_IO;
    nic.ram_pages = NE2K_RAM_PAGES_16K;

    if (nic.cfg.ram_pages_max <= NE2K_RAM_PAGES_16K) return NE2K_OK;

    /* 先頭ページに既知パターンを置き、32KB 側を書いた後で無事か見る (エイリアス検査) */
    fill_pattern(keep, NE2K_PAGE_SIZE, 0x3C);
    if (dma_write((u16)first << 8, keep, NE2K_PAGE_SIZE)) return NE2K_ERR_IO;
    if (page_roundtrip(first32, 0xC3)) return NE2K_OK;      /* 16KB 確定 */
    if (page_roundtrip(last32, 0x69)) return NE2K_OK;
    if (dma_read((u16)first << 8, nic.rxq_buf[0], NE2K_PAGE_SIZE)) return NE2K_ERR_IO;
    for (i = 0; i < NE2K_PAGE_SIZE; i++) {
        if (nic.rxq_buf[0][i] != keep[i]) return NE2K_OK;   /* 折り返し → 16KB */
    }
    nic.ram_pages = 2 * NE2K_RAM_PAGES_16K;
    return NE2K_OK;
}

/* 全ページのパターン試験 (停止中・診断時だけ)。不一致バイト数を統計に足す。 */
static void diag_ram_test(void)
{
    u8 *w = nic.tx_buf;
    u8 *r = nic.rxq_buf[0];
    unsigned int p, i;
    u8 first = nic.cfg.ram_page_first;

    for (p = 0; p < nic.ram_pages; p++) {
        fill_pattern(w, NE2K_PAGE_SIZE, (u8)(NE2K_RAM_TEST_SEED_BASE + p));
        if (dma_write((u16)(first + p) << 8, w, NE2K_PAGE_SIZE)) {
            nic.st.diag_ram_errors += NE2K_PAGE_SIZE;
        }
    }
    for (p = 0; p < nic.ram_pages; p++) {
        fill_pattern(w, NE2K_PAGE_SIZE, (u8)(NE2K_RAM_TEST_SEED_BASE + p));
        kmemset(r, 0, NE2K_PAGE_SIZE);
        if (dma_read((u16)(first + p) << 8, r, NE2K_PAGE_SIZE)) {
            nic.st.diag_ram_errors += NE2K_PAGE_SIZE;
            continue;
        }
        for (i = 0; i < NE2K_PAGE_SIZE; i++) {
            if (w[i] != r[i]) nic.st.diag_ram_errors++;
        }
    }
}

/* Remote DMA 往復: 奇数長・偶数長・ページ境界。読み側の余白が壊れないことも見る。 */
static void diag_dma_test(void)
{
    static const u16 lens[] = { 1, 2, 3, 59, 60, 61, 255, 256, 257, 1513, 1514 };
    u8 *w = nic.tx_buf;
    u8 *r = nic.rxq_buf[0];
    u16 base = (u16)nic.cfg.ram_page_first << 8;
    unsigned int k, i;

    for (k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        u16 len = lens[k];
        int bad = 0;
        fill_pattern(w, len, (u8)(0x80 + k));
        w[len] = 0xEE;                          /* 書き側の末尾余白 (読み越し検出用) */
        if (dma_write(base, w, len)) { nic.st.diag_dma_errors++; continue; }
        kmemset(r, NE2K_DMA_GUARD_BYTE, NE2K_RXQ_SLOT_BYTES);
        if (dma_read(base, r, len)) { nic.st.diag_dma_errors++; continue; }
        for (i = 0; i < len; i++) {
            if (r[i] != w[i]) { bad = 1; break; }
        }
        if (r[len] != NE2K_DMA_GUARD_BYTE) bad = 1;            /* 奇数末尾で余白に書いた */
        if (bad) nic.st.diag_dma_errors++;
    }
}

#include "ne2000_control.inc"

#include "ne2000_data.inc"

#include "ne2000_irq.inc"

static void ne2k_bh(void)
{
    u32 before;
    unsigned int round;
    int watchdog;
    if (!wait_allowed()) return;
    if (nic.busy || nic.state == NE2K_STATE_OFF ||
        nic.state == NE2K_STATE_STOPPED || nic.state == NE2K_STATE_FAILED) return;
    if (!nic.need_rx && !nic.need_tx && !nic.need_reinit &&
        !nic.rx_backlog && nic.state != NE2K_STATE_OVW_WAIT) return;
    if (!nic.need_rx && !nic.need_tx && !nic.need_reinit &&
        nic.rx_backlog && nic.rxq_count >= NE2K_RXQ_SLOTS &&
        nic.state != NE2K_STATE_OVW_WAIT) return;
    ne2k_enter();
    before = nic.st.rx_frames + nic.st.rx_dropped;
    watchdog = (nic.need_rx & NE2K_WORK_WATCHDOG) != 0;
    if (nic.need_reinit) {
        nic.need_reinit = 0;
        reinit_or_fail();
    } else {
        for (round = 0; round < NE2K_BH_ROUNDS; round++) {
            u32 rx_before = nic.st.rx_frames + nic.st.rx_dropped;
            int sent = 0;
            int blocked = service(NE2K_IRQ_BUDGET);
            if (nic.state != NE2K_STATE_RUNNING || nic.need_reinit) break;
            if (!nic.tx_busy && nic.txq_count) {
                tx_dequeue();
                sent = 1;
            }
            if (nic.need_reinit || (nic.tx_busy && (rd(NE2K_CR) & NE2K_CR_TXP))) break;
            if (blocked) break;
            if (!sent && rx_before == nic.st.rx_frames + nic.st.rx_dropped) break;
        }
    }
    nic.need_tx = nic.tx_busy || nic.txq_count;
    if (watchdog) {
        u32 frames = nic.st.rx_frames + nic.st.rx_dropped - before;
        nic.st.watchdog_frames += frames;
        if (frames) nic.st.watchdog_hits++;
    }
    ne2k_leave();
}

/* ---- リンク層が Credit を作るための空き容量 (LINK_PLAN.md §2-2)。
 * ドライバは空きを返すだけで Credit は計算しない。NIC には触れず、
 * ウォッチドッグ / IRQ が更新した last_curr と rx_next から見積もる。 */
unsigned int ne2k_rx_ring_free_pages(void)
{
    unsigned int size, c, b;
    if (nic.state != NE2K_STATE_RUNNING) return 0;
    size = (unsigned int)(nic.rx_stop - nic.rx_start);
    if (size == 0) return 0;
    /* BNRY = rx_next - 1 (先頭なら rx_stop-1)。NIC は CURR から BNRY 手前まで受信できる。 */
    b = (unsigned int)((nic.rx_next == nic.rx_start ? nic.rx_stop - 1 : nic.rx_next - 1) - nic.rx_start);
    c = (unsigned int)(nic.last_curr - nic.rx_start);
    if (c >= size) c = 0;                        /* last_curr 未取得時の安全側 */
    return (b + size - c) % size;
}

unsigned int ne2k_rx_queue_free(void)
{
    unsigned int used = nic.rxq_count;
    return (used >= NE2K_RXQ_SLOTS) ? 0 : (NE2K_RXQ_SLOTS - used);
}

int ne2k_is_busy(void)
{
    return nic.busy ? 1 : 0;
}

void ne2k_get_stats(struct ne2k_stats *stats)
{
    unsigned int f;
    if (!stats) return;
    f = irq_save();
    *stats = nic.st;
    irq_restore(f);
}

int ne2k_state(void)
{
    return nic.state;
}

void ne2k_get_mac(u8 mac[ETH_ALEN])
{
    kmemcpy(mac, nic.mac, NE2K_ETH_ADDR_LEN);
}

unsigned int ne2k_ram_bytes(void)
{
    return (unsigned int)nic.ram_pages * NE2K_PAGE_SIZE;
}
