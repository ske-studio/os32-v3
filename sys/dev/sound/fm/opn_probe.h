#ifndef OS32_OPN_PROBE_H
#define OS32_OPN_PROBE_H
#include "types.h"

#define OPNA_BANK1_ADDR  0x018C
#define OPNA_BANK1_DATA  0x018E
#define OPNA_ID_PORT     0xA460
#define OPN_CLOCK_HZ     3993600UL
#define OPNA_CLOCK_HZ    7987200UL

enum opn_probe_result {
    OPN_PROBE_UNKNOWN, OPN_PROBE_ABSENT, OPN_PROBE_OPN, OPN_PROBE_OPNA,
    OPN_PROBE_PRESENT /* Timer transition, chip family unresolved. */
};

struct fm_probe_bank {
    enum opn_probe_result model;
    unsigned short base, chip_id_port;
    unsigned char chip_id;
};

/* A zero port denotes unconfirmed metadata. The resident owns this record
 * for the entire boot, including detach and module load failures. */
struct fm_probe_record {
    struct fm_probe_bank bank[2];
    enum opn_probe_result native_model;
    unsigned short feature_port, fifo_port;
    unsigned char feature_id, complete, contained;
};
void opn_probe_once(struct fm_probe_record *record, int busy, unsigned char irq_shadow,
                    int (*now_us)(u32 *, u32 *));
#endif
