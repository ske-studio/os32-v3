/* LB-2c: DP83840 PCR initialization, one HELLO; stop then DECLINE. */
#include "e82557.h"
#include "e82557_eeprom.h"
#include "e82557_desc.h"
#include "kernel/dma_pool.h"
#ifndef E82557_HOST_TEST
#include "io.h"
#include "kernel/idt.h"
#include "kernel/cpu_calibrate.h"
#include "kernel/sysconfig.h"
#include "kernel/module.h"
#include "fs/vfs.h"
#include "bootinfo.h"
#include "config.h"
#include "kprintf.h"
#include "kstring.h"
#endif

#define E82557_VAR_DIR "/var"
#define E82557_LOG_DIR "/var/log"
#define E82557_CRC_DIGITS 8
#define E82557_SEQ_MAX 0xFFFFFFFFu

enum try_result { TRY_NONE, TRY_SAME_CRC, TRY_OTHER_CRC, TRY_READ_FAIL };
enum issue_result { ISSUE_OK, ISSUE_TIMEOUT };

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
    if (kdebug.ops.nic_trace) kdebug.ops.nic_trace(stage);
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
    if (!scb_wait(base)) return ISSUE_TIMEOUT;
    if (need_ptr) outpd(base + E82557_SCB_POINTER, ptr);
    outp(base + E82557_SCB_COMMAND, e82557_scb_cmd(cuc, ruc));
    *after = 1;
    return scb_wait(base) ? ISSUE_OK : ISSUE_TIMEOUT;
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

