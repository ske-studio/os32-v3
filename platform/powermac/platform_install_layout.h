#ifndef PLATFORM_INSTALL_LAYOUT_H
#define PLATFORM_INSTALL_LAYOUT_H

#include "os32_kapi_shared.h"

#if OS32_FEATURE_IDE
#define PLATFORM_HAS_INSTALL_TARGET 1
#define PLATFORM_HDPREP_FINISH pm_hdprep_finish
int pm_hdprep_finish(KernelAPI *api, int success);
#else
#define PLATFORM_HAS_INSTALL_TARGET 0
#endif

typedef struct {
    int drive;
    InstallPlanV1 media;
} HdprepGeom;

typedef struct {
    int drive;
    unsigned long start;
    unsigned long len;
    InstallPlanV1 media;
} HdprepPlan;

#define PLATFORM_BOOT_COUNT 1
#define PLATFORM_BOOT_AFTER_FILES 1

#endif
