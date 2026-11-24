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

/* Quiet initialization only. Caller owns the ports and timer state. */
enum opn_probe_result opn_probe(void);

#endif
