#ifndef PCM_HW_H
#define PCM_HW_H
#include "types.h"

#ifdef TICK_PROBE
#include "pcm_module.h"
struct pcm_diag_init {
    PcmModuleApi api;
    struct irq10_diag *storage;
};
void pcm_bind_diag(struct irq10_diag *);
#endif
int pcm_codec_open(u32 rate, u32 period, void (*advance)(void), u8 **ring);
#ifdef SND2_REFILL_DIAG
void pcm_codec_counters(u32 *pi, u32 *ticks, u32 *max_us);
#endif
int pcm_codec_start(void);
int pcm_codec_pos(u32 *frame);
int pcm_codec_ack(void);
int pcm_codec_stop(void);
void pcm_codec_release(int leak);
void pcm_codec_finish_release(void);
void pcm_codec_volume(u8 left, u8 right);
int pcm_hw_register(void);
int pcm_hw_quiet(void);
#endif
