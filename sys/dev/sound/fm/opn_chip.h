#ifndef OS32_OPN_CHIP_H
#define OS32_OPN_CHIP_H

#include "opn_probe.h"
#include "sys/hal/sound/snd2_int.h"

/* Register only a confirmed C0 result. The platform supplies the IRQ wiring. */
struct opn_chip {
    enum opn_probe_result model;
    u8 shared_irq, saved_id;
    /* Platform refreshes the 29h write shadow after external ownership. */
    u8 irq_shadow;
};

extern const SndChipBackend opn_chip_backend;
#endif
