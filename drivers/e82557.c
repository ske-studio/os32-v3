/* LB-3: probe loopback, bounded IRQ/tick service and foreground packet API. */
#include "e82557.h"
#include "pci_bind.h"
#include "e82557_eeprom.h"
#include "e82557_desc.h"
#include "mm/dma_pool.h"
#include "kernel/irq/irq.h"
#ifndef E82557_HOST_TEST
#include "io.h"
#include "arch/x86/kernel/idt.h"
#include "kernel/time/cpu_calibrate.h"
#include "init/sysconfig.h"
#include "kernel/module/module.h"
#include "fs/vfs.h"
#include "bootinfo.h"
#include "config.h"
#include "kprintf.h"
#include "kstring.h"
#endif

#ifdef OS32_LAN_MODULE
#include "lan_module.h"
extern u32 ktime_ticks(void);
#define tick_count (ktime_ticks())
#define irq_register lan_module_irq_register
#define irq_unregister lan_module_irq_unregister
#define dma_alloc lan_module_dma_alloc
#define dma_free lan_module_dma_free
#endif

#define E82557_VAR_DIR "/var"
#define E82557_LOG_DIR "/var/log"
#define E82557_CRC_DIGITS 8
#define E82557_SEQ_MAX 0xFFFFFFFFu

enum try_result { TRY_NONE, TRY_SAME_CRC, TRY_OTHER_CRC, TRY_READ_FAIL };
enum issue_result { ISSUE_OK, ISSUE_TIMEOUT };
enum tx_state { TX_IDLE, TX_RESERVED, TX_ISSUED };
#ifndef E82557_HOST_YIELD
#define E82557_HOST_YIELD() ((void)0)
#endif
enum { E82557_RS_STOP = 1, E82557_RS_REOWN, E82557_RS_RESTART };

static struct {
    struct dma_buf dma;
    struct e82557_stop stop;
    struct e82557_rx rx;
    struct pci_dev dev;
    u32 stats[E82557_ST_COUNT], gen, tx_issued, reset_begin, stop_tries;
    u16 base;
    u8 mac[E82557_MAC_LEN];
    int state, tx, recover_step, recover_pending, rnr_pending;
    int swi_wait;
} nic;

/* Exact, bounded decimal parse; no wrap, signs, trailing data or empty record. */
static int seq_parse(const char *buf, int len, u32 *seq)
{
    u32 n = 0;
    int i;
    if (len < 2 || buf[len - 1] != '\n') return 0;
    for (i = 0; i < len - 1; i++) {
        unsigned int d = (unsigned char)buf[i] - '0';
        if (d > 9 || n > (E82557_SEQ_MAX - d) / 10) return 0;
        n = n * 10 + d;
    }
    if (!n) return 0;
    *seq = n;
    return 1;
}

/* NOTFOUND alone means absent. A malformed readable mark also fails closed. */
static int try_decide(int rc, const char *buf, u32 crc, u32 *seq)
{
    u32 old = 0;
    int i;
    if (rc == VFS_ERR_NOTFOUND) return TRY_NONE;
    if (rc < E82557_CRC_DIGITS + 3 || rc >= E82557_TRY_CAP ||
        buf[E82557_CRC_DIGITS] != ' ') return TRY_READ_FAIL;
    for (i = 0; i < E82557_CRC_DIGITS; i++) {
        unsigned int d = (unsigned char)buf[i] - '0';
        if (d > 9) d = (unsigned char)buf[i] - 'a' + 10;
        if (d > 15) return TRY_READ_FAIL;
        old = (old << 4) | d;
    }
    if (!seq_parse(buf + E82557_CRC_DIGITS + 1,
                   rc - E82557_CRC_DIGITS - 1, seq)) return TRY_READ_FAIL;
    return old == crc ? TRY_SAME_CRC : TRY_OTHER_CRC;
}

static int persist(const char *path, const char *buf, int len)
{
    char back[E82557_TRY_CAP];
    int rc = vfs_write(path, buf, (u32)len);
    /* ext2 returns 0; FAT returns bytes written. Reject positive short writes. */
    if ((rc != 0 && rc != len) || vfs_sync() < 0) return 0;
    rc = vfs_read(path, back, sizeof(back));
    return rc == len && kstrncmp(buf, back, (u32)len) == 0;
}

