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

int platform_install_check_media(KernelAPI *api, u32 ipl_len, u32 loader_len, u32 kernel_len);
int platform_install_identify(KernelAPI *api);
int platform_install_write_boot(KernelAPI *api, const InstTarget *t,
                                const u8 *ipl, u32 ipl_len,
                                const u8 *loader, u32 loader_len);

#endif
