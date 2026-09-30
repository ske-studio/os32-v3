/* ======================================================================== */
/*  NE2000.C — DP8390 (NE2000 互換) ドライバ本体                              */
/*                                                                          */
/*  8390 初期化、TX/RX、Remote DMA 制御、エラー復旧、統計。                    */
/*  カード固有部分 (ポート配置、reset) は ne2k_config で受け取る (LGY-98:     */
/*  drivers/lgy98.c)。転送ループは drivers/ne2000_io.asm、リング計算は        */
/*  drivers/ne2000_ring.c。                                                   */
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
/*     PIT tick だけに頼る上限は置かない (割り込み禁止中に進まない)。          */
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

/* ====================================================================== */
/*  ローカル定数                                                            */
/* ====================================================================== */

#define NE2K_RAM_PAGES_16K   0x40   /* 16KB / 256 */
#define NE2K_PROBE_PSTOP     0xFF   /* probe 中の PSTOP (Remote DMA が途中で折り返さないよう上限へ) */
#define NE2K_TX_PAGES        6      /* 1536B = 1 スロット。TX 二重化はしない */
#define NE2K_TX_BUF_LEN      (NE2K_ETH_MAX_LEN + 2)     /* 偶数丸めの余白 */

#define NE2K_RXQ_SLOTS       8
#define NE2K_RXQ_SLOT_BYTES  (NE2K_ETH_MAX_LEN + 2)     /* 奇数末尾 word の余白 */

/* 待ち時間の上限 (M0 で決めた値。実機余裕は M5 で見直す) */
#define NE2K_RESET_WAIT_US   20000  /* DP8390 のリセット完了 ~1.6ms に対し 10 倍 */
#define NE2K_RESET_POLL_US   100
#define NE2K_RDC_WAIT_US     4000   /* 1536B の 16bit PIO 後に RDC が立つまで */
#define NE2K_RDC_POLL_US     4
#define NE2K_OVW_WAIT_US     2000   /* DP8390D §7: STP 後 1.6ms 以上待つ */
#define NE2K_OVW_WAIT_TICKS  2      /* 同じ待ちを tick で数える場合 (IF=1) */
#define NE2K_TX_TIMEOUT_TICKS 5     /* 50ms。1514B @10Mbps は 1.2ms + 衝突退避 */
#define NE2K_TX_REINIT_MAX   3      /* 連続タイムアウトでの再初期化上限 */
#define NE2K_BAD_HDR_MAX     4      /* 連続する不整合ヘッダで再初期化 */
#define NE2K_OVW_DRAIN_BUDGET 64    /* OVW 復旧時のリング回収上限 (16KB なら十分) */
#define NE2K_IRQ_BUDGET      4      /* IRQ 1 回 / タイマ補助 1 回で回収する受信フレーム数 (§5) */
#define NE2K_WATCHDOG_BUDGET 4      /* 100Hz ウォッチドッグ 1 回で回収する上限 (取りこぼし自己回復, M4) */
#define NE2K_IRQ_RECHECK     4      /* ACK 後の ISR / CURR 再確認の上限 (NP21/W の 2ms 再アサート抑制対策) */
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
    u8  in_irq;              /* IRQ / タイマ文脈で処理中 (待ちを tick で数える) */
    u8  rx_backlog;          /* 予算を使い切り、リングに未回収が残っている (IMR はマスク中) */

    /* TX */
    u8  tx_busy;
    u8  tx_reinit_run;       /* 連続タイムアウト数 */
    u32 tx_start_tick;

    /* OVW 復旧の状態機械 */
    u8  ovw_txp_was;         /* STP 時に TXP が立っていた */
    u8  ovw_tx_done;         /* その送信は既に PTX/TXE で終わっていた */
    u32 ovw_tick;
    u8  ovw_used_delay;      /* IF=0 だったので cpu_delay_us で待った */

    u8  bad_hdr_run;
    u8  last_curr;           /* 直近に読んだ CURR (getter が NIC に触れず空きを見積もる, M4) */

    /* ホスト側受信キュー (単一生産者 / 単一消費者) */
    u8  rxq_head, rxq_tail, rxq_count;
    u16 rxq_len[NE2K_RXQ_SLOTS];
    u8  rxq_buf[NE2K_RXQ_SLOTS][NE2K_RXQ_SLOT_BYTES];

    u8  tx_buf[NE2K_TX_BUF_LEN];

    struct ne2k_stats st;
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
    unsigned int f;
    __asm__ volatile("pushfl\n\tpopl %0" : "=r"(f));
    return (int)((f >> 9) & 1);
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

