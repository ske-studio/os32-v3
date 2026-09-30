/* ======================================================================== */
/*  LGY98.C — メルコ LGY-98 (NE2000 互換 C バス LAN) の OS32 接続             */
/*                                                                          */
/*  BASE / IRQ の検証、カード固有ポートの割り当て、起動時の初期化入口。       */
/*  8390 の制御は drivers/ne2000.c。計画: docs/tasks/network/PLAN.md。        */
/*                                                                          */
/*  「カードの INT 番号」「PIC IRQ」「IDT ベクタ」を混同しない:              */
/*  設定とログはすべて PIC IRQ (3 / 5 / 6) で扱う。                          */
/* ======================================================================== */

#include "lgy98.h"
#include "ne2000.h"
#include "np2sysp.h"
#include "config.h"
#include "kprintf.h"
#include "idt.h"
#include "irq.h"       /* irq_register / IRQ_F_SHARED (票 TASK_HAL_WIRING §1-1) */
#include "io.h"
#include "kstring.h"
#include "link.h"

/* IRQ 入口は共通スタブ (kernel/isr_stub.asm の irq_stub_common_*) に移した。
 * どの線に結ぶかは irq_register() が決める (票 TASK_HAL_WIRING §1-1)。 */

/* 反射モード (LGY98_FLAG_REFLECT)。static にするとホストから読めないので意図的にグローバル。 */
static int lgy98_reflect_on = 0;
u32 lgy98_reflected = 0;
u32 lgy98_reflect_fail = 0;
static u8 lgy98_reflect_buf[1514 + 2];

int lgy98_validate_base(unsigned int base)
{
    if (base < LGY98_BASE_LOW || base > LGY98_BASE_HIGH) return NE2K_ERR_INVAL;
    if ((base & LGY98_BASE_LOWMASK) != LGY98_BASE_LOW) return NE2K_ERR_INVAL;
    return NE2K_OK;
}

int lgy98_validate_irq(unsigned int irq)
{
    if (irq == LGY98_INT0_IRQ || irq == LGY98_INT1_IRQ || irq == LGY98_INT2_IRQ) {
        return NE2K_OK;
    }
    if (irq == LGY98_INT5_IRQ) return NE2K_ERR_IRQ;   /* 音源 / V86 との排他が未決 */
    return NE2K_ERR_INVAL;
}

static void print_mac(const u8 *mac)
{
    static const char hex[] = "0123456789abcdef";
    char buf[18];
    int i;
    for (i = 0; i < 6; i++) {
        buf[i * 3]     = hex[mac[i] >> 4];
        buf[i * 3 + 1] = hex[mac[i] & 0x0F];
        buf[i * 3 + 2] = (i == 5) ? '\0' : ':';
    }
    kprintf(0x07, "%s", buf);
}

