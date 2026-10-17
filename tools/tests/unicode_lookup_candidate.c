/* T3d0 disposable kernel candidate, shared by the fixture and image probe.
 * This file is deliberately outside the product source lists. */
#include "unicode_jis_table.h"

u16 t3d0_unicode_to_jis(u32 cp)
{
    /* Preserve the current utf8.c arithmetic, including kana endpoints. */
    if (cp >= 0x3041 && cp <= 0x3093)
        return (u16)(0x2421 + (cp - 0x3041));
    if (cp >= 0x30A1 && cp <= 0x30F6)
        return (u16)(0x2521 + (cp - 0x30A1));
    if (cp > 0xFFFF) return 0;

    u32 lo = 0, hi = UNICODE_JIS_TABLE_SIZE;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (cp < unicode_jis_table[mid].unicode)
            hi = mid;
        else if (cp > unicode_jis_table[mid].unicode)
            lo = mid + 1;
        else
            return unicode_jis_table[mid].jis;
    }
    return 0;
}