/* Returns a skip reason; only success permits any NIC I/O. */
static const char *try_begin(void)
{
    BootImageInfo image;
    char buf[E82557_TRY_CAP];
    u32 seq = 0, oldseq = 0;
    int rc, decision, len, i;
    if (sysconfig_get_int(SYS_SYSTEM_CFG, "E82557", 1) == 0) return "cfg";
    rc = vfs_mkdir(E82557_VAR_DIR);
    if (rc < 0 && rc != OS32_ERR_EXIST) return "try-dir";
    rc = vfs_mkdir(E82557_LOG_DIR);
    if (rc < 0 && rc != OS32_ERR_EXIST) return "try-dir";
    if (boot_image_info(&image) < 0 || !image.crc_valid) return "try-crc";
    rc = vfs_read(E82557_TRY_PATH, buf, sizeof(buf));
    decision = try_decide(rc, buf, image.image_crc, &oldseq);
    if (decision == TRY_READ_FAIL) return "try-io";
    if (decision == TRY_SAME_CRC) {
        kprintf(E82557_LOG_ATTR, "[e82557] skipped (previous boot stopped, rm /var/log/lan.try to retry)\n");
        return "previous";
    }
    rc = vfs_read(E82557_SEQ_PATH, buf, sizeof(buf));
    if (rc != VFS_ERR_NOTFOUND &&
        (rc < 0 || rc >= (int)sizeof(buf) || !seq_parse(buf, rc, &seq)))
        return "seq-io";
    /* Preserve monotonicity even when recovering an older mark without seq. */
    if (seq < oldseq) seq = oldseq;
    if (seq == E82557_SEQ_MAX) return "seq-overflow";
    seq++;
    len = kutoa_dec(seq, buf, sizeof(buf));
    buf[len++] = '\n';
    if (!persist(E82557_SEQ_PATH, buf, len)) return "seq-write";
    for (i = 0; i < E82557_CRC_DIGITS; i++) {
        unsigned int d = (image.image_crc >> ((E82557_CRC_DIGITS - i - 1) * 4)) & 15;
        buf[i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    }
    buf[E82557_CRC_DIGITS] = ' ';
    len = E82557_CRC_DIGITS + 1;
    len += kutoa_dec(seq, buf + len, sizeof(buf) - len);
    buf[len++] = '\n';
    if (!persist(E82557_TRY_PATH, buf, len)) return "try-write";
    kprintf(E82557_LOG_ATTR, "[e82557] try seq=%u crc=%08x\n", seq, image.image_crc);
    return NULL;
}

static void nic_trace(u32 stage)
{
    /* Normal boot context only: the module performs VFS I/O. */
#ifdef OS32_LAN_MODULE
    lan_module_trace(stage);
#else
    if (kdebug.ops.nic_trace) kdebug.ops.nic_trace(stage);
#endif
}

static void scb_mask(u16 base, int on)
{
    outp(base + E82557_SCB_INTCTL, on ? E82557_SCB_M : 0);
}

static int scb_wait(u16 base)
{
    u32 elapsed = 0;
    for (;;) {
        if (inp(base + E82557_SCB_COMMAND) == 0) return 1;
        if (elapsed == E82557_SCB_WAIT_US) return 0;
        cpu_delay_us(E82557_SCB_POLL_US);
        elapsed += E82557_SCB_POLL_US;
    }
}

/* Caller holds irq_save. Do not touch pointer while a command is outstanding.
 * The byte write never changes M at +3. ISSUE_OK means accepted, not queued. */
static int scb_issue(u16 base, u8 cuc, u8 ruc, u32 ptr, int need_ptr, int *after)
{
    *after = 0;
    if (!scb_wait(base)) { nic.stats[E82557_ST_SCB_STUCK]++; return ISSUE_TIMEOUT; }
    if (need_ptr) outpd(base + E82557_SCB_POINTER, ptr);
    outp(base + E82557_SCB_COMMAND, e82557_scb_cmd(cuc, ruc));
    *after = 1;
    if (scb_wait(base)) return ISSUE_OK;
    nic.stats[E82557_ST_SCB_STUCK]++;
    return ISSUE_TIMEOUT;
}

static void ee_write(void *ctx, u8 value)
{
    outp(*(u16 *)ctx + E82557_EE_CTRL, value);
}
static u8 ee_read(void *ctx)
{
    return inp(*(u16 *)ctx + E82557_EE_CTRL);
}
static void ee_delay(void *ctx, u32 us)
{
    (void)ctx;
    cpu_delay_us(us);
}

static int ee_mac_read(u16 base, u8 *mac, int *selected_width)
{
    struct e82557_ee_pins pins = { &base, ee_write, ee_read, ee_delay, E82557_EE_STEP_US };
    u16 words[E82557_MAC_WORDS], ignored;
    int attempt, addr, dummy, width = E82557_EE_WIDTH6;
    for (attempt = 0; attempt < 2; attempt++) {
        if (attempt && e82557_ee_read_word(&pins, E82557_EE_AUTO,
                    &width, 0, &ignored, &dummy) != E82557_OK) return 0;
        for (addr = 0; addr < E82557_MAC_WORDS; addr++) {
            if (e82557_ee_read_word(&pins, E82557_EE_FIXED, &width,
                    (u16)addr, &words[addr], &dummy) != E82557_OK || dummy != 0)
                break;
        }
        if (addr != E82557_MAC_WORDS) continue;
        e82557_ee_mac(words, mac);
        if (e82557_mac_form(mac) == E82557_OK) { *selected_width = width; return 1; }
    }
    return 0;
}

/* Foreground only. All three CBs share this publication/completion protocol. */
static const char *cb_run(u16 base, struct dma_buf *dma, u32 off,
                          struct e82557_stop *stop)
{
    u8 *cb = (u8 *)dma->va + off;
    int after, issued;
    unsigned int flags;
    E82557_BARRIER(); /* publish: last descriptor store precedes SCB */
    flags = irq_save();
    stop->start_seq = stop->reset_seq;
    issued = scb_issue(base, E82557_CU_START, 0, dma->pa + off, 1, &after);
    irq_restore(flags);
    if (issued != ISSUE_OK) return after ? "post" : "pre";
    for (u32 ms = 0; ; ms++) {
        u16 st = e82557_rd16(cb);
        if (st & E82557_CB_C) {
            E82557_BARRIER(); /* acquire: C before completion/body consumption */
            return (st & E82557_CB_OK) ? NULL : "ng";
        }
        if (ms == E82557_CB_WAIT_MS) return "timeout";
        cpu_delay_us(E82557_CB_POLL_US);
    }
}

static int stop_dma(u16 base, struct e82557_stop *stop, u32 attempts)
{
    u32 before = stop->reset_seq;
    for (u32 attempt = 0; attempt < attempts; attempt++) {
        outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);
        stop->reset_seq++;
        cpu_delay_us(E82557_RESET_US);
        /* Reset loses M: first SCB access, as FreeBSD if_fxp.c:2177-2180. */
        scb_mask(base, 1);
        if (e82557_stop_evidence(stop, before, inpw(base + E82557_SCB_STATUS)))
            return 1;
    }
    return 0;
}

