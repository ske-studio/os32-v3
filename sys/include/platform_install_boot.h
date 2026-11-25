/* Machine operations used by the resident shell and installers. */
#ifndef PLATFORM_INSTALL_BOOT_H
#define PLATFORM_INSTALL_BOOT_H

#include "userland/system/inst_hdd.h"

/* Read-only inspection precedes confirmation. Probe and partition consume
 * the same plan whose drive/start/len the caller passes to ext2_format_at. */
void platform_hdprep_refuse(KernelAPI *api, int code);
void platform_hdprep_incomplete(KernelAPI *api, const char *what, int rc);
int platform_hdprep_check(KernelAPI *api, HdprepGeom *geometry);
void platform_hdprep_describe(KernelAPI *api, const HdprepPlan *plan);
int platform_hdprep_probe(KernelAPI *api, const HdprepPlan *plan);
int platform_hdprep_partition(KernelAPI *api, const HdprepPlan *plan);
/* Both installer front ends share this target and its verified partition. */
int platform_install_target(KernelAPI *api, InstTarget *target);
int platform_install_partition(KernelAPI *api, const InstTarget *target);

int platform_install_check_media(KernelAPI *api, const u32 *sizes, u32 kernel_len);
int platform_install_identify(KernelAPI *api);
/* The machine lists required assets in matching directory/package order and
 * chooses deployment before copying or after destination verification. */
extern const char *const platform_boot_paths[PLATFORM_BOOT_COUNT];
extern const char *const platform_boot_entries[PLATFORM_BOOT_COUNT];
int platform_install_directory_boot(KernelAPI *api, const InstTarget *target,
                                    const u32 *sizes, u8 *scratch, int capacity);
int platform_install_package_boot(KernelAPI *api, const InstTarget *target,
                                  const u8 *const *assets, const u32 *sizes);

#endif
