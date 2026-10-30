#ifndef OS32_BOOT_DISK_H
#define OS32_BOOT_DISK_H
#include "bootinfo.h"
#include "os32_kapi_shared.h"

/* Internal foreground/KAPI contract; excluded from the HWDEV wire umbrella. */
struct bootinfo_ata;
struct boot_disk_ops {
    int (*valid)(int drive);
    int (*bios_da)(int drive);
    int (*report_ata)(int drive, struct bootinfo_ata *out);
    int (*default_geom)(int drive, u16 *heads, u16 *spt);
    void (*fill_ata)(int drive, HddGeom *out);
};
#endif