static void service(unsigned int budget);
static u8 read_curr(void);

/* 出るときは page0・DMA idle。busy 中の IRQ (pending) と、ACK 後に到着した分
 * (ISR に残ったビット、CURR と rx_next の差) を有限回だけ処理してから IMR を戻す。
 * 予算超過 (rx_backlog) のときは IMR をマスクしたままにし、タイマ補助か次の poll が
 * 続きを回収して戻す (§5)。 */
static void ne2k_leave(void)
{
    unsigned int f;
    int pass;

    /* **irq_pending の処理 → IMR 復帰 → busy = 0 を 1 つの irq_save で**
     * (往復 5 B2)。分かれていたころは「IMR は戻したが busy はまだ 1」の窓で
     * 来た IRQ が DEFERRED になり、誰も回収しなかった。
     * 残件 (W6): この区間が IF=0 なので、service() の失敗分岐から RDC 待ち・
     * 再初期化が IF=0 で回る。L-C で foreground に分ける対象。 */
    f = irq_save();
    cr_page0_idle();
    for (pass = 0; pass < NE2K_IRQ_RECHECK; pass++) {
        int need = nic.irq_pending;
        if (!need && nic.state == NE2K_STATE_RUNNING && !nic.rx_backlog) {
            if (rd(NE2K_P0_ISR) & NE2K_IMR_MASK) need = 1;
            else if (read_curr() != nic.rx_next) need = 1;
        }
        if (!need) break;
        nic.irq_pending = 0;
        nic.st.irq_rechecks++;
        service(NE2K_IRQ_BUDGET);
        cr_page0_idle();
    }
    ne2k_imr_sync((nic.irq_on && nic.state == NE2K_STATE_RUNNING &&
                   !nic.rx_backlog) ? nic.imr_mask : 0);
    nic.busy = 0;
    irq_restore(f);
}

/* ====================================================================== */
/*  Remote DMA                                                             */
/* ====================================================================== */

/* RDC を期限付きで待ち、立っていれば消して 0。期限切れは DMA を止めて負値。 */
static int wait_rdc(void)
{
    unsigned int waited = 0;
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

/* NIC RAM addr から len バイトを buf へ。page0 前提。
 * len が奇数でも RBCR は偶数丸め、PIO は末尾を安全に扱う。
 * RDC が期限内に立たない一過性のハズレは、リング全体を捨てる reinit ではなく
 * ここで 1 回だけ Remote DMA を再発行して吸収する (M4/L1 の堅牢性)。 */
static int dma_read(u16 addr, void *buf, u16 len)
{
    u16 n = (u16)((len + 1) & ~1);
    int try_i;
    if (len == 0) return NE2K_OK;
    for (try_i = 0; try_i < 2; try_i++) {
        wr(NE2K_P0_ISR, NE2K_ISR_RDC);      /* 古い RDC を消す */
        wr(NE2K_P0_RBCR0, (u8)(n & 0xFF));
        wr(NE2K_P0_RBCR1, (u8)(n >> 8));
        wr(NE2K_P0_RSAR0, (u8)(addr & 0xFF));
        wr(NE2K_P0_RSAR1, (u8)(addr >> 8));
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD0 | NE2K_CR_STA); /* STA で発行 (Linux ne.c / FreeBSD if_ed と同じ) */
        ne2k_pio_read(nic.cfg.data_port, buf, len);
        if (wait_rdc() == NE2K_OK) return NE2K_OK;
        nic.st.rdc_retry++;
    }
    return NE2K_ERR_TIMEOUT;
}

static int dma_write(u16 addr, const void *buf, u16 len)
{
    u16 n = (u16)((len + 1) & ~1);
    int try_i;
    if (len == 0) return NE2K_OK;
    for (try_i = 0; try_i < 2; try_i++) {
        wr(NE2K_P0_ISR, NE2K_ISR_RDC);
        wr(NE2K_P0_RBCR0, (u8)(n & 0xFF));
        wr(NE2K_P0_RBCR1, (u8)(n >> 8));
        wr(NE2K_P0_RSAR0, (u8)(addr & 0xFF));
        wr(NE2K_P0_RSAR1, (u8)(addr >> 8));
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD1 | NE2K_CR_STA);
        ne2k_pio_write(nic.cfg.data_port, buf, len);
        if (wait_rdc() == NE2K_OK) return NE2K_OK;
        nic.st.rdc_retry++;
    }
    return NE2K_ERR_TIMEOUT;
}

