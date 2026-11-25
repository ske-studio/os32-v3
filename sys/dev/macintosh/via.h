#ifndef MACINTOSH_VIA_H
#define MACINTOSH_VIA_H

#include "types.h"

enum via_kind { VIA_NONE, VIA_PMU, VIA_CUDA };

struct via_io {
    void *cookie;
    u8 (*read8)(void *, u32);
    void (*write8)(void *, u32, u8);
    unsigned long long (*tb)(void *);
    u32 tb_hz;
};

struct via_controller {
    enum via_kind kind;
    struct via_io io;
};

int via_prepare(struct via_controller *v);
/* Caller disables CPU IRQs before invoking this terminal operation. */
int via_restart(struct via_controller *v);
/* Read the unsigned big-endian RTC counter as seconds since 1904-01-01. */
int via_read_rtc(struct via_controller *v, u32 *seconds);

#endif
