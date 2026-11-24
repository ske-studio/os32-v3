#ifndef SND2_H
#define SND2_H
#include "snd2_int.h"

typedef struct {
    const SndBackend *be;
    void *ctx;
    SND2_DevInfo info;
    SND2_Caps caps;
    Snd2Stream stream;
} Snd2Device;

Snd2Device *snd2_device(u32 dev);
void snd2_fault(Snd2Device *d);
int snd2_validate_open(const SND2_StreamDesc *desc, int v1,
                       u32 *period, u32 *lead, u32 *cap);

/* S5 supplies these adapters to its single teardown and handle/owner checks. */
void snd2_teardown_device(Snd2Device *d);
int snd2_owned_device(u32 stream, Snd2Device **out);
#endif
