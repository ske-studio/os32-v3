#include "platform_device.h"
#include "ext2.h"
#include "serialfs.h"

int platform_ext2_format(int drv, u32 sectors)
{
    return ext2_format(drv, sectors);
}

int platform_ext2_format_at(int drv, u32 start_lba, u32 length)
{
    return ext2_format_at(drv, start_lba, length);
}

int platform_serialfs_session_begin(void)
{
    return serialfs_session_begin();
}

int platform_serialfs_session_end(int exit_code)
{
    return serialfs_session_end(exit_code);
}
