#ifndef PLATFORM_VER_H
#define PLATFORM_VER_H

#include "ver_backend_contract.h"

extern const struct ver_backend pc98_ver_backend;

static inline const struct ver_backend *platform_ver_backend(void)
{
    return &pc98_ver_backend;
}

#endif
