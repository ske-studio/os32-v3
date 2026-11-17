/* T3: frame-domain rescue threshold. No I/O or mutable state. */
#ifndef PCM_REFILL_H
#define PCM_REFILL_H
#include "pcm_cs4231.h"

#define PCM_REFILL_SAMPLE_US 10001U
#define PCM_REFILL_JITTER_US 800U
#define PCM_REFILL_COPY_US 180U /* provisional: physical acceptance required */
#define PCM_REFILL_ERROR_US 100U
#define PCM_REFILL_HEADROOM_US (PCM_REFILL_SAMPLE_US + PCM_REFILL_JITTER_US + \
                                PCM_REFILL_COPY_US + PCM_REFILL_ERROR_US)

/* Bounded products: no 64-bit division/libgcc.
 * Only the two supported rates are accepted by pcm_open. Each partial product
 * is bounded by 44100 * 11081 (and F * 1000000 fits u32). */
static inline u32 pcm_refill_guard_frames(u32 rate)
{
    return (rate * PCM_REFILL_HEADROOM_US + USEC_PER_SEC - 1U) / USEC_PER_SEC;
}

static inline u32 pcm_refill_threshold(u32 rate)
{
    return PCM_REFILL_MARGIN + pcm_refill_guard_frames(rate);
}

static inline u32 pcm_refill_threshold_us(u32 rate)
{
    return (pcm_refill_threshold(rate) * USEC_PER_SEC + rate - 1U) / rate;
}

static inline int pcm_tick_rescue_due(u32 pos, u32 rate)
{
    u32 distance = PCM_HALF_FRAMES - pos % PCM_HALF_FRAMES;
    return distance <= pcm_refill_threshold(rate);
}
#endif