static void bm_restore(const struct pci_dev *dev, int changed)
{
    if (changed) {
        u16 cmd = (u16)pci_cfg_read32(dev->bus, dev->dev, dev->fn, PCI_CFG_COMMAND);
        (void)pci_cfg_write16(dev->bus, dev->dev, dev->fn, PCI_CFG_COMMAND,
                             cmd & ~E82557_COMMAND_BM);
    }
}

static void hello_build(u8 *frame, const u8 *mac)
{
    const char mark[E82557_HELLO_MARK_LEN] = "OS32-LB-HELLO";
    for (u32 i = 0; i < E82557_HELLO_LEN; i++) frame[i] = 0;
    for (u32 i = 0; i < E82557_MAC_LEN; i++) {
        frame[i] = 0xff;
        frame[E82557_MAC_LEN + i] = mac[i];
        frame[ETH_HLEN + E82557_HELLO_MARK_LEN + i] = mac[i];
    }
    frame[ETH_TYPE_OFF] = E82557_HELLO_TYPE >> 8;
    frame[ETH_TYPE_OFF + 1] = E82557_HELLO_TYPE & 0xff;
    for (u32 i = 0; i < E82557_HELLO_MARK_LEN; i++)
        frame[ETH_HLEN + i] = (u8)mark[i];
}

struct phy_observation {
    u8 addr;
    u16 bmcr, bmsr_first, bmsr, id1, id2, old, value, par;
    int nsc, pcr_valid, par_valid, pcr_off, pcr_bad, link, an_done;
    u32 waited;
    const char *error;
};

static int mdi_xfer(u16 base, u8 op, u8 phy, u8 reg, u16 *value)
{
    outpd(base + E82557_MDI, e82557_mdi_ctl(op, phy, reg,
                       op == E82557_MDI_WRITE ? *value : 0));
    for (u32 us = 0; ; us += E82557_MDI_POLL_US) {
        u32 ctl = inpd(base + E82557_MDI);
        if (e82557_mdi_ready(ctl)) {
            *value = e82557_mdi_data(ctl);
            return 1;
        }
        if (us == E82557_MDI_WAIT_US) return 0;
        cpu_delay_us(E82557_MDI_POLL_US);
    }
}
static int mdi_read(u16 base, struct phy_observation *p, u8 reg, u16 *value)
{
    if (p->error) return 0;
    if (mdi_xfer(base, E82557_MDI_READ, p->addr, reg, value)) return 1;
    p->error = "timeout";
    return 0;
}
static int phy_status(u16 base, struct phy_observation *p)
{
    /* Preserve latched-low history as well as the current (second) value. */
    return mdi_read(base, p, E82557_PHY_BMSR, &p->bmsr_first) &&
           mdi_read(base, p, E82557_PHY_BMSR, &p->bmsr);
}
static void tick_wait(u32 start, u32 ticks)
{
    while ((u32)(tick_count - start) < ticks) { /* IF=1, foreground only */ }
}
static void phy_prepare(u16 base, struct phy_observation *p, int width)
{
    struct e82557_ee_pins pins = { &base, ee_write, ee_read, ee_delay, E82557_EE_STEP_US };
    u16 word;
    int dummy;
    if (e82557_ee_read_word(&pins, E82557_EE_FIXED, &width,
                          E82557_EE_PHY, &word, &dummy) != E82557_OK || dummy) {
        p->error = "id";
        return;
    }
    p->addr = word & E82557_PHY_MASK;
    if (!mdi_read(base, p, E82557_PHY_ID1, &p->id1) ||
        !mdi_read(base, p, E82557_PHY_ID2, &p->id2)) return;
    if (!e82557_phy_id_valid(p->id1)) { p->error = "id"; return; }
    p->nsc = p->id1 == E82557_NSC_ID1 &&
             (p->id2 & E82557_NSC_ID2_MASK) == E82557_NSC_ID2;
    (void)mdi_read(base, p, E82557_PHY_BMCR, &p->bmcr);
}
/* FreeBSD main sys/dev/mii/nsphy.c:145-179: old | 0x0562.
 * Reserved bits 10/8 are written but only documented R/W bits are verified. */
