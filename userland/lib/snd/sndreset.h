#ifndef OS32_USERLAND_SNDRESET_H
#define OS32_USERLAND_SNDRESET_H

#include "os32api.h"

static inline int sndreset_hardware(KernelAPI *api)
{
    if (api->version < 90) return OS32_ERR_NOTFOUND;
    int count = api->snd2_dev_count ? api->snd2_dev_count() : OS32_ERR_NOSYS;
    if (count == OS32_ERR_NOSYS) return OS32_ERR_NOTFOUND;
    if (count < 0) return count;
    for (int i = 0; i < count; ++i) {
        SND2_DevInfo info;
        int rc = api->snd2_dev_info(i, &info);
        if (rc) return rc;
        if (info.kind != SND2_KIND_FM) continue;
        u32 handle;
        rc = api->snd2_chip_open(info.dev, &handle);
        if (rc) return rc;
        rc = api->snd2_chip_reset(handle);
        int close_rc = api->snd2_chip_close(handle);
        return rc ? rc : close_rc;
    }
    return OS32_ERR_NOTFOUND;
}

#endif