/* ====================================================================== */
/*  リセット・存在確認・PROM                                                 */
/* ====================================================================== */

static int hw_reset(void)
{
    unsigned int waited = 0;

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

    for (i = 0; i < 16; i++) {
        if (prom[2 * i] != prom[2 * i + 1]) doubled = 0;
    }
    nic.prom_doubled = (u8)doubled;
    for (i = 0; i < NE2K_ETH_ADDR_LEN; i++) {
        nic.mac[i] = doubled ? prom[2 * i] : prom[i];
        if (nic.mac[i] != 0x00) all0 = 0;
        if (nic.mac[i] != 0xFF) allf = 0;
    }
    /* 署名 'W' (byte 14/15、重複なら 28..31) */
    if (doubled) {
        if (prom[28] != NE2K_PROM_SIG || prom[30] != NE2K_PROM_SIG) return NE2K_ERR_NODEV;
    } else {
        if (prom[14] != NE2K_PROM_SIG || prom[15] != NE2K_PROM_SIG) return NE2K_ERR_NODEV;
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
        fill_pattern(w, NE2K_PAGE_SIZE, (u8)(0x11 + p));
        if (dma_write((u16)(first + p) << 8, w, NE2K_PAGE_SIZE)) {
            nic.st.diag_ram_errors += NE2K_PAGE_SIZE;
        }
    }
    for (p = 0; p < nic.ram_pages; p++) {
        fill_pattern(w, NE2K_PAGE_SIZE, (u8)(0x11 + p));
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
        kmemset(r, 0xA5, NE2K_RXQ_SLOT_BYTES);
        if (dma_read(base, r, len)) { nic.st.diag_dma_errors++; continue; }
        for (i = 0; i < len; i++) {
            if (r[i] != w[i]) { bad = 1; break; }
        }
        if (r[len] != 0xA5) bad = 1;            /* 奇数末尾で余白に書いた */
        if (bad) nic.st.diag_dma_errors++;
    }
}

/* ====================================================================== */
/*  初期化 (DP8390D §11)                                                    */
/* ====================================================================== */

static void program_ring(void)
{
    int i;
    u8 rcr;

    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    wr(NE2K_P0_DCR, NE2K_DCR_DEFAULT);
    wr(NE2K_P0_RBCR0, 0);
    wr(NE2K_P0_RBCR1, 0);
    wr(NE2K_P0_RCR, NE2K_RCR_MON);
    wr(NE2K_P0_TCR, NE2K_TCR_LOOP_INT);
    wr(NE2K_P0_TPSR, nic.tx_page);
    wr(NE2K_P0_PSTART, nic.rx_start);
    wr(NE2K_P0_PSTOP, nic.rx_stop);
    wr(NE2K_P0_BNRY, nic.rx_start);
    wr(NE2K_P0_ISR, NE2K_ISR_ALL);
    ne2k_imr_sync(0);

    wr(NE2K_CR, NE2K_CR_PAGE1 | NE2K_CR_RD2 | NE2K_CR_STP);
    for (i = 0; i < NE2K_ETH_ADDR_LEN; i++) wr((u8)(NE2K_P1_PAR0 + i), nic.mac[i]);
    for (i = 0; i < 8; i++) wr((u8)(NE2K_P1_MAR0 + i), 0);
    wr(NE2K_P1_CURR, (u8)(nic.rx_start + 1));
    nic.rx_next = (u8)(nic.rx_start + 1);

    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    nic.state = NE2K_STATE_RUNNING;
    cr_page0_idle();                                    /* STA */
    wr(NE2K_P0_TCR, (nic.cfg.flags & NE2K_CFG_LOOPBACK) ? NE2K_TCR_LOOP_INT : NE2K_TCR_NORMAL);
    rcr = NE2K_RCR_AB;
    if (nic.cfg.flags & NE2K_CFG_PROMISC) rcr |= NE2K_RCR_PRO;
    wr(NE2K_P0_RCR, rcr);
    wr(NE2K_P0_ISR, NE2K_ISR_ALL);
    /* **ここで IMR を戻さない** (往復 6 の R1)。再初期化は busy = 1 のまま
     * 走るので、ここで開けると「busy = 装置マスク済み」という DEFERRED の
     * 契約が破れる。復帰は呼び手の最終出口 (leave / アダプタの出口) が
     * ne2k_imr_sync で行う。IMR はこの関数を抜けた時点で 0。 */
}

