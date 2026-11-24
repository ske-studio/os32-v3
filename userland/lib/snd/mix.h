#ifndef SNDL_MIX_H
#define SNDL_MIX_H

#include <stdint.h>
#include "os32_snd2.h"

#define SNDL_VOICE_MAX 32
#define SNDL_LOOP 1
#define SNDL_PLAYED 1
#define SNDL_RESET 2
#define SNDL_AGAIN (-14) /* OS32_ERR_AGAIN */

int sndl_voice_new(const SND2_Fmt *src, unsigned long *voice);
int sndl_voice_submit(unsigned long voice, const void *pcm, unsigned long bytes,
                      unsigned long flags, unsigned long *buf_id);
int sndl_voice_set(unsigned long voice, unsigned long hz, signed long vol_cb, signed long pan_cb);
int sndl_voice_play(unsigned long voice);
int sndl_voice_stop(unsigned long voice);
int sndl_voice_reset(unsigned long voice);
int sndl_voice_delete(unsigned long voice);
int sndl_voice_done(unsigned long voice, unsigned long *buf_id, unsigned long *how);

/* L3 reserves each interval before generation. Zero refuses the interval.
 * Output offsets are relative to this render; L3 supplies the stream origin. */
typedef struct {
    uint32_t voice, buf_id, offset, count, first, phase16, step16, loop_len;
    int last;
} SndlMixSpan;
typedef int (*SndlMixRecord)(void *ctx, const SndlMixSpan *span);

/* Accumulate into a caller-cleared interleaved stereo block. Clip only after
 * every input (including a synthesizer) has contributed. */
void sndl_mix_render(int32_t *stereo, uint32_t frames, uint32_t rate,
                     SndlMixRecord record, void *ctx);
void sndl_mix_finish(const int32_t *stereo, int16_t *out, uint32_t frames);
void sndl_mix_played(uint32_t voice, uint32_t buf_id);
int sndl_mix_position(unsigned long voice, unsigned long *buf_id, unsigned long *frame);
#endif
