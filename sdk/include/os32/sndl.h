#ifndef OS32_SNDL_H
#define OS32_SNDL_H

#include "os32api.h"
#include "os32_snd2.h"

#define SNDL_LOOP 1
#define SNDL_PLAYED 1
#define SNDL_RESET 2

/* Link libos32sndl.a. The first call binds the sound table in this AS.
 * sndl_render consumes an interval into interleaved stereo s16le PCM. */
int sndl_init(KernelAPI *api);
int sndl_voice_new(const SND2_Fmt *src, unsigned long *voice);
int sndl_voice_submit(unsigned long voice, const void *pcm, unsigned long bytes,
                      unsigned long flags, unsigned long *buf_id);
int sndl_voice_set(unsigned long voice, unsigned long hz, signed long vol_cb, signed long pan_cb);
int sndl_voice_play(unsigned long voice);
int sndl_voice_stop(unsigned long voice);
int sndl_voice_reset(unsigned long voice);
int sndl_voice_delete(unsigned long voice);
int sndl_voice_done(unsigned long voice, unsigned long *buf_id, unsigned long *how);
int sndl_render(short *pcm, unsigned long frames, unsigned long rate);

#endif
