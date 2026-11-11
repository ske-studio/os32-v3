/* D35: reject incompatible images before entry or AS allocation. */
#include "os32x_hdr.h"
#include "endian_le.h"

int os32x_decode(OS32Header *h, const u8 *wire, u32 len)
{
    if (len < OS32X_HDR_SIZE) return OS32X_LAYOUT_SHORT;
    h->magic = le32_rd(wire + 0);
    h->header_size = le32_rd(wire + 4);
    h->version = le32_rd(wire + 8);
    h->flags = le32_rd(wire + 12);
    h->entry_offset = le32_rd(wire + 16);
    h->text_size = le32_rd(wire + 20);
    h->bss_size = le32_rd(wire + 24);
    h->heap_size = le32_rd(wire + 28);
    h->stack_size = le32_rd(wire + 32);
    h->min_api_ver = le32_rd(wire + 36);
    h->load_addr = le32_rd(wire + 40);
    h->kapi_data_off = le32_rd(wire + 44);
    h->kapi_abi_generation = le32_rd(wire + 48);
    h->memory_layout_generation = le32_rd(wire + 52);
    h->shlib_protocol = le32_rd(wire + 56);
    h->code_size = le32_rd(wire + 60);
    return OS32X_LAYOUT_OK;
}


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