static int ee_mac_read(u16 base, u8 *mac)
{
    struct e82557_ee_pins pins = { &base, ee_write, ee_read, ee_delay, E82557_EE_STEP_US };
    u16 words[3], ignored;
    int attempt, addr, dummy, width = E82557_EE_WIDTH6;
    for (attempt = 0; attempt < 2; attempt++) {
        if (attempt && e82557_ee_read_word(&pins, E82557_EE_AUTO,
                    &width, 0, &ignored, &dummy) != E82557_OK) return 0;
        for (addr = 0; addr < 3; addr++) {
            if (e82557_ee_read_word(&pins, E82557_EE_FIXED, &width,
                    (u16)addr, &words[addr], &dummy) != E82557_OK || dummy != 0)
                break;
        }
        if (addr != 3) continue;
        e82557_ee_mac(words, mac);
        if (e82557_mac_form(mac) == E82557_OK) return 1;
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

static int stop_dma(u16 base, struct e82557_stop *stop)
{
    u32 before = stop->reset_seq;
    for (u32 attempt = 0; attempt < E82557_STOP_ATTEMPTS; attempt++) {
        outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);
        stop->reset_seq++;
        cpu_delay_us(E82557_RESET_US);
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
        frame[E82557_FRAME_MIN + E82557_HELLO_MARK_LEN + i] = mac[i];
    }
    frame[E82557_FRAME_MIN - 2] = E82557_HELLO_TYPE >> 8;
    frame[E82557_FRAME_MIN - 1] = E82557_HELLO_TYPE & 0xff;
    for (u32 i = 0; i < E82557_HELLO_MARK_LEN; i++)
        frame[E82557_FRAME_MIN + i] = (u8)mark[i];
}

struct phy_observation {
    u8 addr;
    u16 bmcr, bmsr_first, bmsr, anlpar;
    int nsc;
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
static void phy_prepare(u16 base, struct phy_observation *p)
{
    struct e82557_ee_pins pins = { &base, ee_write, ee_read, ee_delay, E82557_EE_STEP_US };
    u16 word = 0, id1 = 0, id2 = 0, anar = 0, old, value = 0;
    int width = E82557_EE_WIDTH6, dummy, scan = 0, fix;
    const char *fix_name = "none";
    if (e82557_ee_read_word(&pins, E82557_EE_FIXED, &width,
                          E82557_EE_PHY, &word, &dummy) != E82557_OK || dummy)
        word = 0; /* Still discover by ID if the EEPROM hint is unavailable. */
    p->addr = word & E82557_PHY_MASK;
    if (!mdi_read(base, p, E82557_PHY_ID1, &id1) ||
        !mdi_read(base, p, E82557_PHY_ID2, &id2)) goto unavailable;
    if (!e82557_phy_id_valid(id1)) {
        scan = 1;
        for (p->addr = 0; p->addr <= E82557_PHY_MASK; p->addr++) {
            if (!mdi_read(base, p, E82557_PHY_ID1, &id1)) goto unavailable;
            if (e82557_phy_id_valid(id1)) break;
        }
        if (p->addr > E82557_PHY_MASK) {
            p->error = "none";
            kprintf(E82557_LOG_ATTR, "[e82557] phy=none\n");
            goto no_fix;
        }
        if (!mdi_read(base, p, E82557_PHY_ID2, &id2)) goto unavailable;
    }
    p->nsc = id1 == E82557_NSC_ID1 &&
             (id2 & E82557_NSC_ID2_MASK) == E82557_NSC_ID2;
    if (!mdi_read(base, p, E82557_PHY_BMCR, &p->bmcr) || !phy_status(base, p) ||
        !mdi_read(base, p, E82557_PHY_ANAR, &anar) ||
        !mdi_read(base, p, E82557_PHY_ANLPAR, &p->anlpar)) goto unavailable;
    kprintf(E82557_LOG_ATTR,
            "[e82557] phy=%s%u id=%04x:%04x bmcr=%04x bmsr=%04x anar=%04x anlpar=%04x\n",
            scan ? "scan:" : "", p->addr, id1, id2, p->bmcr, p->bmsr, anar, p->anlpar);
    if (!sysconfig_get_int(SYS_SYSTEM_CFG, "E82557_PHYFIX", 1)) {
        fix_name = "off";
    } else {
        u32 start = tick_count;
        while (p->bmcr & E82557_BMCR_RESET) {
            u32 elapsed = tick_count - start;
            if (elapsed >= E82557_PHY_RESET_WAIT_TICKS) break;
            u32 next = elapsed + E82557_PHY_POLL_TICKS;
            tick_wait(start, next < E82557_PHY_RESET_WAIT_TICKS ? next : E82557_PHY_RESET_WAIT_TICKS);
            if (!mdi_read(base, p, E82557_PHY_BMCR, &p->bmcr)) goto no_fix;
        }
        old = p->bmcr;
        fix = e82557_bmcr_fix(old, &value);
        if (fix == E82557_FIX_RESET_BUSY) fix_name = "skip(reset)";
        if (fix == E82557_FIX_WRITE) {
            u16 written = value;
            if (!mdi_xfer(base, E82557_MDI_WRITE, p->addr, E82557_PHY_BMCR, &value)) {
                p->error = "timeout";
                goto no_fix;
            }
            if (!mdi_read(base, p, E82557_PHY_BMCR, &p->bmcr) || !phy_status(base, p))
                goto no_fix;
            if (e82557_bmcr_fixed(p->bmcr))
                kprintf(E82557_LOG_ATTR, "[e82557] fix=bmcr:%04x->%04x rb=%04x bmsr=%04x\n",
                        old, written, p->bmcr, p->bmsr);
            else
                kprintf(E82557_LOG_ATTR, "[e82557] fix=ng rb=%04x bmsr=%04x\n", p->bmcr, p->bmsr);
            return;
        }
    }
    kprintf(E82557_LOG_ATTR, "[e82557] fix=%s rb=%04x bmsr=%04x\n", fix_name, p->bmcr, p->bmsr);
    return;
unavailable:
    kprintf(E82557_LOG_ATTR, "[e82557] phy=%u mdi=timeout\n", p->addr);
no_fix:
    kprintf(E82557_LOG_ATTR, "[e82557] fix=- rb=- bmsr=-\n");
}
/* FreeBSD main sys/dev/mii/nsphy.c:145-179 (fxp): preserve every other bit.
 * Reserved bits 10/8 are written but only documented R/W bits are verified. */
static void phy_vendor(u16 base, struct phy_observation *p)
{
    u16 old, value, rb, par, srr;
    const char *note = "";
    if (!p->nsc) {
        kprintf(E82557_LOG_ATTR, "[e82557] nsc pcr=skip(id)\n");
        return;
    }
    if (!mdi_read(base, p, E82557_NSC_PCR, &old)) goto unavailable;
    value = old;
    rb = old;
    if (!sysconfig_get_int(SYS_SYSTEM_CFG, "E82557_PHYVENDOR", 1)) {
        note = " pcr=off";
    } else {
        value = old | E82557_NSC_PCR_SET;
        if (value != old) {
            u16 write = value;
            if (!mdi_xfer(base, E82557_MDI_WRITE, p->addr, E82557_NSC_PCR, &write)) {
                p->error = "timeout";
                goto unavailable;
            }
            if (!mdi_read(base, p, E82557_NSC_PCR, &rb)) goto unavailable;
        }
        if ((rb & E82557_NSC_PCR_VERIFY) != (value & E82557_NSC_PCR_VERIFY))
            note = " pcr=ng";
    }
    if (!mdi_read(base, p, E82557_NSC_PAR, &par) ||
        !mdi_read(base, p, E82557_NSC_SRR, &srr)) goto unavailable;
    kprintf(E82557_LOG_ATTR, "[e82557] nsc pcr=%04x->%04x rb=%04x par=%04x srr=%04x%s\n",
            old, value, rb, par, srr, note);
    return;
unavailable:
    kprintf(E82557_LOG_ATTR, "[e82557] nsc pcr=- mdi=timeout\n");
}

static void phy_link_wait(u16 base, struct phy_observation *p)
{
    u32 start = tick_count, elapsed = 0;
    int link = 0, an_done = 0, an_en = 0;
    u16 par = 0xffff; /* No vendor register access on another PHY. */
    while (phy_status(base, p)) {
        elapsed = tick_count - start;
        e82557_phy_decode(p->bmcr, p->bmsr, &link, &an_done, &an_en);
        if (e82557_link_wait_step(link, an_done, an_en, elapsed,
                                  E82557_LINK_WAIT_TICKS) != E82557_WAIT) break;
        u32 next = elapsed + E82557_PHY_POLL_TICKS;
        tick_wait(start, next < E82557_LINK_WAIT_TICKS ? next : E82557_LINK_WAIT_TICKS);
    }
    if (p->error || (p->nsc && !mdi_read(base, p, E82557_NSC_PAR, &par))) {
        kprintf(E82557_LOG_ATTR, "[e82557] link=- an=- wait=- bmsr=-/- par=-\n");
        return;
    }
    kprintf(E82557_LOG_ATTR, "[e82557] link=%s an=%s wait=%u bmsr=%04x/%04x par=%04x\n",
            link ? "up" : "down", an_done ? "done" : "no",
            elapsed * E82557_TICK_MS, p->bmsr_first, p->bmsr, par);
}

/* No reissue after an incomplete Dump: the device can still own this area. */
static const char *stats_until(u16 base, struct dma_buf *dma, u32 expect,
                              u32 *good, u32 *waited)
{
    u8 *area = (u8 *)dma->va + E82557_STAT_OFF;
    u32 start = tick_count;
    for (;;) {
        int after, issued;
        unsigned int flags;
        e82557_wr32(area + E82557_STAT_DONE_OFF, 0);
        E82557_BARRIER(); /* stats publish */
        flags = irq_save();
        issued = scb_issue(base, E82557_CU_STAT_DUMP, 0, 0, 0, &after);
        irq_restore(flags);
        if (issued != ISSUE_OK) return after ? "post" : "pre";
        for (u32 ms = 0; ; ms++) {
            if (e82557_stats_done(area)) break;
            if (ms == E82557_CB_WAIT_MS ||
                (u32)(tick_count - start) * E82557_TICK_MS >= E82557_TXDONE_WAIT_MS)
                return "timeout";
            cpu_delay_us(E82557_CB_POLL_US);
        }
        E82557_BARRIER(); /* stats acquire */
        *good = e82557_stats_read(area, E82557_STAT_GOOD);
        *waited = (tick_count - start) * E82557_TICK_MS;
        if (e82557_stats_step(*good, expect, *waited, E82557_TXDONE_WAIT_MS) != E82557_WAIT)
            return NULL;
        tick_wait(tick_count, E82557_TXDONE_POLL_TICKS);
        *waited = (tick_count - start) * E82557_TICK_MS;
        if (*waited >= E82557_TXDONE_WAIT_MS) return NULL;
    }
}

static int e82557_probe(const struct pci_dev *dev)
{
    const char *why = try_begin();
    const char *stage = "win";
    u16 base, status = 0;
    u8 mac[E82557_MAC_LEN], cfg[E82557_CFG_LEN], frame[E82557_HELLO_LEN];
    struct dma_buf dma;
    struct e82557_stop stop = { 0, 0 };
    struct phy_observation phy = { 0 };
    u32 good = 0, waited = 0;
    int observed = 0, stats_valid = 0;
    const char *stat_error = NULL;
    int allocated = 0, changed_bm = 0, rc;
    u16 command, tx_len = 0;
    unsigned int flags;
    int reason, after = 0, issued, result = PCI_PROBE_DECLINE;
    if (why) {
        if (kstrncmp(why, "seq-", 4) == 0)
            kprintf(E82557_LOG_ATTR, "[e82557] trace=off\n");
        kprintf(E82557_LOG_ATTR, "[e82557] skip(%s)\n", why);
        kprintf(E82557_LOG_ATTR, "[e82557] LB2c=FAIL(try:%s)\n", why);
        return result;
    }
    nic_trace(E82557_TRACE_TRY);
    nic_trace(E82557_TRACE_WIN);
    if (!e82557_io_window(dev, &base, &reason)) {
        why = e82557_window_name(reason);
        goto done;
    }
    stage = "rst";
    nic_trace(E82557_TRACE_RST);
    outpd(base + E82557_PORT, E82557_PORT_SOFTWARE);
    cpu_delay_us(E82557_RESET_US);
    outpd(base + E82557_PORT, E82557_PORT_SELECTIVE);
    cpu_delay_us(E82557_RESET_US);
    scb_mask(base, 1);
    status = inpw(base + E82557_SCB_STATUS);
    outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> 8));
    status = inpw(base + E82557_SCB_STATUS);
    if (status & (E82557_CUS_MASK | E82557_RUS_MASK)) {
        kprintf(E82557_LOG_ATTR, "[e82557] rst=fail scb=%04x\n", status);
        pci_bind_set_reason(PCI_BIND_RESET_FAILED);
        result = PCI_PROBE_QUARANTINE;
        why = "idle";
        goto done;
    }
    stage = "base";
    nic_trace(E82557_TRACE_BASE);
    flags = irq_save();
    issued = scb_issue(base, E82557_CU_BASE, 0, 0, 1, &after);
    if (issued == ISSUE_OK)
        issued = scb_issue(base, 0, E82557_RU_BASE, 0, 1, &after);
    irq_restore(flags);
    if (issued != ISSUE_OK) {
        status = inpw(base + E82557_SCB_STATUS);
        kprintf(E82557_LOG_ATTR, "[e82557] rst=ok base=timeout scb=%04x\n", status);
        why = after ? "post" : "pre";
        goto done;
    }
    kprintf(E82557_LOG_ATTR, "[e82557] rst=ok base=ok scb=%04x\n", status);
    stage = "ee";
    nic_trace(E82557_TRACE_EEPROM);
    if (!ee_mac_read(base, mac)) { why = "mac"; goto done; }
    stage = "dma";
    nic_trace(E82557_TRACE_DMA);
    rc = dma_alloc(E82557_DMA_SIZE, E82557_DMA_ALIGN, E82557_DMA_LIMIT, &dma);
    if (rc < 0) {
        kprintf(E82557_LOG_ATTR, "[e82557] dma rc=%d\n", rc);
        why = "alloc";
        goto done;
    }
    allocated = 1;
    kprintf(E82557_LOG_ATTR, "[e82557] dma pa=%08x size=%u\n", dma.pa, dma.size);
    command = (u16)pci_cfg_read32(dev->bus, dev->dev, dev->fn, PCI_CFG_COMMAND);
    if (!(command & E82557_COMMAND_BM)) {
        rc = pci_cfg_write16(dev->bus, dev->dev, dev->fn, PCI_CFG_COMMAND,
                             command | E82557_COMMAND_BM);
        if (rc < 0) { why = "bm"; goto done; }
        changed_bm = 1;
    }
    nic_trace(E82557_TRACE_PHY);
    phy_prepare(base, &phy);
    observed = 1;
    phy_vendor(base, &phy);
    nic_trace(E82557_TRACE_LINK);
    phy_link_wait(base, &phy);
    stage = "cfg";
    nic_trace(E82557_TRACE_CFG);
    e82557_config_bytes(cfg, E82557_CFG_NORMAL);
    e82557_cb_config((u8 *)dma.va + E82557_CB_OFF, cfg);
    why = cb_run(base, &dma, E82557_CB_OFF, &stop);
    if (why) goto done;
    stage = "ia";
    nic_trace(E82557_TRACE_IA);
    e82557_cb_ia((u8 *)dma.va + E82557_CB_OFF, mac);
    why = cb_run(base, &dma, E82557_CB_OFF, &stop);
    if (why) goto done;
    stage = "stat";
    nic_trace(E82557_TRACE_STAT);
    flags = irq_save();
    issued = scb_issue(base, E82557_CU_STAT_ADDR, 0,
                       dma.pa + E82557_STAT_OFF, 1, &after);
    irq_restore(flags);
    if (issued != ISSUE_OK) {
        why = stat_error = after ? "post" : "pre";
        goto done;
    }
    stage = "tx";
    nic_trace(E82557_TRACE_TX);
    hello_build(frame, mac);
    (void)e82557_tcb_build((u8 *)dma.va + E82557_TCB_OFF, frame,
                           E82557_HELLO_LEN, E82557_TX_THRESH, E82557_TBD_NONE, 0);
    why = cb_run(base, &dma, E82557_TCB_OFF, &stop);
    if (why) goto done;
    tx_len = e82557_rd16((u8 *)dma.va + E82557_TCB_OFF + 12) & ~E82557_TCB_EOF;
    stage = "stat";
    nic_trace(E82557_TRACE_STAT);
    why = stat_error = stats_until(base, &dma, 1, &good, &waited);
    if (why) goto done;
    stats_valid = 1;
 done:
    if (observed) {
        if (stats_valid) {
            u8 *area = (u8 *)dma.va + E82557_STAT_OFF;
            u16 par = 0xffff, dcr = 0xffff, fcscr = 0xffff, recr = 0xffff;
            kprintf(E82557_LOG_ATTR, "[e82557] tx good=%u lostcrs=%u maxcol=%u\n",
                    good, e82557_stats_read(area, E82557_STAT_LOSTCRS),
                    e82557_stats_read(area, E82557_STAT_MAXCOL));
            kprintf(E82557_LOG_ATTR, "[e82557] tx latecol=%u under=%u defer=%u\n",
                    e82557_stats_read(area, E82557_STAT_LATECOL),
                    e82557_stats_read(area, E82557_STAT_UNDER),
                    e82557_stats_read(area, E82557_STAT_DEFER));
            (void)phy_status(base, &phy);
            if (phy.nsc) {
                (void)mdi_read(base, &phy, E82557_NSC_PAR, &par);
                (void)mdi_read(base, &phy, E82557_NSC_DCR, &dcr);
                (void)mdi_read(base, &phy, E82557_NSC_FCSCR, &fcscr);
                (void)mdi_read(base, &phy, E82557_NSC_RECR, &recr);
            }
            if (phy.error)
                kprintf(E82557_LOG_ATTR, "[e82557] tx bmsr=-/- par=- dcr=- fcscr=- recr=-\n");
            else
                kprintf(E82557_LOG_ATTR, "[e82557] tx bmsr=%04x/%04x par=%04x dcr=%04x fcscr=%04x recr=%04x\n",
                        phy.bmsr_first, phy.bmsr, par, dcr, fcscr, recr);
        } else {
            kprintf(E82557_LOG_ATTR, "[e82557] stat=%s bmsr=-\n", stat_error ? stat_error : "-");
        }
        if (!why && phy.error) { stage = "mdi"; why = phy.error; }
    }
    nic_trace(E82557_TRACE_STOP);
    if (allocated) {
        if (why) kprintf(E82557_LOG_ATTR, "[e82557] %s=%s\n", stage, why);
        if (stop_dma(base, &stop)) {
            scb_mask(base, 1);
            status = inpw(base + E82557_SCB_STATUS);
            outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> 8));
            bm_restore(dev, changed_bm);
            (void)dma_free(&dma);
            if (!why)
                kprintf(E82557_LOG_ATTR, "[e82557] cfg=ok ia=ok tx=ok(%u) stop=ok\n", tx_len);
            else
                kprintf(E82557_LOG_ATTR, "[e82557] stop=ok\n");
        } else {
            scb_mask(base, 1);
            status = inpw(base + E82557_SCB_STATUS);
            outp(base + E82557_SCB_ACK, (u8)((status & E82557_STAT_MASK) >> 8));
            /* Safety shutdown is unconditional; normal restoration is owned. */
            bm_restore(dev, 1);
            (void)dma_mark_leaked(&dma);
            pci_bind_set_reason(PCI_BIND_RESET_FAILED);
            result = PCI_PROBE_QUARANTINE;
            stage = "stop";
            why = "idle";
            kprintf(E82557_LOG_ATTR, "[e82557] stop=fail leaked QUARANTINE\n");
        }
    }
    /* DECLINE/QUARANTINE both returned normally, so permit a later boot. */
    if (vfs_rm(E82557_TRY_PATH) < 0 || vfs_sync() < 0)
        kprintf(E82557_LOG_ATTR, "[e82557] try-rm=fail\n");
    nic_trace(E82557_TRACE_DONE);
    if (why)
        kprintf(E82557_LOG_ATTR, "[e82557] LB2c=FAIL(%s:%s)\n", stage, why);
    else
        kprintf(E82557_LOG_ATTR, "[e82557] LB2c=PASS\n");
    return result;
}

/* S4 ABI reservations; LB-3 will supply actual packet operations. */
int e82557_send(const void *buf, u32 len) { (void)buf; (void)len; return OS32_ERR_NOSYS; }
int e82557_recv(void *buf, u32 cap, u32 *len)
{
    (void)buf; (void)cap;
    if (len) *len = 0;
    return OS32_ERR_NOSYS;
}
void e82557_get_mac(u8 *mac)
{
    int i;
    if (mac) for (i = 0; i < E82557_MAC_LEN; i++) mac[i] = 0;
}
int e82557_state(void) { return 0; }
int e82557_loopback(int on) { (void)on; return OS32_ERR_NOSYS; }
u32 e82557_stat(u32 idx) { (void)idx; return 0; }

const struct pci_driver e82557_driver = {
    sizeof(struct pci_driver), E82557_VENDOR, E82557_DEVICE,
    PCI_CLASS_NETWORK, PCI_SUB_NET_ETHERNET, "e82557", e82557_probe
};