static void reset_soft_state(void)
{
    nic.tx_busy = 0;
    nic.bad_hdr_run = 0;
    nic.rxq_head = nic.rxq_tail = nic.rxq_count = 0;
    nic.irq_pending = 0;
    nic.rx_backlog = 0;
}

/* 停止状態からの再構築。統計は保持する。 */
static int bring_up(void)
{
    int rc;

    nic.state = NE2K_STATE_STOPPED;
    rc = hw_reset();
    if (rc) return rc;
    rc = presence_check();
    if (rc) return rc;

    /* probe 中は STA + 内部 loopback + monitor で受信も送信も外に出さない */
    wr(NE2K_P0_RCR, NE2K_RCR_MON);
    wr(NE2K_P0_TCR, NE2K_TCR_LOOP_INT);
    rc = read_prom();
    if (rc) return rc;
    rc = probe_ram();
    if (rc) return rc;

    nic.tx_page  = nic.cfg.ram_page_first;
    nic.rx_start = (u8)(nic.cfg.ram_page_first + NE2K_TX_PAGES);
    nic.rx_stop  = (u8)(nic.cfg.ram_page_first + nic.ram_pages);

    if (nic.cfg.flags & NE2K_CFG_DIAG_RAM) diag_ram_test();
    if (nic.cfg.flags & NE2K_CFG_DIAG_DMA) diag_dma_test();

    reset_soft_state();
    program_ring();
    return NE2K_OK;
}

int ne2k_init(const struct ne2k_config *config)
{
    int rc;

    if (!config) return NE2K_ERR_INVAL;
    if (config->reg_base == 0 || config->data_port == 0) return NE2K_ERR_INVAL;
    if (config->ram_page_first == 0 || config->ram_pages_max < NE2K_RAM_PAGES_16K) return NE2K_ERR_INVAL;

    kmemset(&nic, 0, sizeof(nic));
    nic.cfg = *config;
    nic.imr_mask = 0;                       /* M3 で NE2K_ISR_* を入れる */

    rc = bring_up();
    if (rc) {
        /* 失敗したら NIC を止めて OS 起動は続けさせる */
        nic.state = NE2K_STATE_FAILED;
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
        ne2k_imr_sync(0);
        return rc;
    }
    return NE2K_OK;
}

void ne2k_stop(void)
{
    if (nic.state == NE2K_STATE_OFF) return;
    ne2k_enter();
    ne2k_imr_sync(0);
    nic.irq_on = 0;
    nic.imr_mask = 0;
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    nic.state = NE2K_STATE_STOPPED;
    reset_soft_state();
    nic.busy = 0;
    nic.irq_pending = 0;
}

/* 有限回の再初期化。上限を超えたら FAILED で NIC だけ止める。 */
static void reinit_or_fail(void)
{
    int rc;
    nic.st.reinit++;
    if (nic.tx_reinit_run >= NE2K_TX_REINIT_MAX) {
        nic.state = NE2K_STATE_FAILED;
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
        ne2k_imr_sync(0);
        return;
    }
    nic.tx_reinit_run++;
    rc = bring_up();
    if (rc) {
        nic.state = NE2K_STATE_FAILED;
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
        ne2k_imr_sync(0);
    }
}

/* ====================================================================== */
/*  受信リングの回収 (DP8390D §7)                                           */
/* ====================================================================== */

static u8 read_curr(void)
{
    u8 c;
    cr_page1_idle();
    c = rd(NE2K_P1_CURR);
    cr_page0_idle();
    nic.last_curr = c;
    return c;
}

/* CURR に合わせて再同期する (壊れたヘッダをたどらない) */
static void ring_resync(u8 curr)
{
    nic.st.resync++;
    nic.rx_next = curr;
    wr(NE2K_P0_BNRY, ne2k_ring_bnry_for(nic.rx_start, nic.rx_stop, curr));
}

