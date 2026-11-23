#include "ide_boot.h"
#include "install_backend.h"
#include "ide_contract.h"
#include "dev.h"
#include "apm.h"
#include "fs/ext2/ext2.h"

u32 pm_ide_base[PM_IDE_BANKS];
void pm_ide_boot(void)
{
    ide_init();
    pm_format_register();
}

int pm_root_select(void)
{
    IdeInfo info;
    if (pm_root_unit < 0 || ide_get_info(pm_root_unit, &info) || info.phys_sector_size != 512)
        return EXT2_ERR_IO;
    return pm_partition_register(pm_root_unit, info.total_sectors, pm_root_partition);
}
