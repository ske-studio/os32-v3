#include "ide_boot.h"
#include "install_backend.h"
#include "ide_contract.h"
#include "sys/dev/ata/cd_body.h"
#include "dev.h"
#include "apm.h"
#include "sys/fs/ext2/ext2.h"

u32 pm_ide_base[PM_IDE_BANKS];
void pm_ide_boot(void)
{
    ide_init();
    pm_format_register();
    CdModuleApi cd;
    if (pm_root_unit == -1 && pm_cd_bank >= 0 && pm_ide_base[pm_cd_bank] &&
        !cd_module_init(&cd) && cd.device)
        dev_register_cd(cd.device, 0);
}

int pm_root_select(void)
{
    IdeInfo info;
    if (pm_root_unit < 0 || ide_get_info(pm_root_unit, &info) || info.phys_sector_size != 512)
        return EXT2_ERR_IO;
    return pm_partition_register(pm_root_unit, info.total_sectors, pm_root_partition);
}
