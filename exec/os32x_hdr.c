/* D35: reject incompatible images before entry or AS allocation. */
#include "os32x_hdr.h"

int os32x_layout_check(const OS32Header *hdr, u32 read_len, u32 kernel_off)
{
    u32 known = OS32X_FLAG_GFX | OS32X_FLAG_RING3 | OS32X_FLAG_SHLIB |
                OS32X_FLAG_CUI_ONLY | OS32X_FLAG_LAUNCHER;
    if (!hdr || read_len < OS32X_HDR_SIZE) return OS32X_LAYOUT_SHORT;
    if (hdr->magic != OS32X_MAGIC || hdr->version != OS32X_HDR_VERSION ||
        hdr->header_size != OS32X_HDR_SIZE || (hdr->flags & ~known))
        return OS32X_LAYOUT_OLD;
    if (hdr->kapi_data_off != kernel_off ||
        hdr->kapi_abi_generation != OS32_KAPI_ABI_GENERATION ||
        hdr->memory_layout_generation != OS32_MEMORY_LAYOUT_GENERATION ||
        (hdr->shlib_protocol && hdr->shlib_protocol != OS32_SHLIB_PROTOCOL) ||
        ((hdr->flags & OS32X_FLAG_SHLIB) && hdr->shlib_protocol != OS32_SHLIB_PROTOCOL) ||
        hdr->min_api_ver > KAPI_VERSION)
        return OS32X_LAYOUT_MISMATCH;
    return OS32X_LAYOUT_OK;
}

const char *os32x_layout_reason(int rc)
{
    switch (rc) {
    case OS32X_LAYOUT_OK: return "ok";
    case OS32X_LAYOUT_SHORT: return "short header";
    case OS32X_LAYOUT_OLD: return "unknown format or flags";
    case OS32X_LAYOUT_MISMATCH: return "generation or KAPI layout mismatch";
    default: return "invalid header";
    }
}
