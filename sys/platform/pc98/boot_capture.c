#include "platform_boot.h"
#include "bootinfo_native.h"
#include "bios_snapshot.h"
void platform_boot_capture(void)
{
    bootinfo_capture();
    v86_bios_save_real();
}
