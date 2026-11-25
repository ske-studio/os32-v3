#ifndef SNDL_LIVE_H
#define SNDL_LIVE_H
#include "sndl.h"
#include "mix.h"
extern KernelAPI *sound_api;
int sndl_pcm_release(u32 how);
int sndl_pcm_active(void);
struct SndlWindow { u32 now, frames, offset; };
int sndl_chip_prepare(struct SndlWindow *window);
int sndl_chip_mix(int32_t *stereo, unsigned frames, unsigned rate, const struct SndlWindow *window);
int sndl_soft_pcm_acquire(const SND2_Fmt *fmt);
uint32_t sndl_mix_generation(uint32_t id);
int sndl_mix_queued(void);
int sndl_mix_can_drain(void);
int sndl_mix_pending(void);
void sndl_mix_abort(void);
#endif
