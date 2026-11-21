#include "ide_boot.h"
#include "ide_contract.h"
#include "dev.h"

u32 pm_ide_base[PM_IDE_BANKS];
void pm_ide_boot(void)
{
    ide_init();
    for (int i = 0; i < IDE_MAX_DRIVES; ++i)
        if (ide_drive_present(i)) dev_register_hdd(i);
}
