#include "loop_boot.h"
#include "loop_contract.h"
#include "ext2_medium_contract.h"

void pm_loop_boot(void)
{
    loop_dev_init();
#if OS32_FEATURE_IDE
    ext2_medium_register(EXT2_MEDIUM_LOOP, &ext2_medium_loop_ops);
#endif
}
