#pragma once
#include "kstring.h"

static inline int pm_ide_compatible(const char *buf, int bytes)
{
    int start = 0;
    for (int end = 0; end < bytes; ++end) {
        if (buf[end]) continue;
        const char *item = buf + start;
        if (!kstrcmp(item, "keylargo-ata") || !kstrcmp(item, "heathrow-ata") ||
            !kstrcmp(item, "ohare-ata")) return 1;
        start = end + 1;
    }
    return 0;
}
