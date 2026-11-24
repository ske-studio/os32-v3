#ifndef SND2_H
#define SND2_H
#include "snd2_int.h"

typedef struct {
    const SndBackend *be;
    void *ctx;
    SND2_DevInfo info;
    SND2_Caps caps;
    Snd2Stream stream;
    u8 writing; /* A foreground copy still holds the staging allocation. */
} Snd2Device;

Snd2Device *snd2_device(u32 dev);
int snd2_refill_caps(Snd2Device *d, SND2_Caps *out);
void snd2_fault(Snd2Device *d);
int snd2_validate_open(const SND2_StreamDesc *desc, int v1,
                       u32 *period, u32 *lead, u32 *cap);

/* hw_start returns zero on success, this value for a proven pre-DMA
 * failure, and a negative error when stop proof must be obtained. */
#define SND2_START_STOPPED 1
int snd2_open_mode(const SND2_StreamDesc *desc, u32 *stream, int v1);

/* S5 supplies these adapters to its single teardown and handle/owner checks. */
void snd2_teardown_device(Snd2Device *d);
int snd2_owned_device(u32 stream, Snd2Device **out);
int snd2_write_checked(u32 id, const void *buf, u32 bytes,
                       void (*check)(const void *, u32));
int snd2_process_checked(u32 dev, u32 unit, const i32 *params, u32 n,
                         const void *in, void *out, u32 frames, u32 fi,
                         int (*check)(const void *, void *, u32, const SND2_Fmt *));
#endif
