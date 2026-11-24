#ifndef OS32_OPNA_H
#define OS32_OPNA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Internal ABI: 32-bit int/unsigned int, 8-bit unsigned char.
 * The caller provides exclusive, non-overlapping storage. OPNA_STATE_BYTES
 * and OPNA_STATE_ALIGN will be generated from the target's Rust layout.
 * New ADPCM RAM is zeroed by the caller and remains valid throughout use.
 * Return 0 on success or an OS32 error. Invalid inputs leave state intact.
 */
#define OPNA_ADPCM_BYTES 0x40000u
#define OPNA_MAX_FRAMES 256u

int opna_init(void *state, unsigned int bytes, unsigned char *adpcm,
              unsigned int adpcm_bytes);
/* Reset registers, phase and timers. Retain ADPCM RAM contents and samples. */
int opna_reset(void *state);
/* Stopped only. Borrow mono PCM16 LE until playback and callbacks stop.
 * ch = 0..5, source_rate > 0. Enable rhythm when all six samples exist. */
int opna_rhythm(void *state, unsigned int ch, const unsigned char *s16le,
                unsigned int frames, unsigned int source_rate);
/* reg includes the bank (0..0x1ff), value = 0..255, status bank = 0..1. */
int opna_write(void *state, unsigned int reg, unsigned int value);
int opna_read(void *state, unsigned int reg, unsigned int *value);
int opna_status(void *state, unsigned int bank, unsigned int *value);
/* Oracle wrapper advances timer and applies CSM. Limit each step to the
 * next deadline or 32767 us, whichever is smaller. No deadline: UINT_MAX. */
int opna_count(void *state, unsigned int delta_us, unsigned int *irq_level);
int opna_next_event(void *state, unsigned int *delta_us);
/* Overwrite 4*frames bytes of stereo PCM16 LE. Zero frames is a no-op.
 * Mix FM, SSG, ADPCM, rhythm in order, saturate once. No timer advance. */
int opna_render(void *state, unsigned char *out_s16le, unsigned int frames);

/* Private split timer. Count in normal context, queue a CSM event on
 * expiry in CSM mode, apply it at the corresponding audio frame. */
int opna_timer_write(void *timer_state, unsigned int reg, unsigned int value);
int opna_timer_count(void *timer_state, unsigned int delta_us, unsigned int *event);
int opna_apply_csm(void *audio_state);

#ifdef __cplusplus
}
#endif
#endif