/* リングから 1 フレーム分をホストキューへ。戻り値: 1 = 進んだ, 0 = 空, -1 = 再初期化要 */
static int rx_one(u8 curr)
{
    u8 raw[NE2K_RX_HDR_LEN];
    struct ne2k_rx_hdr h;
    u16 dlen = 0;
    int chk;
    int fcs_in_count = (nic.cfg.flags & NE2K_CFG_RX_COUNT_NO_FCS) ? 0 : 1;

    if (nic.rx_next == curr) return 0;

    if (dma_read((u16)nic.rx_next << 8, raw, NE2K_RX_HDR_LEN)) return -1;
    ne2k_ring_decode_hdr(raw, &h);
    chk = ne2k_ring_check(nic.rx_start, nic.rx_stop, nic.rx_next, &h, fcs_in_count, &dlen);

    if (chk == NE2K_RING_BAD_NEXT || chk == NE2K_RING_BAD_COUNT) {
        nic.st.bad_header++;
        if (++nic.bad_hdr_run >= NE2K_BAD_HDR_MAX) return -1;
        ring_resync(curr);
        return 0;
    }
    nic.bad_hdr_run = 0;

    if (chk == NE2K_RING_RX_ERROR) {
        nic.st.rx_err++;
    } else if (nic.rxq_count >= NE2K_RXQ_SLOTS) {
        nic.st.rx_dropped++;
    } else {
        u8 *dst = nic.rxq_buf[nic.rxq_tail];
        u16 addr = (u16)(((u16)nic.rx_next << 8) + NE2K_RX_HDR_LEN);
        u16 first = ne2k_ring_first_chunk(nic.rx_stop, addr, dlen);
        if (dma_read(addr, dst, first)) return -1;
        if (first < dlen) {
            if (dma_read((u16)nic.rx_start << 8, dst + first, (u16)(dlen - first))) return -1;
        }
        nic.rxq_len[nic.rxq_tail] = dlen;
        {
            unsigned int f = irq_save();
            nic.rxq_tail = (u8)((nic.rxq_tail + 1) % NE2K_RXQ_SLOTS);
            nic.rxq_count++;
            irq_restore(f);
        }
        nic.st.rx_frames++;
        nic.st.rx_bytes += dlen;
    }

    /* リングで消費したページ数を実測に足す (§2-3、リンク層の credit cost 用) */
    {
        unsigned int size = (unsigned int)(nic.rx_stop - nic.rx_start);
        if (size) nic.st.rx_pages_total +=
            ((unsigned int)h.next + size - nic.rx_next) % size;
    }
    /* データ回収後に next を進め、BNRY はその 1 ページ前 */
    nic.rx_next = h.next;
    wr(NE2K_P0_BNRY, ne2k_ring_bnry_for(nic.rx_start, nic.rx_stop, nic.rx_next));
    return 1;
}

/* budget フレームまで回収。戻り値 -1 = 再初期化要、1 = 予算を使い切り未回収が残る、0 = 空 */
static int rx_drain(unsigned int budget)
{
    while (budget > 0) {
        u8 curr = read_curr();
        int r = rx_one(curr);
        if (r < 0) return -1;
        if (r == 0) return 0;
        budget--;
    }
    return (read_curr() != nic.rx_next) ? 1 : 0;
}

/* ====================================================================== */
/*  OVW 復旧 (DP8390D §7 の手順を状態機械に分けたもの)                       */
/* ====================================================================== */

static void ovw_begin(u8 isr)
{
    nic.st.overwrite++;
    nic.ovw_txp_was = (u8)((rd(NE2K_CR) & NE2K_CR_TXP) ? 1 : 0);
    nic.ovw_tx_done = (u8)((isr & (NE2K_ISR_PTX | NE2K_ISR_TXE)) ? 1 : 0);
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    ne2k_imr_sync(0);                           /* 復旧完了まで NIC 割り込みは止める */
    nic.ovw_tick = tick_count;
    nic.ovw_used_delay = 0;
    nic.state = NE2K_STATE_OVW_WAIT;
}

/* 1.6ms 以上経ったら復旧を完了する。IF=0 なら校正済み delay で待つ。 */
static void ovw_continue(void)
{
    int resend;

    if (eflags_if() || nic.in_irq) {
        /* tick は IF=1 の foreground でもタイマ文脈 (tick を数えた直後) でも進む */
        if ((u32)(tick_count - nic.ovw_tick) < NE2K_OVW_WAIT_TICKS) return;
    } else if (!nic.ovw_used_delay) {
        cpu_delay_us(NE2K_OVW_WAIT_US);
        nic.ovw_used_delay = 1;
    }

    wr(NE2K_P0_RBCR0, 0);
    wr(NE2K_P0_RBCR1, 0);
    resend = nic.ovw_txp_was && !nic.ovw_tx_done;
    wr(NE2K_P0_TCR, NE2K_TCR_LOOP_INT);
    nic.state = NE2K_STATE_RUNNING;
    cr_page0_idle();                                    /* STA, loopback 中 */
    if (rx_drain(NE2K_OVW_DRAIN_BUDGET) < 0) {
        reinit_or_fail();
        return;
    }
    wr(NE2K_P0_ISR, NE2K_ISR_OVW);
    wr(NE2K_P0_TCR, (nic.cfg.flags & NE2K_CFG_LOOPBACK) ? NE2K_TCR_LOOP_INT : NE2K_TCR_NORMAL);
    if (resend) {
        /* TPSR / TBCR は残っているので送信だけ再開する */
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STA | NE2K_CR_TXP);
        nic.tx_start_tick = tick_count;
    } else {
        nic.tx_busy = 0;
    }
}