int lgy98_attach(unsigned int base, unsigned int irq, unsigned int flags)
{
    struct ne2k_config cfg;
    struct ne2k_stats st;
    u8 mac[6];
    int rc;

    rc = lgy98_validate_base(base);
    if (rc) {
        kprintf(0xC1, "[lgy98] invalid base 0x%x (0x00D0 + n*0x1000, n=0..7)\n", base);
        return rc;
    }
    rc = lgy98_validate_irq(irq);
    if (rc) {
        kprintf(0xC1, "[lgy98] irq %d not usable (3/5/6 only; 12 is reserved)\n", (int)irq);
        return rc;
    }

    cfg.reg_base       = (u16)(base + LGY98_OFS_REGS);
    cfg.data_port      = (u16)(base + LGY98_OFS_DATA);
    cfg.reset_port     = (u16)(base + LGY98_OFS_RESET);
    cfg.irq            = (u8)irq;
    cfg.ram_page_first = LGY98_RAM_PAGE_FIRST;
    cfg.ram_pages_max  = LGY98_RAM_PAGES_32K;
    cfg.flags          = 0;
    /* NP21/W の受信ヘッダ count は FCS を含まない (PLAN.md §8)。実カードは
     * DP8390D の定義どおり含む前提で、M0 の実機確認項目。 */
    if (np2_detect()) cfg.flags |= NE2K_CFG_RX_COUNT_NO_FCS;
    if (flags & LGY98_FLAG_DIAG) cfg.flags |= NE2K_CFG_DIAG_RAM | NE2K_CFG_DIAG_DMA;
    if (flags & LGY98_FLAG_LOOPBACK) cfg.flags |= NE2K_CFG_LOOPBACK;
    lgy98_reflect_on = 0;

    rc = ne2k_init(&cfg);
    if (rc) {
        kprintf(0xC1, "[lgy98] init failed rc=%d (base 0x%x irq %d) -> NIC disabled\n",
                rc, base, (int)irq);
        return rc;
    }

    ne2k_get_mac(mac);
    kprintf(0x07, "[lgy98] base 0x%x irq %d mac ", base, (int)irq);
    print_mac(mac);
    kprintf(0x07, " ram %dKB%s%s\n", (int)(ne2k_ram_bytes() / 1024),
            (cfg.flags & NE2K_CFG_RX_COUNT_NO_FCS) ? " (np21w)" : "",
            (cfg.flags & NE2K_CFG_LOOPBACK) ? " loopback" : "");
    if (flags & LGY98_FLAG_REFLECT) {
        lgy98_reflect_on = 1;
        kprintf(0x07, "[lgy98] reflect mode (M2 test): rx frames are sent back with MACs swapped\n");
    }
    /* 反射モードではリンク層を起こさない — RX キューの消費者は反射か
     * リンク層のどちらか 1 つ (TASK_N0 §2a、往復 2 の R10)。link_init を
     * 呼ばなければ link_tick は何もせず戻る。 */
    if (!(flags & LGY98_FLAG_REFLECT)) link_init(mac);
    /* IRQ 駆動へ (票 TASK_HAL_WIRING §1-1)。空きの判定は PIC の IMR を覗く
     * irq_is_free() ではなく **irq_register の戻り** で行う — 共有登録では
     * 「マスクが開いている = 使用中」が成り立たない。
     *
     * 順序: irq_register → ne2k_irq_enable。登録が先なら、PIC のマスクが
     * 開いた時点でハンドラは既に表に居り、NIC はまだ IMR = 0 なので
     * 要因を上げられない。IDT ゲートは起動時から共通スタブ。 */
    {
        int irc = irq_register(irq, ne2k_irq_shared, NULL, IRQ_F_SHARED);
        if (irc < 0) {
            /* 登録を断られた線では IRQ 駆動に入らない。NIC 自体は初期化済みで、
             * ne2k_timer_tick (100Hz の受信ウォッチドッグ) が回収を続けるので
             * **ポーリングだけで動く**。
             * ⚠ 票 §1-1 は「登録を拒否された装置はこの票では利用不可」と
             * 書いている (往復 7 B1)。あちらは PCI の probe 経路の決定で、
             * ここは PM の指示に従って従来どおり NIC を落とさない。 */
            kprintf(0xC1, "[lgy98] irq %d registration refused (rc=%d) -> polling only\n",
                    (int)irq, irc);
        } else {
            ne2k_irq_enable();
        }
    }
    if (flags & LGY98_FLAG_LINKTEST) {
        link_selftest(10);          /* L0: HELLO + PING/PONG */
        link_l1_bulk(200, 512);     /* L1: WINDOW/Credit で 200 フレームを溢れさせず受ける */
        link_l2_stream(131072, 512, 100);  /* L2: 128KB を再結合なしで消費、seq100 で欠落→Go-Back-N 回復 */
        link_l3_service();                 /* L3: Host Services (GET / 404 / 実HTTP / TIME) */
    }
    if (flags & LGY98_FLAG_DIAG) {
        ne2k_get_stats(&st);
        kprintf(st.diag_ram_errors || st.diag_dma_errors ? 0xC1 : 0x07,
                "[lgy98] diag: ram errors %d, dma errors %d, rdc timeouts %d\n",
                (int)st.diag_ram_errors, (int)st.diag_dma_errors, (int)st.rdc_timeout);
    }
    return NE2K_OK;
}

int lgy98_init(void)
{
#if CONFIG_LGY98_BASE == 0
    return 0;                       /* 未設定 = 無効 (既定) */
#else
    return lgy98_attach(CONFIG_LGY98_BASE, CONFIG_LGY98_IRQ, CONFIG_LGY98_FLAGS);
#endif
}

void lgy98_tick(void)
{
    int n;
    if (!lgy98_reflect_on) return;
    if (ne2k_state() != NE2K_STATE_RUNNING) return;
    if (ne2k_is_busy()) return;                 /* foreground の send/recv を中断した tick では触らない */
    for (n = 0; n < 2; n++) {
        unsigned int len = 0;
        u8 tmp[6];
        int rc = ne2k_recv(lgy98_reflect_buf, sizeof(lgy98_reflect_buf), &len);
        if (rc != NE2K_OK) break;
        if (len < 14) { lgy98_reflect_fail++; continue; }
        kmemcpy(tmp, lgy98_reflect_buf, 6);
        kmemcpy(lgy98_reflect_buf, lgy98_reflect_buf + 6, 6);
        kmemcpy(lgy98_reflect_buf + 6, tmp, 6);
        if (ne2k_send(lgy98_reflect_buf, len) == NE2K_OK) lgy98_reflected++;
        else lgy98_reflect_fail++;
    }
}
