#ifndef SND2_REFILL_DIAG_H
#define SND2_REFILL_DIAG_H

/* Query snd2_caps(dev | SND2_CAPS_REFILL, ...) for a running stream.
 * The diagnostic snapshot occupies reserved[] only on this query. */
#define SND2_CAPS_REFILL 0x80000000UL
enum {
    SND2_DIAG_PI, SND2_DIAG_TICK, SND2_DIAG_MAX_US,
    SND2_DIAG_MIN_STG, SND2_DIAG_FRAME, SND2_DIAG_SEG_N,
    SND2_DIAG_NEXT_FIRST, SND2_DIAG_EVENT_STG, SND2_DIAG_DMA_STEP,
    SND2_DIAG_REFILL_TRIED
};

#endif