/* ====================================================================== */
/*  送信完了・カウンタ・タイムアウト                                          */
/* ====================================================================== */

static void tx_complete(u8 isr)
{
    u8 tsr = rd(NE2K_P0_TSR);
    if ((isr & NE2K_ISR_TXE) || !(tsr & NE2K_TSR_PTX)) {
        nic.st.tx_err++;
    } else {
        nic.st.tx_ok++;
    }
    if (tsr & NE2K_TSR_COL) nic.st.tx_collisions++;
    if (tsr & NE2K_TSR_ABT) nic.st.tx_aborted++;
    if (tsr & NE2K_TSR_FU)  nic.st.tx_underrun++;
    wr(NE2K_P0_ISR, NE2K_ISR_PTX | NE2K_ISR_TXE);
    nic.tx_busy = 0;
    nic.tx_reinit_run = 0;
}

static void read_counters(void)
{
    nic.st.rx_align  += rd(NE2K_P0_CNTR0);
    nic.st.rx_crc    += rd(NE2K_P0_CNTR1);
    nic.st.rx_missed += rd(NE2K_P0_CNTR2);
    wr(NE2K_P0_ISR, NE2K_ISR_CNT);
}

/* 送信タイムアウト: tick は IF=1 の文脈で進む。IF=0 の診断ループでは
 * TXP が消えないことを poll 回数ではなく呼び出し側の期限で扱う。 */
static void tx_check_timeout(void)
{
    if (!nic.tx_busy) return;
    if (!(rd(NE2K_CR) & NE2K_CR_TXP)) {
        /* 完了しているが ISR を見る前だった */
        tx_complete(rd(NE2K_P0_ISR));
        return;
    }
    if (!eflags_if() && !nic.in_irq) return;    /* IF=0 の診断ループでは tick が進まない */
    if ((u32)(tick_count - nic.tx_start_tick) >= NE2K_TX_TIMEOUT_TICKS) {
        nic.st.tx_timeouts++;
        reinit_or_fail();
    }
}

/* NIC を触る共通の 1 サイクル。呼び出し側が busy を握っていること。 */
static void service(unsigned int budget)
{
    u8 isr;

    if (nic.state == NE2K_STATE_OVW_WAIT) {
        ovw_continue();
        return;
    }
    if (nic.state != NE2K_STATE_RUNNING) return;

    isr = rd(NE2K_P0_ISR);
    if (isr & NE2K_ISR_OVW) {
        ovw_begin(isr);
        return;
    }
    if (isr & (NE2K_ISR_PTX | NE2K_ISR_TXE)) tx_complete(isr);
    if (isr & (NE2K_ISR_PRX | NE2K_ISR_RXE)) {
        if (isr & NE2K_ISR_RXE) nic.st.rx_err++;
        wr(NE2K_P0_ISR, NE2K_ISR_PRX | NE2K_ISR_RXE);
    }
    if (isr & NE2K_ISR_CNT) read_counters();
    if (isr & NE2K_ISR_RDC) wr(NE2K_P0_ISR, NE2K_ISR_RDC);

    /* リングは ISR に関係なく CURR で判定する (ACK 前後の到着を取りこぼさない) */
    {
        int r = rx_drain(budget);
        if (r < 0) {
            reinit_or_fail();
            return;
        }
        if (r == 1 && !nic.rx_backlog) nic.st.rx_backlog_events++;
        nic.rx_backlog = (u8)(r == 1);
    }
    tx_check_timeout();
}

/* ====================================================================== */
/*  公開 API                                                               */
/* ====================================================================== */

