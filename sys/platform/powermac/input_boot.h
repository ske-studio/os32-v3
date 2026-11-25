#ifndef PM_INPUT_BOOT_H
#define PM_INPUT_BOOT_H
#include "types.h"

typedef struct {
    u32 started;
    int state, after_cr, pending;
} PmInputDecoder;

int pm_input_decode(PmInputDecoder *d, int byte, u32 now);
void pm_input_boot(void);
#endif
