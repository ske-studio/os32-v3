#ifndef PLATFORM_BUILD_INFO_H
#define PLATFORM_BUILD_INFO_H

#include "sqlite_engine.h"

static inline void platform_build_info_report(void)
{
    sqlite_engine_stack_report();
    services_report();
}

#endif
