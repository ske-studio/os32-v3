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
    u32 table_writes;      /* Changed sectors among DDR and OS32 map entries 1..3. */
} HdprepGeom;

typedef struct {
    int drive;
    unsigned long start;
    unsigned long len;
    InstallPlanV1 media;
    u32 table_writes;      /* Changed sectors among DDR and OS32 map entries 1..3. */
} HdprepPlan;

/* The selected raw ATA unit is exported as the single hd0 partition view. */
#define PLATFORM_INSTALL_LOGICAL_DRIVE 0
#define PLATFORM_BOOT_COUNT 2
#define PLATFORM_INSTALL_ROOT_FILE "/sys/unicode.bin"
#define PLATFORM_INSTALL_CHECK_DIRECTORY pm_install_check_directory
#define PLATFORM_INSTALL_STREAM_PACKAGE pm_install_stage_package
#define PLATFORM_INSTALL_RELEASE_ASSETS pm_install_release_assets
int pm_install_check_directory(KernelAPI *api, const u32 *sizes);
int pm_install_stage_package(KernelAPI *api, const char *path, const u32 *offsets, const u32 *sizes);
void pm_install_release_assets(KernelAPI *api);
int pm_install_files_verified(KernelAPI *api);
#define PLATFORM_BOOT_AFTER_FILES 1

#endif