int ne2k_send(const void *frame, unsigned int length)
{
    u16 padded;
    int rc;

    if (!frame) return NE2K_ERR_INVAL;
    if (length < NE2K_ETH_HDR_LEN || length > NE2K_ETH_MAX_LEN) return NE2K_ERR_INVAL;
    if (nic.state == NE2K_STATE_OVW_WAIT) return NE2K_ERR_BUSY;
    if (nic.state != NE2K_STATE_RUNNING) return NE2K_ERR_STOPPED;

    ne2k_enter();
    if (nic.tx_busy) {
        service(0);                             /* 完了済みなら回収 */
        if (nic.tx_busy || nic.state != NE2K_STATE_RUNNING) {
            ne2k_leave();
            return (nic.state == NE2K_STATE_RUNNING) ? NE2K_ERR_BUSY : NE2K_ERR_STOPPED;
        }
    }

    /* 呼び出し元長 (length)、padding 後 (padded)、DMA の偶数丸めは dma_write 内で別 */
    kmemcpy(nic.tx_buf, frame, length);
    padded = (u16)length;
    if (padded < NE2K_ETH_MIN_LEN) {
        kmemset(nic.tx_buf + padded, 0, NE2K_ETH_MIN_LEN - padded);
        padded = NE2K_ETH_MIN_LEN;
    }
    nic.tx_buf[padded] = 0;                     /* 奇数長の最終 word 上位 */

    rc = dma_write((u16)nic.tx_page << 8, nic.tx_buf, padded);
    if (rc) {
        ne2k_leave();
        return NE2K_ERR_IO;
    }
    wr(NE2K_P0_TPSR, nic.tx_page);
    wr(NE2K_P0_TBCR0, (u8)(padded & 0xFF));
    wr(NE2K_P0_TBCR1, (u8)(padded >> 8));
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STA | NE2K_CR_TXP);
    nic.tx_busy = 1;
    nic.tx_start_tick = tick_count;
    nic.st.tx_accepted++;
    nic.st.tx_bytes += length;
    ne2k_leave();
    return NE2K_OK;
}

int ne2k_recv(void *frame, unsigned int capacity, unsigned int *length)
{
    u16 len;
    unsigned int f;

    if (!frame || !length) return NE2K_ERR_INVAL;
    if (nic.state == NE2K_STATE_OFF) return NE2K_ERR_STOPPED;
    if (nic.rxq_count == 0) return NE2K_ERR_AGAIN;

    len = nic.rxq_len[nic.rxq_head];
    *length = len;
    if (capacity < len) return NE2K_ERR_NOSPACE;
    kmemcpy(frame, nic.rxq_buf[nic.rxq_head], len);
    f = irq_save();
    nic.rxq_head = (u8)((nic.rxq_head + 1) % NE2K_RXQ_SLOTS);
    nic.rxq_count--;
    irq_restore(f);
    return NE2K_OK;
}

void ne2k_poll(unsigned int budget)
{
    if (nic.state == NE2K_STATE_OFF || nic.state == NE2K_STATE_STOPPED ||
        nic.state == NE2K_STATE_FAILED) return;
    ne2k_enter();
    service(budget);
    ne2k_leave();
}

/* ======================================================================== */
/*  ne2k_irq_shared — 共有 IRQ の口 (票 TASK_HAL_WIRING §1-1「LGY-98 のアダプタ」)
 *
 *  kernel/irq.c の表から呼ばれる。線は他の装置と共有され得るので、
 *  **自分の要因が無ければ必ず IRQ_NONE を返す** (受けたふりをすると
 *  相方の要因が残ったまま線が上がり続ける)。
 *
 *    IRQ_NONE     : 自分の要因ではない
 *    IRQ_HANDLED  : 要因を落とし、残件も無い
 *    IRQ_DEFERRED : 自分の要因だが残件がある。**装置はマスクしてある** —
 *                   回収は ne2k_timer_tick (M4 の受信ウォッチドッグ。
 *                   契約が要求する「定期回収」そのもの) が行い、
 *                   回収し終えたら ne2k_imr_sync でマスクを外す。
 * ======================================================================== */
