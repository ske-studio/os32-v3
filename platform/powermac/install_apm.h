#ifndef PM_INSTALL_APM_H
#define PM_INSTALL_APM_H

/* TN1189 block-zero driver descriptor and 512-byte partition map entries. */
#include "os32_kapi_shared.h"

static inline void pm_put_word(u8 *p, u32 v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

static inline void pm_apm_header(u8 *s, u32 capacity)
{
    __builtin_memset(s, 0, 512);
    s[0] = 'E'; s[1] = 'R'; s[2] = 2;
    pm_put_word(s+4, capacity);
}

static const char *const pm_types[] = {"Apple_partition_map", "Apple_HFS", "Apple_UNIX_SVR2"};

static inline void pm_apm_entry(u8 *s, u32 entries, const InstallPlanPartitionV1 *part)
{
    const char *type = pm_types[part->type-1];
    __builtin_memset(s, 0, 512);
    s[0] = 'P'; s[1] = 'M';
    pm_put_word(s+4, entries);
    pm_put_word(s+8, part->start_lba);
    pm_put_word(s+12, part->sectors);
    for (unsigned i = 0; type[i]; ++i) s[48+i] = type[i];
    pm_put_word(s+84, part->sectors);
    pm_put_word(s+88, 0x33);
}
#endif
