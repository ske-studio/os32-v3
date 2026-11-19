#ifndef PLATFORM_VER_H
#define PLATFORM_VER_H

#include "ver_backend_contract.h"

extern const struct ver_backend pm_ver_backend;
void pm_ver_note_pvr(u32 pvr);

static inline const struct ver_backend *platform_ver_backend(void)
{
    return &pm_ver_backend;
}

#endif
