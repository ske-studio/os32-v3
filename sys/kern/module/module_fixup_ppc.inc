/* KMOD PPC records are little-endian. The relocated body is big-endian. */
#include "endian_le.h"

static int module_fixup_ppc_validate(const u8 *rec, u32 count,
                                     u32 body_size, u32 text_size)
{
    u32 end = 0;
    if (text_size > body_size) return -MODULE_RELOC_INVALID;
    for (u32 i = 0; i < count; i++, rec += 12) {
        u32 off = le32_rd(rec), kind = le32_rd(rec + 4);
        u32 width = kind == 1 ? 4 : 2;
        if (kind != 1 && (kind < 4 || kind > 6))
            return -MODULE_RELOC_INVALID;
        if (off % width || off < end || off > body_size || width > body_size - off)
            return -MODULE_RELOC_INVALID;
        if (le32_rd(rec + 8) > body_size) return -MODULE_RELOC_INVALID;
        end = off + width;
    }
    return 0;
}

/* body_size is mem_size (a target may be in BSS or at the end marker).
 * Caller has checked base + mem_size,
 * record table length, import slots and write offsets against payload size. */
static void module_fixup_ppc_apply(u8 *body, u32 base, const u8 *rec, u32 count)
{
    for (u32 i = 0; i < count; i++, rec += 12) {
        u8 *p = body + le32_rd(rec);
        u32 kind = le32_rd(rec + 4);
        u32 value = base + le32_rd(rec + 8);
        if (kind == 1) {
            *p++ = value >> 24;
            *p++ = value >> 16;
        } else if (kind == 5) {
            value >>= 16;
        } else if (kind == 6) {
            value = ((unsigned long long)value + 0x8000) >> 16;
        }
        p[0] = value >> 8;
        p[1] = value;
    }
}