static void phy_vendor(u16 base, struct phy_observation *p)
{
    u16 rb;
    if (p->error || !p->nsc) return;
    if (!mdi_read(base, p, E82557_NSC_PCR, &p->old)) return;
    p->value = rb = p->old;
    p->pcr_off = !sysconfig_get_int(SYS_SYSTEM_CFG, "E82557_PHYVENDOR", 1);
    if (!p->pcr_off) {
        p->value = p->old | E82557_NSC_PCR_SET;
        if (p->value != p->old) {
            u16 write = p->value;
            if (!mdi_xfer(base, E82557_MDI_WRITE, p->addr, E82557_NSC_PCR, &write)) {
                p->error = "timeout";
                return;
            }
            if (!mdi_read(base, p, E82557_NSC_PCR, &rb)) return;
        }
        if ((rb & E82557_NSC_PCR_VERIFY) != (p->value & E82557_NSC_PCR_VERIFY))
            p->pcr_bad = 1;
    }
    p->pcr_valid = 1;
}
static void phy_link_wait(u16 base, struct phy_observation *p)
{
    u32 start = tick_count, elapsed = 0;
    int an_en;
    while (phy_status(base, p)) {
        elapsed = tick_count - start;
        e82557_phy_decode(p->bmcr, p->bmsr, &p->link, &p->an_done, &an_en);
        if (e82557_link_wait_step(p->link, p->an_done, an_en, elapsed,
                                  E82557_LINK_WAIT_TICKS) != E82557_WAIT) break;
        u32 next = elapsed + E82557_PHY_POLL_TICKS;
        tick_wait(start, next < E82557_LINK_WAIT_TICKS ? next : E82557_LINK_WAIT_TICKS);
    }
    p->waited = elapsed * E82557_TICK_MS;
    if (p->nsc && mdi_read(base, p, E82557_NSC_PAR, &p->par)) p->par_valid = 1;
}
static void phy_report(const struct phy_observation *p, u16 compat)
{
    kprintf(E82557_LOG_ATTR, "[e82557] phy=%u id=%04x:%04x pcr=", p->addr, p->id1, p->id2);
    if (p->error) kprintf(E82557_LOG_ATTR, "- mdi=%s", p->error);
    else if (!p->nsc) kprintf(E82557_LOG_ATTR, "skip");
    else if (p->pcr_bad) kprintf(E82557_LOG_ATTR, "ng");
    else if (p->pcr_off) kprintf(E82557_LOG_ATTR, "off");
    else if (p->pcr_valid) kprintf(E82557_LOG_ATTR, "%04x->%04x", p->old, p->value);
    else kprintf(E82557_LOG_ATTR, "-");
    kprintf(E82557_LOG_ATTR, " link=%s an=%s wait=%u par=",
            p->link ? "up" : "down", p->an_done ? "done" : "no", p->waited);
    if (p->par_valid && !p->error) kprintf(E82557_LOG_ATTR, "%04x", p->par);
    else kprintf(E82557_LOG_ATTR, "-");
    kprintf(E82557_LOG_ATTR, " compat=%04x\n", compat);
}

static u8 *rx_ring(void) { return (u8 *)nic.dma.va + E82557_RFD_OFF; }
static void rx_scan(void) { e82557_rx_scan(rx_ring(), &nic.rx, &nic.rnr_pending); }
static u32 rx_pa(void) { return nic.dma.pa + E82557_RFD_OFF + nic.rx.dev * E82557_RFD_STRIDE; }

/* IF=0, RUNNING only. Preserve the completion barrier before reusing DMA. */
static void tx_complete(void)
{
    if (nic.state != E82557_RUNNING) return;
    if (nic.tx == TX_ISSUED) {
        u16 st = e82557_rd16((u8 *)nic.dma.va + E82557_TCB_OFF);
        if (st & E82557_CB_C) {
            E82557_BARRIER(); /* TX DMA completion */
            nic.tx = TX_IDLE;
            /* SDM :4648-4663: C implies OK, and means DMA done.
             * !OK is defensive handling of an anomalous value. */
            nic.stats[(st & E82557_CB_OK) ? E82557_ST_TX_DMA_DONE : E82557_ST_TX_FAIL]++;
        }
    }
}

/* IF=0. FreeBSD if_fxp.c:1706-1724: acknowledge exactly the observed byte.
 * Two passes only; tick drains remaining events even if the PIC loses an edge. */
static int e82557_irq(unsigned int irq, void *arg)
{
    int handled = IRQ_NONE;
    (void)irq; (void)arg;
    if (nic.state == E82557_OFF || nic.state == E82557_STARTING ||
        nic.state == E82557_STOPPING) return IRQ_NONE;
    for (u32 pass = 0; pass < 2; pass++) {
        u16 st = inpw(nic.base + E82557_SCB_STATUS);
        u8 ack = (u8)(st >> E82557_STAT_SHIFT);
        if (ack == 0xff) { nic.stats[E82557_ST_OTHER]++; return IRQ_NONE; }
        if (!ack) break;
        outp(nic.base + E82557_SCB_ACK, ack);
        handled = IRQ_HANDLED;
        if (st & (E82557_STAT_CX | E82557_STAT_CNA)) nic.stats[E82557_ST_CX]++;
        if (st & E82557_STAT_FR) nic.stats[E82557_ST_FR]++;
        if (st & E82557_STAT_RNR) nic.stats[E82557_ST_RNR]++;
        if (st & E82557_STAT_SWI) nic.stats[E82557_ST_SWI]++;
        if (st & E82557_STAT_MDI) nic.stats[E82557_ST_OTHER]++;
        if (nic.state == E82557_RUNNING) {
            if (st & (E82557_STAT_CX | E82557_STAT_CNA)) tx_complete();
            if (st & E82557_STAT_RNR) nic.rnr_pending = 1;
        }
    }
    return handled;
}
static void failed_dma(void)
{
    nic.state = E82557_FAILED;
    scb_mask(nic.base, 1);
    (void)e82557_irq(0, NULL); /* drain only; no flags in FAILED */
    bm_restore(&nic.dev, 1);
    (void)dma_mark_leaked(&nic.dma); /* retained until reboot, never reused */
}
static void recover_begin(void)
{
    if (nic.tx == TX_RESERVED) { nic.recover_pending = 1; return; }
    nic.recover_pending = 0;
    nic.gen++;
    nic.stats[E82557_ST_RECOVER_TOTAL]++;
    if (nic.stats[E82557_ST_RECOVER_TOTAL] > E82557_RECOVER_MAX) { failed_dma(); return; }
    nic.state = E82557_RECOVER;
    scb_mask(nic.base, 1);
    nic.stop_tries = 0;
    nic.recover_step = E82557_RS_STOP;
    nic.reset_begin = nic.stop.reset_seq;
}
/* IF=0. Unlike fxp's continuous consume/restart (if_fxp.c:1994-1998),
 * foreground consumption is separate here. IF=0 does not stop DMA: read RUS,
 * then scan again after RU has stopped, before deciding count and pointer. */
