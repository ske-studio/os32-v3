#ifndef OS32_OPN_PROBE_H
#define OS32_OPN_PROBE_H

#define OPNA_BANK1_ADDR  0x018C
#define OPNA_BANK1_DATA  0x018E
#define OPNA_ID_PORT     0xA460
#define OPN_CLOCK_HZ     3993600UL
#define OPNA_CLOCK_HZ    7987200UL

enum opn_probe_result {
    OPN_PROBE_ABSENT, OPN_PROBE_UNKNOWN, OPN_PROBE_OPN, OPN_PROBE_OPNA
};

/* Quiet initialization destroys prior timer state. Caller owns the ports,
 * masks the FM IRQ at the interrupt controller before entry, and supplies
 * the current OPNA 29h write shadow (ignored by a YM2203). After starting B
 * we attempt stop/reset and shadow restoration even on bus failure. UNKNOWN
 * cannot confirm containment: keep the IRQ masked until hardware recovery.
 * On success, clear any pending IRQ before restoring the caller's IRQ mask. */
enum opn_probe_result opn_probe(unsigned char irq_shadow);

#endif
