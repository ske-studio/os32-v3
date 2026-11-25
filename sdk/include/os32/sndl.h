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

#define SNDL_POL_END 0
#define SNDL_POL_SOFT 1
#define SNDL_POL_FALLDOWN 2
#define SNDL_POL_FAIL 3
#define SNDL_REQ_PREFER_SOFT 1
#define SNDL_HW 1
#define SNDL_SOFT 2
#define SNDL_HW_FALL 3
typedef struct {
    u32 size, chip, need, flags;
    u8 fm_ch, ssg_ch, banks, rsv, pol[4];
} SNDL_ChipReq;
typedef struct {
    u32 size, backend, chip, fm_ch, ssg_ch, flags, lost_flags, lost_fm_ch;
} SNDL_ChipGot;

int sndl_open(const SND2_Fmt *fmt, u32 period, u32 lead);
/* Nonblocking. Call at least once every 10 ms for the low-latency stream. */
int sndl_pump(void);
/* End or reset unbounded loops before DRAIN. A stopped nonempty queue
 * must be resumed or reset. BUSY leaves the stream open. */
int sndl_close(u32 how);
int sndl_voice_pos(u32 voice, u32 *buffer, u32 *play, u32 *gen);
int sndl_chip_open(const SNDL_ChipReq *rq, u32 *handle, SNDL_ChipGot *got);
int sndl_chip_write(u32 handle, const SND2_RegW *w, u32 n, u32 at_us);
int sndl_chip_read(u32 handle, u32 bank, u32 what, u32 reg);
int sndl_chip_close(u32 handle);

#endif