static int ru_restart(void)
{
    u16 st = inpw(nic.base + E82557_SCB_STATUS);
    u32 rus = (st & E82557_RUS_MASK) >> E82557_RUS_SHIFT;
    int after;
    if (rus == E82557_RUS_READY) { nic.rnr_pending = 0; return ISSUE_OK; }
    if ((st >> E82557_STAT_SHIFT) == 0xff || rus > E82557_RUS_NORES) return ISSUE_TIMEOUT;
    rx_scan(); /* stopped RU: settle DMA completions since the earlier scan */
    if (nic.rx.ready == E82557_RFD_COUNT) return ISSUE_OK;
    E82557_BARRIER(); /* publish recycled descriptors before RU Start */
    nic.stop.start_seq = nic.stop.reset_seq;
    int rc = scb_issue(nic.base, 0, E82557_RU_START, rx_pa(), 1, &after);
    if (rc == ISSUE_OK) nic.rnr_pending = 0;
    return rc;
}
void e82557_tick(void)
{
    if (nic.state == E82557_RUNNING) {
        if (nic.recover_pending && nic.tx != TX_RESERVED) {
            recover_begin();
        } else {
            tx_complete();
            rx_scan();
            if (ru_restart() != ISSUE_OK) recover_begin();
            if (nic.state == E82557_RUNNING && nic.tx == TX_ISSUED &&
                (u32)(tick_count - nic.tx_issued) > E82557_TX_WAIT_TICKS)
                recover_begin();
            /* SWI proof belongs to IRQ3: tick may read status, but must not ack. */
            if (!nic.swi_wait) (void)e82557_irq(0, NULL);
            return; /* never issue a second SCB command in this tick */
        }
    }
    if (nic.state == E82557_RX_WAIT) {
        if (nic.rx.ready == E82557_RFD_COUNT) return;
        nic.state = E82557_RECOVER;
        nic.recover_step = E82557_RS_RESTART;
    }
    if (nic.state != E82557_RECOVER) return;
    switch (nic.recover_step) {
    case E82557_RS_STOP:
        if (nic.stop.reset_seq - nic.reset_begin >= E82557_RECOVER_RESETS) {
            failed_dma(); return;
        }
        if (!stop_dma(nic.base, &nic.stop, 1)) {
            if (++nic.stop_tries >= E82557_STOP_ATTEMPTS) failed_dma();
            return; /* retry R1 on the next tick, never twice in one tick */
        }
        nic.stop_tries = 0;
        rx_scan(); /* R1 final scan: reset has now stopped all DMA writes */
        nic.recover_step = E82557_RS_REOWN;
        break;
    case E82557_RS_REOWN:
        if (nic.tx == TX_ISSUED) { nic.tx = TX_IDLE; nic.stats[E82557_ST_TX_FAIL]++; }
        e82557_rx_reown(rx_ring(), &nic.rx); /* only rx_free, preserve C=1 */
        nic.recover_step = E82557_RS_RESTART;
        break;
    case E82557_RS_RESTART:
        if (nic.rx.ready == E82557_RFD_COUNT) { nic.state = E82557_RX_WAIT; break; }
        if (ru_restart() == ISSUE_OK) {
            /* Drain while still RECOVER and masked, before exposing INTA. */
            if (!nic.swi_wait) (void)e82557_irq(0, NULL);
            nic.state = E82557_RUNNING;
            scb_mask(nic.base, 0);
            nic.stats[E82557_ST_RECOVER_OK]++;
            return;
        } else if (nic.stop.reset_seq - nic.reset_begin >= E82557_RECOVER_RESETS) {
            failed_dma();
        } else nic.recover_step = E82557_RS_STOP;
        break;
    }
    /* Reset has M=1; also drain events that arrived before R1. */
    if (nic.state != E82557_FAILED && !nic.swi_wait) (void)e82557_irq(0, NULL);
}
static int packet_unavailable(void)
{
    return nic.state == E82557_RECOVER || nic.state == E82557_RX_WAIT ?
           OS32_ERR_AGAIN : OS32_ERR_NOSYS;
}
int e82557_send(const void *buf, u32 len)
{
    u32 gen;
    int after, rc;
    unsigned int flags = irq_save();
    if (nic.state != E82557_RUNNING) { rc = packet_unavailable(); goto out; }
    if (!buf || len < ETH_HLEN || len > E82557_FRAME_MAX) {
        rc = OS32_ERR_INVAL; goto out;
    }
    if (nic.tx == TX_ISSUED) tx_complete();
    if (nic.tx != TX_IDLE || nic.recover_pending) { rc = OS32_ERR_AGAIN; goto out; }
    nic.tx = TX_RESERVED;
    nic.tx_issued = 0;
    gen = nic.gen;
    irq_restore(flags);
    (void)e82557_tcb_build((u8 *)nic.dma.va + E82557_TCB_OFF, buf, len,
                           E82557_TX_THRESH, E82557_TBD_NONE, 1);
    E82557_HOST_YIELD();
    E82557_BARRIER(); /* send publication */
    flags = irq_save();
    if (nic.state != E82557_RUNNING || nic.gen != gen || nic.tx != TX_RESERVED) {
        /* Never clear a reservation belonging to a different generation. */
        if (nic.gen == gen && nic.tx == TX_RESERVED) nic.tx = TX_IDLE;
        rc = OS32_ERR_AGAIN; goto out;
    }
    nic.stop.start_seq = nic.stop.reset_seq;
    rc = scb_issue(nic.base, E82557_CU_START, 0, nic.dma.pa + E82557_TCB_OFF, 1, &after);
    if (rc != ISSUE_OK) {
        nic.tx = TX_IDLE;
        nic.recover_pending = 1; /* stop before any subsequent use of the TCB */
        rc = OS32_ERR_AGAIN;
    } else {
        nic.tx = TX_ISSUED;
        nic.tx_issued = tick_count;
        rc = 0;
    }
 out:
    irq_restore(flags);
    return rc;
}
int e82557_recv(void *buf, u32 cap, u32 *len)
{
    unsigned int flags = irq_save();
    int rc;
    if (len) *len = 0;
    if (nic.state != E82557_RUNNING && nic.state != E82557_RX_WAIT) {
        rc = packet_unavailable(); goto out;
    }
    if (!buf || !len) { rc = OS32_ERR_INVAL; goto out; }
    rx_scan();
    if (!nic.rx.ready) { rc = OS32_ERR_AGAIN; goto out; }
    u8 *rfd = e82557_rfd(rx_ring(), nic.rx.cons);
    irq_restore(flags);
    u16 st = e82557_rd16(rfd);
    E82557_BARRIER(); /* recv completion acquisition */
    u32 count = e82557_rd16(rfd + E82557_DESC_COUNT) & E82557_RFD_COUNT_MASK;
    int drop = !(st & E82557_RFD_OK) || count < ETH_HLEN || count > E82557_FRAME_MAX;
    if (!drop && count > cap) return OS32_ERR_NOSPC;
    if (!drop) {
        E82557_DESC_ACCESS(rfd + E82557_RFD_HEADER, count, 0);
        for (u32 i = 0; i < count; i++) ((u8 *)buf)[i] = rfd[E82557_RFD_HEADER + i];
        *len = count;
    }
    E82557_HOST_YIELD(); /* completed RFD stays CPU-owned through any recovery */
    flags = irq_save();
    /* FAILED DMA is deliberately retained. Do not publish more buffers to it. */
    if (nic.state != E82557_FAILED)
        e82557_rfd_recycle(rx_ring(), &nic.rx);
    nic.stats[drop ? E82557_ST_RX_DROP : E82557_ST_RX_OK]++;
    rc = drop ? OS32_ERR_AGAIN : 0;
 out:
    irq_restore(flags);
    return rc;
}
void e82557_get_mac(u8 *mac)
{
    if (mac) for (u32 i = 0; i < E82557_MAC_LEN; i++) mac[i] = nic.mac[i];
}
int e82557_state(void) { return nic.state; }
int e82557_rx_ready(void)
{
    unsigned int flags = irq_save();
    int ready = nic.state == E82557_RUNNING &&
        ((inpw(nic.base + E82557_SCB_STATUS) & E82557_RUS_MASK) >> E82557_RUS_SHIFT) ==
        E82557_RUS_READY;
    irq_restore(flags);
    return ready;
}
u32 e82557_rx_free(void)
{
    unsigned int flags = irq_save();
    u32 free = 0;
    if (nic.state == E82557_RUNNING || nic.state == E82557_RX_WAIT) {
        rx_scan(); free = E82557_RFD_COUNT - nic.rx.ready;
    }
    irq_restore(flags);
    return free;
}
int e82557_loopback(int on) { (void)on; return OS32_ERR_NOSYS; }
u32 e82557_stat(u32 idx) { return idx < E82557_ST_COUNT ? nic.stats[idx] : 0; }

