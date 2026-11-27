/* Caller holds IF=0 across the ownership query and the active probe. */
#ifndef PLATFORM_FM_PROBE_H
#define PLATFORM_FM_PROBE_H
#include "irq.h"

void platform_fm_block_irq12(void);
void platform_fm_release_irq12(void);
int platform_fm_irq12_released(void);

/* PC-98 sound IRQ selectors INT0, INT4, INT5, INT6. */
static inline int platform_fm_probe_busy(void)
{
    static const unsigned lines[] = {3, 10, 12, 13};
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        int index = irq_dyn_index(lines[i]);
        if (index >= 0 && (irq_lines[index].count || irq_lines[index].quarantined))
            return 1;
    }
    return 0;
}

#endif