int ne2k_irq_shared(unsigned int irq, void *arg)
{
    int pass;

    (void)irq;
    (void)arg;

    nic.st.irq_count++;
    if (nic.busy) {
        /* busy 中の入口は NIC レジスタを変更しない (foreground のページ切替・
         * remote DMA と衝突する)。leave() が irq_pending を見て処理する。 */
        nic.irq_pending = 1;
        nic.st.irq_deferred++;
        return IRQ_DEFERRED;
    }
    /* 装置が止まっている状態 (OFF / STOPPED / FAILED) では IMR = 0 で、
     * この線を上げているのは自分ではない。**票からの逸脱** (票は (b) の
     * ISR 検査だけを書いている): ここを通さないと FAILED の NIC が
     * IRQ_DEFERRED を返し続け、tick 回収を持たない状態 (ne2k_timer_tick は
     * 非 RUNNING/OVW_WAIT で即戻る) と組み合わさって残件が永久に残る。 */
    if (nic.state != NE2K_STATE_RUNNING && nic.state != NE2K_STATE_OVW_WAIT) {
        return IRQ_NONE;
    }
    /* (b) ISR に要因が無く、リングにも未回収が無ければ自分ではない。 */
    if (!(rd(NE2K_P0_ISR) & NE2K_IMR_MASK) && !nic.rx_backlog) return IRQ_NONE;

    nic.busy = 1;
    nic.in_irq = 1;
    for (pass = 0; pass < NE2K_IRQ_RECHECK; pass++) {
        service(NE2K_IRQ_BUDGET);
        cr_page0_idle();
        if (nic.rx_backlog || nic.state != NE2K_STATE_RUNNING) break;
        /* ACK 後に到着した分。NP21/W は 2ms 以内の再アサートを抑えるので、
         * ここで見ないと次の IRQ まで取り残す (実機でも安全側)。 */
        if (!(rd(NE2K_P0_ISR) & NE2K_IMR_MASK) && read_curr() == nic.rx_next) break;
        nic.st.irq_rechecks++;
    }
    /* 予算超過なら IMR をマスクしたまま返し、tick / poll が続きを回収して戻す。
     * **shadow と実 IMR は必ず一緒に動く** (1-6 / 往復 7 B3)。 */
    ne2k_imr_sync((nic.irq_on && !nic.rx_backlog &&
                   nic.state == NE2K_STATE_RUNNING) ? nic.imr_mask : 0);
    nic.in_irq = 0;
    nic.busy = 0;

    /* **判定は imr_written (実 IMR)**。imr_mask は「戻したい値」なので、
     * OVW 復旧待ちや FAILED で 0 を書いた直後でも非 0 のまま = 誤判定になる。 */
    return (nic.rx_backlog || nic.imr_written == 0) ? IRQ_DEFERRED : IRQ_HANDLED;
}

void ne2k_irq_enable(void)
{
    if (nic.state == NE2K_STATE_OFF) return;
    nic.imr_mask = NE2K_IMR_MASK;
    nic.irq_on = 1;
    /* 有効化前に届いていた分を回収してから IMR を入れる (poll の leave が行う) */
    ne2k_poll(NE2K_IRQ_BUDGET);
}

/* 100Hz の受信ウォッチドッグ (M4)。NIC IRQ を取りこぼしても (NP21/W の 2ms
 * 再アサート抑制やエッジ喪失、リング飽和後の停止) ここで CURR を見て回収するので、
 * 「フレームがリングに残ったまま二度と IRQ が来ない」wedge を防ぐ。健全時は CURR ==
 * rx_next で 1 回の CURR 読みだけで戻る (安価)。溢れそのものはリンク層の Credit で
 * 防ぐ ([LINK_PLAN.md])。ここは「溢れても・取りこぼしても壊れない」安全網。 */
void ne2k_timer_tick(void)
{
    u32 before;
    if (nic.busy) return;                       /* foreground 操作中は延期 (再入しない) */
    if (nic.state != NE2K_STATE_RUNNING && nic.state != NE2K_STATE_OVW_WAIT) return;
    nic.in_irq = 1;
    before = nic.st.rx_frames + nic.st.rx_dropped;
    ne2k_poll(nic.rx_backlog ? NE2K_IRQ_BUDGET : NE2K_WATCHDOG_BUDGET);
    /* この tick で回収できたフレーム = IRQ が取りこぼしていた分 (健全時は 0)。 */
    nic.st.watchdog_frames += (nic.st.rx_frames + nic.st.rx_dropped) - before;
    if (nic.st.rx_frames + nic.st.rx_dropped != before) nic.st.watchdog_hits++;
    nic.in_irq = 0;
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

void ne2k_get_mac(u8 mac[6])
{
    kmemcpy(mac, nic.mac, NE2K_ETH_ADDR_LEN);
}

unsigned int ne2k_ram_bytes(void)
{
    return (unsigned int)nic.ram_pages * NE2K_PAGE_SIZE;
}
