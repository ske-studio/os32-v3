/* PC-9821 IRQ0 wiring: preserve the original timer callback order. */
#ifndef PC98_TICK_ORDER_H
#define PC98_TICK_ORDER_H

enum {
    PC98_TICK_SND, PC98_TICK_PCM, PC98_TICK_NE2K,
    PC98_TICK_E82557, PC98_TICK_LINK, PC98_TICK_LGY98,
    PC98_TICK_COUNT
};

#endif
