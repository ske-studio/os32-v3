/* Private saved values shared by collection and formatting; not a KAPI ABI. */
#ifndef KERNEL_VER_INFO_H
#define KERNEL_VER_INFO_H

#include "os32_kapi_shared.h"
#include "os32_ver_info.h"
#include "sqlite_engine.h"

typedef struct {
    u32 cpu_measured, pit_valid, pit_hz, pit_input;
    u32 ser_ready, ser_mode, ser_baud, ser_fifo, pcm_present;
    GFX_ScreenInfo gfx; u32 gfx_active, gfx_kind;
    u32 api_version; char build[32];
    int boot_ok, debug_ok; BootImageInfo boot; MemStat debug;
    ServicesVerState services;
} VerSnapshot;

void ver_collect(VerSnapshot *s);
void ver_format(const VerSnapshot *s, OS32_VerInfo *out);

#endif