static int probe_ru(void)
{
    unsigned int flags = irq_save();
    int after;
    E82557_BARRIER(); /* probe RU publication */
    nic.stop.start_seq = nic.stop.reset_seq;
    int rc = scb_issue(nic.base, 0, E82557_RU_START, rx_pa(), 1, &after);
    irq_restore(flags);
    return rc;
}
static int e82557_probe(const struct pci_dev *dev)
{
    const char *why, *stage = "win";
    u16 base, status = 0, compat = 0, lb_len = 0;
    u8 cfg[E82557_CFG_LEN], frame[E82557_HELLO_LEN];
    struct phy_observation phy = { 0 };
    int allocated = 0, registered = 0, changed_bm = 0, width = 0, dummy;
    int reason, after = 0, issued, result = PCI_PROBE_DECLINE;
    unsigned int flags;
    nic.state = E82557_STARTING;
    why = try_begin();
    if (why) {
        if (kstrncmp(why, "seq-", 4) == 0) kprintf(E82557_LOG_ATTR, "[e82557] trace=off\n");
        kprintf(E82557_LOG_ATTR, "[e82557] skip(%s)\n[e82557] LB3=FAIL(try:%s)\n", why, why);
        nic.state = E82557_OFF;
        return result;
    }
    nic_trace(E82557_TRACE_TRY);
    nic_trace(E82557_TRACE_WIN);
    if (!e82557_io_window(dev, &base, &reason)) { why = e82557_window_name(reason); goto done; }
    nic.base = base; nic.dev = *dev;
    stage = "rst";
    nic_trace(E82557_TRACE_RST);
    outpd(base + E82557_PORT, E82557_PORT_SOFTWARE);
    cpu_delay_us(E82557_RESET_US);
    outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);
    cpu_delay_us(E82557_RESET_US);
    scb_mask(base, 1);
    status = inpw(base + E82557_SCB_STATUS);
    outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> E82557_STAT_SHIFT));
    status = inpw(base + E82557_SCB_STATUS);
    if (status & (E82557_CUS_MASK | E82557_RUS_MASK)) {
        pci_bind_set_reason(PCI_BIND_RESET_FAILED);
        result = PCI_PROBE_QUARANTINE; why = "idle"; goto done;
    }
    stage = "base";
    nic_trace(E82557_TRACE_BASE);
    flags = irq_save();
    issued = scb_issue(base, E82557_CU_BASE, 0, 0, 1, &after);
    if (issued == ISSUE_OK) issued = scb_issue(base, 0, E82557_RU_BASE, 0, 1, &after);
    irq_restore(flags);
    if (issued != ISSUE_OK) {
        status = inpw(base + E82557_SCB_STATUS);
        kprintf(E82557_LOG_ATTR, "[e82557] rst=ok base=timeout scb=%04x\n", status);
        why = after ? "post" : "pre"; goto done;
    }
    stage = "ee";
    nic_trace(E82557_TRACE_EEPROM);
    if (!ee_mac_read(base, nic.mac, &width)) { why = "mac"; goto done; }
    struct e82557_ee_pins pins = { &base, ee_write, ee_read, ee_delay, E82557_EE_STEP_US };
    /* FreeBSD if_fxp.c:537-544: report RX lockup compatibility word, no workaround yet. */
    if (e82557_ee_read_word(&pins, E82557_EE_FIXED, &width,
                          E82557_EE_COMPAT, &compat, &dummy) != E82557_OK || dummy) {
        why = "compat"; goto done;
    }
    stage = "dma";
    nic_trace(E82557_TRACE_DMA);
    if (dma_alloc(E82557_DMA_SIZE, E82557_DMA_ALIGN, E82557_DMA_LIMIT, &nic.dma) < 0) {
        why = "alloc"; goto done;
    }
    allocated = 1;
    stage = "irq";
    nic_trace(E82557_TRACE_IRQ);
    if (irq_register(dev->irq_line, e82557_irq, NULL, IRQ_F_SHARED) < 0) { why = "register"; goto done; }
    registered = 1;
    u16 command = (u16)pci_cfg_read32(dev->bus, dev->dev, dev->fn, PCI_CFG_COMMAND);
    if (!(command & E82557_COMMAND_BM)) {
        if (pci_cfg_write16(dev->bus, dev->dev, dev->fn, PCI_CFG_COMMAND,
                            command | E82557_COMMAND_BM) < 0) { why = "bm"; goto done; }
        changed_bm = 1;
    }
    stage = "phy";
    nic_trace(E82557_TRACE_PHY);
    phy_prepare(base, &phy, width);
    phy_vendor(base, &phy);
    nic_trace(E82557_TRACE_LINK);
    phy_link_wait(base, &phy);
    phy_report(&phy, compat);
    if (phy.error) { why = phy.error; goto done; }
    stage = "cfg";
    nic_trace(E82557_TRACE_CFG);
    e82557_config_bytes(cfg, E82557_CFG_LOOPBACK);
    e82557_cb_config(nic.dma.va, cfg);
    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);
    if (why) goto done;
    stage = "ia";
    nic_trace(E82557_TRACE_IA);
    e82557_cb_ia(nic.dma.va, nic.mac);
    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);
    if (why) goto done;
    stage = "ring";
    nic_trace(E82557_TRACE_RING);
    e82557_rx_init(rx_ring(), nic.dma.pa + E82557_RFD_OFF, &nic.rx);
    if (probe_ru() != ISSUE_OK) { why = "timeout"; goto done; }
    stage = "lb";
    nic_trace(E82557_TRACE_LB);
    hello_build(frame, nic.mac);
    (void)e82557_tcb_build((u8 *)nic.dma.va + E82557_TCB_OFF, frame,
                           E82557_HELLO_LEN, E82557_TX_THRESH, E82557_TBD_NONE, 0);
    why = cb_run(base, &nic.dma, E82557_TCB_OFF, &nic.stop);
    if (why) {
        if (kstrcmp(why, "ng") == 0) why = "txng";
        goto done;
    }
    for (u32 ms = 0; !(e82557_rd16(rx_ring()) & E82557_RFD_C); ms++) {
        if (ms == E82557_LB_WAIT_MS) { why = "timeout"; goto done; }
        cpu_delay_us(E82557_CB_POLL_US);
    }
    E82557_BARRIER(); /* loopback completion acquisition */
    stage = "cmp";
    nic_trace(E82557_TRACE_CMP);
    lb_len = e82557_rd16(rx_ring() + E82557_DESC_COUNT) & E82557_RFD_COUNT_MASK;
    if (!(e82557_rd16(rx_ring()) & E82557_RFD_OK) || lb_len != E82557_HELLO_LEN) {
        why = "ng"; goto done;
    }
    E82557_BARRIER();
    E82557_DESC_ACCESS(rx_ring() + E82557_RFD_HEADER, lb_len, 0);
    for (u32 i = 0; i < lb_len; i++) if (rx_ring()[E82557_RFD_HEADER + i] != frame[i]) {
        why = "ng"; goto done;
    }
    stage = "rst2";
    nic_trace(E82557_TRACE_RST2);
    flags = irq_save();
    int stopped = stop_dma(base, &nic.stop, E82557_STOP_ATTEMPTS);
    irq_restore(flags);
    if (!stopped) { why = "idle"; goto done; }
    stage = "cfg2";
    nic_trace(E82557_TRACE_CFG2);
    e82557_config_bytes(cfg, E82557_CFG_NORMAL);
    e82557_cb_config(nic.dma.va, cfg);
    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);
    if (why) goto done;
    stage = "ia2";
    nic_trace(E82557_TRACE_IA2);
    e82557_cb_ia(nic.dma.va, nic.mac);
    why = cb_run(base, &nic.dma, E82557_CB_OFF, &nic.stop);
    if (why) goto done;
    stage = "ru";
    nic_trace(E82557_TRACE_RU);
    nic.rx.cons = nic.rx.dev = 1; nic.rx.ready = 0;
    e82557_rx_reown(rx_ring(), &nic.rx);
    if (probe_ru() != ISSUE_OK) { why = "timeout"; goto done; }
    flags = irq_save();
    nic.state = E82557_RUNNING;
    nic.swi_wait = 1;
    scb_mask(base, 0);
    irq_restore(flags);
    stage = "swi";
    nic_trace(E82557_TRACE_SWI);
    outp(base + E82557_SCB_INTCTL, E82557_SCB_SI);
    for (u32 us = 0; !nic.stats[E82557_ST_SWI] && us < E82557_SWI_WAIT_MS * 1000; us += E82557_SWI_POLL_US)
        cpu_delay_us(E82557_SWI_POLL_US);
    flags = irq_save();
    nic.swi_wait = 0;
    irq_restore(flags);
    if (!nic.stats[E82557_ST_SWI]) why = "irq";
    nic_trace(E82557_TRACE_BOUND);
    kprintf(E82557_LOG_ATTR, "[e82557] lb=ok len=%u cmp=ok rst2=ok cfg2=ok ia2=ok ru=ok irq=%s bound irq=%u\n",
            lb_len, why ? "ng" : "ok(swi)", dev->irq_line);
    result = PCI_PROBE_OK;
 done:
    if (result != PCI_PROBE_OK) {
        nic.state = E82557_STOPPING;
        nic_trace(E82557_TRACE_STOP);
        if (allocated) {
            flags = irq_save();
            int stopped = stop_dma(base, &nic.stop, E82557_STOP_ATTEMPTS);
            status = inpw(base + E82557_SCB_STATUS);
            outp(base + E82557_SCB_ACK, (u8)(status >> E82557_STAT_SHIFT));
            irq_restore(flags);
            if (stopped) {
                if (registered) (void)irq_unregister(dev->irq_line, e82557_irq, NULL);
                bm_restore(dev, changed_bm);
                (void)dma_free(&nic.dma);
            } else {
                bm_restore(dev, 1);
                (void)dma_mark_leaked(&nic.dma);
                pci_bind_set_reason(PCI_BIND_RESET_FAILED);
                result = PCI_PROBE_QUARANTINE;
                kprintf(E82557_LOG_ATTR, "[e82557] stop=fail leaked QUARANTINE\n");
            }
        }
        nic.state = result == PCI_PROBE_QUARANTINE ? E82557_QUARANTINED : E82557_OFF;
        kprintf(E82557_LOG_ATTR, "[e82557] %s=%s\n", stage, why ? why : "ng");
    }
    if (vfs_rm(E82557_TRY_PATH) < 0 || vfs_sync() < 0) kprintf(E82557_LOG_ATTR, "[e82557] try-rm=fail\n");
    nic_trace(E82557_TRACE_DONE);
    if (why) kprintf(E82557_LOG_ATTR, "[e82557] LB3=FAIL(%s:%s)\n", stage, why);
    else kprintf(E82557_LOG_ATTR, "[e82557] LB3=PASS\n");
    return result;
}
const struct pci_driver e82557_driver = {
    sizeof(struct pci_driver), E82557_VENDOR, E82557_DEVICE,
    PCI_CLASS_NETWORK, PCI_SUB_NET_ETHERNET, "e82557", e82557_probe
};

