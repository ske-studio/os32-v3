#include "bootinfo.h"
#include "bootinfo_native.h"
#include "../../init/boot_disk.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "build_id.h"
void bootinfo_report(void)
{
    char line[BOOTINFO_LINE_MAX];
    int i;

    if (bootinfo_get()->status != BOOTINFO_OK) {
        bootinfo_format_bios(bootinfo_get(), 0, line, (int)sizeof(line));
        kprintf(KLOG_ATTR, "%s\n", line);
    } else {
        for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
            if (!bootinfo_get()->drive[i].queried) continue;
            bootinfo_format_bios(bootinfo_get(), i, line, (int)sizeof(line));
            kprintf(KLOG_ATTR, "%s\n", line);
        }
    }

    /* IDENTIFY の値。hd0 / hd1 (バンク0 のマスタ・スレーブ) だけ。 */
    for (i = 0; i < (int)BOOTINFO_NDRIVES; i++) {
        struct bootinfo_ata a;

        if (!pc98_boot_disk_get() || pc98_boot_disk_get()->report_ata(i, &a) != 0) continue;
        bootinfo_format_ata(&a, i, line, (int)sizeof(line));
        kprintf(KLOG_ATTR, "%s\n", line);
    }
}

