#include "input_policy.h"
#include "os32_kapi_shared.h"

static u16 read16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 read32(const u8 *p)
{
    return (u32)read16(p) | ((u32)read16(p + 2) << 16);
}

int ikm_wire_header(const u8 *wire, u32 length, u32 *count)
{
    u32 n;
    if (!wire || !count || length < IKM_HEADER_SIZE ||
        wire[0] != 'I' || wire[1] != 'K' || wire[2] != 'M' || wire[3] != '1' ||
        read16(wire + 4) != 2 || read16(wire + 6) != IKM_ROW_SIZE ||
        read32(wire + 12) != 0)
        return OS32_ERR_INVAL;
    n = read32(wire + 8);
    if (n > IKM_MAX_ROWS || length != IKM_HEADER_SIZE + n * IKM_ROW_SIZE)
        return OS32_ERR_INVAL;
    *count = n;
    return 0;
}

int ikm_wire_row(const u8 *wire, u32 length, u32 index, IkmRow *row)
{
    u32 count;
    const u8 *p;
    IkmRow decoded;
    if (!row || ikm_wire_header(wire, length, &count) || index >= count)
        return OS32_ERR_INVAL;
    p = wire + IKM_HEADER_SIZE + index * IKM_ROW_SIZE;
    decoded.kind = p[0]; decoded.profile = p[1];
    decoded.context = p[2]; decoded.flags = p[3];
    decoded.index = read16(p + 4); decoded.selector = read16(p + 6);
    decoded.value = read32(p + 8); decoded.aux = read32(p + 12);
    *row = decoded;
    return 0;
}
