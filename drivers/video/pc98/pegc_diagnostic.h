/* Synchronous foreground loan; valid only while PEGC remains active. */
#ifndef PEGC_DIAGNOSTIC_H
#define PEGC_DIAGNOSTIC_H
#include "types.h"
struct pegc_diagnostic_view {
    const u8 *client;
    volatile u8 *vram;
};
int pegc_diagnostic_view(struct pegc_diagnostic_view *view);
#endif