/* Boot rollback only. Reuse LB's selective-reset evidence before detaching
 * IRQ/tick; free the pool buffer only after both callbacks are detached. */
int lan_module_quiet(void)
{
    if (nic.state == E82557_OFF) return 1;
    if (nic.state == E82557_QUARANTINED || nic.state == E82557_FAILED) return 0;
    unsigned int flags = irq_save();
    nic.state = E82557_STOPPING;
    int stopped = stop_dma(nic.base, &nic.stop, E82557_STOP_ATTEMPTS);
    if (stopped) {
        u16 status = inpw(nic.base + E82557_SCB_STATUS);
        outp(nic.base + E82557_SCB_ACK, (u8)(status >> E82557_STAT_SHIFT));
    }
    irq_restore(flags);
    if (!stopped) {
        bm_restore(&nic.dev, 1);
        (void)dma_mark_leaked(&nic.dma);
        nic.state = E82557_QUARANTINED;
    }
    return stopped;
}

int lan_module_release(void)
{
    if (nic.state == E82557_OFF) return 1;
    if (nic.state != E82557_STOPPING) return 0;
    bm_restore(&nic.dev, !(nic.dev.command & E82557_COMMAND_BM));
    if (dma_free(&nic.dma)) return 0;
    nic.state = E82557_OFF;
    return 1;
}
