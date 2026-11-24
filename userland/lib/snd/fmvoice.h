#ifndef SNDL_FMVOICE_H
#define SNDL_FMVOICE_H
#include <stdint.h>

typedef struct FmVoice FmVoice;
typedef struct {
    void *(*alloc)(uint32_t bytes);
    void (*release)(void *);
} FmvMemory;

/* An absent file has a null read_at. Return the number of bytes read. */
typedef struct {
    void *file;
    uint32_t length;
    int (*read_at)(void *file, uint32_t offset, void *dst, uint32_t bytes);
} FmvSample;

/* Supply samples at creation, while stopped. external_timer selects V86 FIFO
 * order and external CSM events. All calls on one voice are serialized. */
int fmv_new(FmVoice **out, FmvMemory mem, uint32_t now, int external_timer,
            const FmvSample samples[6]);
void fmv_delete(FmVoice *v);
void fmv_reset(FmVoice *v, uint32_t now);
int fmv_write(FmVoice *v, uint32_t at, unsigned reg, unsigned value);
int fmv_csm(FmVoice *v, uint32_t at);
int fmv_read(FmVoice *v, uint32_t now, unsigned reg, unsigned *value);
int fmv_status(FmVoice *v, uint32_t now, unsigned bank, unsigned *value);
/* L3 calls once per generated interval. Output is overwritten stereo PCM16 LE.
 * A zero-frame call advances the timer only. Retry on allocation failure. */
int fmv_render(FmVoice *v, uint32_t now, unsigned char *out, uint32_t frames);
#endif
