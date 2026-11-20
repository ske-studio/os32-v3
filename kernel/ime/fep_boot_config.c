#include "ime_store.h"
#include "kstring.h"

/* Core owns the immutable boot snapshot; a switch probe never updates it. */
static FepMeta boot_meta;
static u32 boot_reserve_kb;
static int boot_config_saved, boot_user_rc = OS32_ERR_AGAIN;
static char boot_user_path[OS32_MAX_PATH];

int fep_boot_config_get(FepMeta *out, u32 *reserve_kb)
{
    if (!out || !reserve_kb) return OS32_ERR_INVAL;
    if (!boot_config_saved) return OS32_ERR_AGAIN;
    *out = boot_meta;
    *reserve_kb = boot_reserve_kb;
    return 0;
}

int fep_boot_user_path_get(char *out, u32 capacity)
{
    if (!out || !capacity) return OS32_ERR_INVAL;
    if (boot_user_rc) return boot_user_rc;
    u32 n = kstrlen(boot_user_path);
    if (n >= capacity) return OS32_ERR_INVAL;
    kmemcpy(out, boot_user_path, n + 1);
    return 0;
}

void fep_boot_config_set(const FepMeta *meta, u32 reserve_kb)
{
    boot_meta = *meta;
    boot_reserve_kb = reserve_kb;
    boot_config_saved = 1;
}

void fep_boot_config_clear(void)
{
    boot_config_saved = 0;
    boot_user_rc = OS32_ERR_AGAIN;
}

int fep_boot_user_path_set(const char *path)
{
    if (!path) return OS32_ERR_INVAL;
    u32 n = kstrlen(path);
    if (n >= sizeof(boot_user_path)) return OS32_ERR_INVAL;
    kmemcpy(boot_user_path, path, n + 1);
    return 0;
}

void fep_boot_user_rc_set(int rc)
{
    boot_user_rc = rc > 0 ? OS32_ERR_IO : rc;
}
