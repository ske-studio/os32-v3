/* LB-1: reset/Base bring-up. No DMA, IRQ registration or packet traffic. */
#include "e82557.h"
#include "e82557_eeprom.h"
#ifndef E82557_HOST_TEST
#include "io.h"
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
    outp(base + E82557_SCB_COMMAND, (u8)((cuc << 4) | ruc));
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

static int e82557_probe(const struct pci_dev *dev)
{
    const char *why = try_begin();
    const char *stage = "win";
    u16 base, status = 0;
    u8 mac[E82557_MAC_LEN];
    unsigned int flags;
    int reason, after = 0, issued, result = PCI_PROBE_DECLINE;
    if (why) {
        if (kstrncmp(why, "seq-", 4) == 0)
            kprintf(E82557_LOG_ATTR, "[e82557] trace=off\n");
        kprintf(E82557_LOG_ATTR, "[e82557] skip(%s)\n", why);
        kprintf(E82557_LOG_ATTR, "[e82557] LB1=FAIL(try:%s)\n", why);
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
        kprintf(E82557_LOG_ATTR, "[e82557] rst=ok base=timeout scb=%04x\n", status);
        why = after ? "post" : "pre";
        goto done;
    }
    kprintf(E82557_LOG_ATTR, "[e82557] rst=ok base=ok scb=%04x\n", status);
    stage = "ee";
    nic_trace(E82557_TRACE_EEPROM);
    if (!ee_mac_read(base, mac)) why = "mac";
 done:
    nic_trace(E82557_TRACE_STOP);
    /* DECLINE/QUARANTINE both returned normally, so permit a later boot. */
    if (vfs_rm(E82557_TRY_PATH) < 0 || vfs_sync() < 0)
        kprintf(E82557_LOG_ATTR, "[e82557] try-rm=fail\n");
    nic_trace(E82557_TRACE_DONE);
    if (why)
        kprintf(E82557_LOG_ATTR, "[e82557] LB1=FAIL(%s:%s)\n", stage, why);
    else
        kprintf(E82557_LOG_ATTR, "[e82557] LB1=PASS io=0x%04x irq=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
                base, dev->irq_line, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return result;
}

/* S4 ABI reservations; LB-2+ will supply actual packet operations. */
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
