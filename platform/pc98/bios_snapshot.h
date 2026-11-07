#ifndef PC98_BIOS_SNAPSHOT_H
#define PC98_BIOS_SNAPSHOT_H
#include "types.h"
int platform_bios_ready(void);
void v86_bios_save_real(void);
void v86_bios_restore_real(void);
const u8 *v86_bios_real_lowmem(void);
#endif
